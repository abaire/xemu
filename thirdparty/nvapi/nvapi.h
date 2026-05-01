/*
 * NVIDIA Driver Settings Management
 *
 * Copyright (c) 2025 Matt Borgerson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef NVAPI_H
#define NVAPI_H

#include <windows.h>
#include <stdbool.h>

enum EValues_OGL_DX_PRESENT_DEBUG {
    OGL_DX_PRESENT_DEBUG_DISABLED = 0x00000000,
    OGL_DX_PRESENT_DEBUG_DISABLE_FULLSCREEN_OPT = 0x00000001,
    OGL_DX_PRESENT_DEBUG_DISABLE_THREAD = 0x00000002,
    OGL_DX_PRESENT_DEBUG_ENABLE_DFLIP_ALWAYS = 0x00000004,
    OGL_DX_PRESENT_DEBUG_ENABLE_NON_STEREO = 0x00000008,
    OGL_DX_PRESENT_DEBUG_UNDOCUMENTED_DXGI_SPEEDUP = 0x00000200,
    OGL_DX_PRESENT_DEBUG_ALLOW_DXVK_PROMOTION = 0x00080000,
    OGL_DX_PRESENT_DEBUG_ENABLE_FULLSCREEN_WIN7_STEREO = 0x10000000,
};

typedef struct NvApiProfileOpts {
    const wchar_t *profile_name;
    const wchar_t *executable_name;
    bool threaded_optimization;
    enum EValues_OGL_DX_PRESENT_DEBUG present_method_flags;
} NvApiProfileOpts;

bool nvapi_init(void);
bool nvapi_setup_profile(NvApiProfileOpts opts);
void nvapi_finalize(void);

#endif
