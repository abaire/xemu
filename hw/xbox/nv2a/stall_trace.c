/*
 * Wall-clock accounting of where NV2A emulation threads wait or work.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "hw/xbox/nv2a/stall_trace.h"

bool g_stall_trace_enabled;

typedef struct StallTraceCounter {
    uint64_t total_ns;
    uint64_t count;
    uint64_t max_ns;
} StallTraceCounter;

static StallTraceCounter counters[ST__COUNT];

static const char *site_names[ST__COUNT] = {
#define _X(x) #x,
    STALL_TRACE_SITES_XMAC
#undef _X
};

static FILE *output;
static unsigned int interval_ms = 250;
static QemuThread reporter_thread;

void stall_trace_record(StallTraceSite site, int64_t start_ns, uint64_t count)
{
    StallTraceCounter *c = &counters[site];
    qatomic_add(&c->count, count);
    if (!start_ns) {
        return;
    }

    uint64_t duration = get_clock() - start_ns;
    qatomic_add(&c->total_ns, duration);
    uint64_t max = qatomic_read(&c->max_ns);
    while (duration > max) {
        uint64_t prev = qatomic_cmpxchg(&c->max_ns, max, duration);
        if (prev == max) {
            break;
        }
        max = prev;
    }
}

/* Each line is "t_ms=<since start> <site>=<total us>/<count>/<max us> ...". */
static void *reporter_main(void *opaque)
{
    StallTraceCounter last[ST__COUNT] = { 0 };
    int64_t start = get_clock();

    while (true) {
        g_usleep(interval_ms * 1000);

        fprintf(output, "t_ms=%" PRId64, (get_clock() - start) / SCALE_MS);
        for (int i = 0; i < ST__COUNT; ++i) {
            uint64_t total = qatomic_read(&counters[i].total_ns);
            uint64_t count = qatomic_read(&counters[i].count);
            uint64_t max = qatomic_xchg(&counters[i].max_ns, 0);
            if (count == last[i].count) {
                continue;
            }
            fprintf(output, " %s=%" PRIu64 "/%" PRIu64 "/%" PRIu64,
                    site_names[i], (total - last[i].total_ns) / SCALE_US,
                    count - last[i].count, max / SCALE_US);
            last[i].total_ns = total;
            last[i].count = count;
        }
        fputc('\n', output);
        fflush(output);
    }

    return NULL;
}

void stall_trace_init(void)
{
    const char *path = getenv("XEMU_STALL_TRACE");
    if (!path || !*path || output) {
        return;
    }

    output = fopen(path, "w");
    if (!output) {
        fprintf(stderr, "stall_trace: failed to open %s\n", path);
        return;
    }

    const char *interval = getenv("XEMU_STALL_TRACE_INTERVAL_MS");
    if (interval && atoi(interval) > 0) {
        interval_ms = atoi(interval);
    }

    g_stall_trace_enabled = true;
    qemu_thread_create(&reporter_thread, "nv2a.stall_trace", reporter_main,
                       NULL, QEMU_THREAD_DETACHED);
}
