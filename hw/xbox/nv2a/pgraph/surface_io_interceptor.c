/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "surface_io_interceptor.h"

#include "exec/memory.h"

// Used to intercept guest CPU/DMA interactions with a surface to
// facilitate syncing with GPU buffer.
typedef struct SurfaceIOInterceptor {
    bool in_use;
    MemoryRegion mem;
    NV2AState *nv2a_state;
    QemuMutex lock;

    // Base address (relative to nv2a_state->ram) of the intercepted region
    hwaddr base;
    // User data value that will be passed to surface memory operations.
    void *opaque;

    MemoryRegionOps surface_mem_ops;
    const MemoryRegionOps *delegate_surface_mem_ops;
} SurfaceIOInterceptor;


// TODO: Determine a reasonable upper bound
static SurfaceIOInterceptor surface_mmio_interceptors[32] = { 0 };
static QemuMutex surface_mmio_interceptors_lock;
static QemuEvent registration_action_completed;

#if defined(XEMU_DEBUG_BUILD)
#define SIOI_DUMP_STATS() \
    do {                  \
        dump_stats();     \
    } while (0)
static void dump_stats(void)
{
    int in_use = 0;
    for (int i = 0; i < ARRAY_SIZE(surface_mmio_interceptors); ++i) {
        SurfaceIOInterceptor *interceptor = &surface_mmio_interceptors[i];

        if (interceptor->in_use) {
            ++in_use;
        }
    }

    fprintf(stderr, "SIOI stats: %d in use\n\n", in_use);
}
#else
#define SIOI_DUMP_STATS() \
    do {                  \
    } while (0)
#endif

static uint64_t surface_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    SurfaceIOInterceptor *interceptor = (SurfaceIOInterceptor *)opaque;

    SurfaceIOInterceptorContext context;
    context.state = interceptor->nv2a_state;
    context.opaque = interceptor->opaque;

    qemu_mutex_lock(&interceptor->lock);
    assert(interceptor->in_use && "Hanging sioi read detected");
    uint64_t ret =
        interceptor->delegate_surface_mem_ops->read(&context, addr, size);
    qemu_mutex_unlock(&interceptor->lock);

    return ret;
}

static void surface_mem_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size)
{
    SurfaceIOInterceptor *interceptor = (SurfaceIOInterceptor *)opaque;

    SurfaceIOInterceptorContext context;
    context.state = interceptor->nv2a_state;
    context.opaque = interceptor->opaque;

    qemu_mutex_lock(&interceptor->lock);
    assert(interceptor->in_use && "Hanging sioi write detected");

    interceptor->delegate_surface_mem_ops->write(&context, addr, data, size);
    qemu_mutex_unlock(&interceptor->lock);
}

void sioi_init(void)
{
    qemu_mutex_init(&surface_mmio_interceptors_lock);
    qemu_event_init(&registration_action_completed, false);

    for (int i = 0; i < ARRAY_SIZE(surface_mmio_interceptors); ++i) {
        SurfaceIOInterceptor *interceptor = &surface_mmio_interceptors[i];
        memset(interceptor, 0, sizeof(*interceptor));

        qemu_mutex_init(&interceptor->lock);
        interceptor->surface_mem_ops.read = surface_mem_read;
        interceptor->surface_mem_ops.write = surface_mem_write;
        interceptor->surface_mem_ops.endianness = DEVICE_LITTLE_ENDIAN,
        interceptor->surface_mem_ops.valid.min_access_size = 1;
        interceptor->surface_mem_ops.valid.max_access_size = 8;
    }
}

static void register_interceptor_subregion(void *opaque)
{
    SurfaceIOInterceptor *interceptor = opaque;

    // Note: Priority must be higher than the system memory subregion.
    // See xbox.c
    memory_region_add_subregion_overlap(
        interceptor->nv2a_state->vram, interceptor->base, &interceptor->mem, 1);
    qemu_event_set(&registration_action_completed);
}

