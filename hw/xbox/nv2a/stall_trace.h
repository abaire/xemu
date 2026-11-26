/*
 * Wall-clock accounting of where NV2A emulation threads wait or work.
 *
 * Enabled by setting XEMU_STALL_TRACE to an output file path. Every
 * XEMU_STALL_TRACE_INTERVAL_MS (default 250) a line is appended with, for each
 * site that was hit during the interval, the total time, the number of hits and
 * the longest single hit.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef HW_XBOX_NV2A_STALL_TRACE_H
#define HW_XBOX_NV2A_STALL_TRACE_H

#include "qemu/osdep.h"
#include "qemu/timer.h"

#define STALL_TRACE_SITES_XMAC                                              \
    /* GUI thread, pgraph_gl_get_framebuffer_surface */                     \
    _X(gui_pfifo_lock)                                                      \
    _X(gui_sync_wait)                                                       \
    /* pfifo thread, main loop */                                           \
    _X(pfifo_process_pending)                                               \
    _X(pfifo_pusher)                                                        \
    _X(pfifo_reports)                                                       \
    _X(pfifo_idle)                                                          \
    /* pfifo thread, pfifo_run_puller */                                    \
    _X(puller_pgraph_lock)                                                  \
    _X(puller_method)                                                       \
    _X(puller_pfifo_relock)                                                 \
    /* pfifo thread, BQL taken to raise interrupts */                       \
    _X(pfifo_bql_wait)                                                      \
    _X(pfifo_bql_hold)                                                      \
    /* pfifo thread, pgraph_gl_sync */                                      \
    _X(sync_early_return)                                                   \
    _X(sync_upload)                                                         \
    _X(sync_fence_render)                                                   \
    _X(sync_render_display)                                                 \
    _X(sync_fence_display)                                                  \
    /* pfifo thread, other renderer work */                                 \
    _X(report_query_wait)                                                   \
    _X(draws)                                                               \
    _X(flip_stalls)                                                         \
    /* vCPU thread, register access and surface reads */                    \
    _X(vcpu_pfifo_lock)                                                     \
    _X(vcpu_pgraph_lock)                                                    \
    _X(vcpu_surface_wait)

typedef enum StallTraceSite {
#define _X(x) ST_##x,
    STALL_TRACE_SITES_XMAC
#undef _X
    ST__COUNT
} StallTraceSite;

extern bool g_stall_trace_enabled;

void stall_trace_init(void);
void stall_trace_record(StallTraceSite site, int64_t start_ns, uint64_t count);

/* Returns a start time for stall_trace_end(), or 0 when tracing is off. */
static inline int64_t stall_trace_begin(void)
{
    return g_stall_trace_enabled ? get_clock() : 0;
}

static inline void stall_trace_end(StallTraceSite site, int64_t start_ns)
{
    if (start_ns) {
        stall_trace_record(site, start_ns, 1);
    }
}

/* Counts an event that has no duration. */
static inline void stall_trace_count(StallTraceSite site, uint64_t count)
{
    if (g_stall_trace_enabled) {
        stall_trace_record(site, 0, count);
    }
}

/* Wraps a blocking call so its wall-clock duration is recorded at `site`. */
#define STALL_TRACE(site, call)                     \
    do {                                            \
        int64_t _st_start = stall_trace_begin();    \
        call;                                       \
        stall_trace_end(ST_##site, _st_start);      \
    } while (0)

#endif
