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

#endif // HW_XBOX_NV2A_PGRAPH_SURFACEIO_INTERCEPTOR_H