void sioi_claim(NV2AState *d, const MemoryRegionOps *surface_mem_ops,
                void *opaque, hwaddr base, uint32_t size)
{
    if (!tcg_enabled()) {
        // TODO: Implement or assert.
        return;
    }

    fprintf(stderr, "sioi_claim 0x%p :: " HWADDR_FMT_plx "\n", opaque, base);
    SIOI_DUMP_STATS();

    assert(!surface_mem_ops->write_with_attrs &&
           !surface_mem_ops->read_with_attrs && "with_attrs not implemented");

    qemu_mutex_lock(&surface_mmio_interceptors_lock);
    int i = 0;
    SurfaceIOInterceptor *interceptor;
    for (; i < ARRAY_SIZE(surface_mmio_interceptors); ++i) {
        interceptor = &surface_mmio_interceptors[i];

        if (qatomic_read(&interceptor->in_use)) {
            continue;
        }

        char name[64];
        snprintf(name, sizeof(name), "sioi." HWADDR_FMT_plx, base);

        qatomic_set(&interceptor->in_use, true);
        interceptor->nv2a_state = d;
        interceptor->base = base;
        interceptor->opaque = opaque;
        interceptor->delegate_surface_mem_ops = surface_mem_ops;
        interceptor->surface_mem_ops.valid = surface_mem_ops->valid;
        interceptor->surface_mem_ops.impl = surface_mem_ops->impl;

        memory_region_init_io(&interceptor->mem, memory_region_owner(d->vram),
                              &interceptor->surface_mem_ops, interceptor, name,
                              size);

        break;
    }
    qemu_mutex_unlock(&surface_mmio_interceptors_lock);

    assert(i < ARRAY_SIZE(surface_mmio_interceptors) &&
           "Failed to find a free SurfaceIOInterceptor!");

    // Schedule the subregion to be added within a safe BQL context. The
    // calling thread needs to be blocked until registration completes

    // Using aio_bh with an event guard causes deadlock during reset.
    // Not using an event guard can cause a race condition where a read/
    // write is processed before registration completes.
    //        qemu_event_reset(&registration_action_completed);
    //        aio_bh_schedule_oneshot(qemu_get_aio_context(),
    //                                register_interceptor_subregion,
    //                                interceptor);
    //        qemu_event_wait(&registration_action_completed);
    bql_lock();
    register_interceptor_subregion(interceptor);
    bql_unlock();
}

static void delete_interceptor_subregion(void *opaque)
{
    SurfaceIOInterceptor *interceptor = opaque;

    qemu_mutex_lock(&interceptor->lock);

    interceptor->delegate_surface_mem_ops = NULL;

    memory_region_del_subregion(interceptor->nv2a_state->vram,
                                &interceptor->mem);
    object_unparent(OBJECT(&interceptor->mem));

    qemu_mutex_unlock(&interceptor->lock);

    qatomic_set(&interceptor->in_use, false);
}

void sioi_release(void *opaque, hwaddr base)
{
    if (!tcg_enabled()) {
        // TODO: Implement or assert.
        return;
    }

    fprintf(stderr, "sioi_release 0x%p :: " HWADDR_FMT_plx "\n", opaque, base);
    SIOI_DUMP_STATS();

    qemu_mutex_lock(&surface_mmio_interceptors_lock);
    int i = 0;
    SurfaceIOInterceptor *interceptor = NULL;
    for (; i < ARRAY_SIZE(surface_mmio_interceptors); ++i) {
        interceptor = &surface_mmio_interceptors[i];

        if (!qatomic_read(&interceptor->in_use) ||
            interceptor->opaque != opaque || interceptor->base != base) {
            continue;
        }
        break;
    }

    qemu_mutex_unlock(&surface_mmio_interceptors_lock);

    assert(i < ARRAY_SIZE(surface_mmio_interceptors) &&
           "Failed to release SurfaceIOInterceptor!");

    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            delete_interceptor_subregion, interceptor);
}
