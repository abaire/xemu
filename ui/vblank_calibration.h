/*
 * VBlank Interval Calibration
 *
 * Measures the display's vblank interval by rendering trivial frames with
 * vsync enabled while guest emulation is not running. The measured interval
 * is used to calculate a pre-swap sleep that minimizes time spent inside the
 * driver's blocking SDL_GL_SwapWindow call, mitigating AMD driver context
 * locking issues (xemu-project/xemu#2790).
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

#ifndef XEMU_VBLANK_CALIBRATION_H
#define XEMU_VBLANK_CALIBRATION_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

extern float g_ui_dropped_frame_average;
extern float g_swap_time_average;
extern uint64_t g_long_swap_count;

/**
 * Perform vblank interval calibration.
 *
 * Must be called while guest emulation is not running so timing measurements
 * are not contaminated by emulation thread GL operations.
 */
void vblank_calibrate(SDL_Window *window);

/**
 * Awaits the next upcoming vblank boundary, returning slightly before the
 * expected end of the current frame.
 */
void vblank_await_next(void);

/**
 * Record the completion time of SDL_GL_SwapWindow. Call immediately after
 * swap returns.
 */
void vblank_notify_swap_complete(int64_t pre_swap_ns);

/**
 * Reset/invalidate calibration data.
 */
void vblank_calibration_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* XEMU_VBLANK_CALIBRATION_H */
