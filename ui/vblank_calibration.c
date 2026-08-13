/*
 * VBlank Interval Calibration
 *
 * Copyright (c) 2026 Matt Borgerson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "vblank_calibration.h"
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include <epoxy/gl.h>
#include <math.h>

#define VBLANK_TARGET_HEADROOM_NS 1000000LL
#define VBLANK_COARSE_SLEEP_HEADROOM_NS 2000000LL

#define CALIBRATION_TOTAL_FRAMES 10
#define WARMUP_FRAMES 3
#define MIN_VALID_INTERVALS 3

/*
 * Sleep headroom before vblank target. This leaves some room for OS thread
 * scheduling jitter and SDL event processing latency. As this value increases
 * the driver is allowed to wait longer for vblanks, potentially holding locks
 * that will stall the emulation thread.
 */
#define HEADROOM_NS 2000000

#define MIN_REASONABLE_INTERVAL_NS (NANOSECONDS_PER_SECOND / 600)
#define MAX_REASONABLE_INTERVAL_NS (NANOSECONDS_PER_SECOND / 30)

typedef struct {
    bool calibrated;
    int64_t interval_ns;
    int64_t last_swap_time_ns;
} VBlankCalibration;

static VBlankCalibration g_vblank_cal;

static int set_swap_interval_with_fallback(int interval)
{
    if (SDL_GL_SetSwapInterval(interval) == 0) {
        return 0;
    }
    if (interval == -1) {
        return SDL_GL_SetSwapInterval(1);
    }
    return -1;
}

void vblank_calibrate(SDL_Window *window)
{
    g_vblank_cal.calibrated = false;
    g_vblank_cal.interval_ns = 0;
    g_vblank_cal.last_swap_time_ns = 0;

    set_swap_interval_with_fallback(-1);

    int64_t swap_times[CALIBRATION_TOTAL_FRAMES];
    for (int i = 0; i < CALIBRATION_TOTAL_FRAMES; ++i) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
        swap_times[i] = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }

    int num_intervals = 0;
    double interval_sum = 0.0;
    for (int i = WARMUP_FRAMES + 1; i < CALIBRATION_TOTAL_FRAMES; ++i) {
        double interval = (double)(swap_times[i] - swap_times[i - 1]);
        if (interval >= MIN_REASONABLE_INTERVAL_NS &&
            interval <= MAX_REASONABLE_INTERVAL_NS) {
            interval_sum += interval;
            ++num_intervals;
        }
    }

    if (num_intervals >= 3) {
        g_vblank_cal.interval_ns = (int64_t)(interval_sum / num_intervals);
        g_vblank_cal.last_swap_time_ns =
            swap_times[CALIBRATION_TOTAL_FRAMES - 1];
        g_vblank_cal.calibrated = true;

        // DONOTSUBMIT
        fprintf(stderr,
                "vblank_calibrate: measured interval = %" PRId64 " ns "
                "(%d samples)\n",
                g_vblank_cal.interval_ns, num_intervals);
    } else {
        fprintf(stderr,
                "vblank_calibrate: calibration failed, "
                "insufficient valid samples (%d)\n",
                num_intervals);
    }
}

static int64_t vblank_get_time_to_next_vblank_ns(void)
{
    if (!g_vblank_cal.calibrated || g_vblank_cal.interval_ns <= 0) {
        return 0;
    }

    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    int64_t elapsed = now - g_vblank_cal.last_swap_time_ns;
    int64_t target_count =
        elapsed <= 0 ?
            1 :
            (elapsed + g_vblank_cal.interval_ns - 1) / g_vblank_cal.interval_ns;
    int64_t target_vblank = g_vblank_cal.last_swap_time_ns +
                            target_count * g_vblank_cal.interval_ns;

    return target_vblank - now;
}

void vblank_await_next(void)
{
    if (!g_vblank_cal.calibrated) {
        return;
    }

    int64_t time_to_vblank_ns = vblank_get_time_to_next_vblank_ns();

    if (time_to_vblank_ns > VBLANK_COARSE_SLEEP_HEADROOM_NS) {
        int64_t coarse_sleep_ns =
            time_to_vblank_ns - VBLANK_COARSE_SLEEP_HEADROOM_NS;
        SDL_DelayPrecise(coarse_sleep_ns);
        time_to_vblank_ns = vblank_get_time_to_next_vblank_ns();
    }

    if (time_to_vblank_ns > VBLANK_TARGET_HEADROOM_NS) {
        int64_t spin_target = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +
                              (time_to_vblank_ns - VBLANK_TARGET_HEADROOM_NS);
        while (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) < spin_target) {
        }
    }

    int64_t remaining_to_vblank = vblank_get_time_to_next_vblank_ns();
    if (remaining_to_vblank <= 0) {
        // DONOTSUBMIT: Debug message when pre-swap sleep overshot target vblank
        fprintf(stderr,
                "vblank pre-swap sleep overshot vblank by %" PRId64 " ns\n",
                -remaining_to_vblank);
    }
}

void vblank_notify_swap_complete(void)
{
    if (g_vblank_cal.calibrated) {
        g_vblank_cal.last_swap_time_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }
}

void vblank_calibration_reset(void)
{
    g_vblank_cal.calibrated = false;
    g_vblank_cal.interval_ns = 0;
    g_vblank_cal.last_swap_time_ns = 0;
}
