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

#ifndef HW_XBOX_NV2A_PGRAPH_SURFACEIO_INTERCEPTOR_H
#define HW_XBOX_NV2A_PGRAPH_SURFACEIO_INTERCEPTOR_H

#include "hw/xbox/nv2a/nv2a_int.h"

/** Context object passed to memory region callbacks for intercepted range. */
typedef struct SurfaceIOInterceptorContext {
    NV2AState *state;

    // User data value provided when the interceptor was claimed.
    void *opaque;
} SurfaceIOInterceptorContext;

/**
 * Initializes the SurfaceIOInterceptor subsystem. Must be invoked before any
 * other sioi calls.
 */
void sioi_init(void);

/**
 * Schedules releases for any interceptors in use. Should be called when machine
 * state is reset to avoid bleeding over handlers.
 */
void sioi_reset(void);

/**
 * Sets up an interceptor for the given memory region.
 * @param parent MemoryRegion that the interceptor will overlay.
 * @param surface_mem_ops Defines the callbacks that will be invoked on
 *                        read/write.
 * @param opaque User data value to be passed in the SurfaceIOInterceptorContext
 *               to callbacks.
 * @param base Base address relative to the `parent` at which this interceptor
 *             will start.
 * @param size Size of the intercepted region, in bytes.
 */
void sioi_claim(NV2AState *state, const MemoryRegionOps *surface_mem_ops,
                void *opaque, hwaddr base, uint32_t size);

/**
 * Releases a previously claimed interceptor.
 * @param opaque User data passed during sioi_claim.
 * @param base Base address passed during sioi_claim.
 */
void sioi_release(void *opaque, hwaddr base);


static inline uint64_t sioi_default_read(void *ram_base, hwaddr offset,
                                         unsigned size)
{
    void *ram_ptr = ram_base + offset;
    switch (size) {
    case 1:
        return ldub_p(ram_ptr);

    case 2:
        return lduw_le_p(ram_ptr);

    case 4:
        return ldl_le_p(ram_ptr);

    case 8:
        return ldq_le_p(ram_ptr);

    default:
        assert(!"Invalid read size");
        return 0;
    }
}

static inline void sioi_default_write(void *ram_base, hwaddr offset,
                                      uint64_t data, unsigned size)
{
    void *ram_ptr = ram_base + offset;
    switch (size) {
    case 1:
        stb_p(ram_ptr, data);
        break;
    case 2:
        stw_le_p(ram_ptr, data);
        break;
    case 4:
        stl_le_p(ram_ptr, data);
        break;
    case 8:
        stq_le_p(ram_ptr, data);
        break;
    default:
        assert(!"invalid write size");
    }
}

#endif // HW_XBOX_NV2A_PGRAPH_SURFACEIO_INTERCEPTOR_H
