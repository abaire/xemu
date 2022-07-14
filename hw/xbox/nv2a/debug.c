/*
 * QEMU Geforce NV2A debug helpers
 *
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2012 espes
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

#include "debug.h"

#ifdef DEBUG_NV2A_GL

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#ifdef CONFIG_RENDERDOC
#include "thirdparty/renderdoc_app.h"
#ifdef _WIN32
#include <libloaderapi.h>
#else
#include <dlfcn.h>
#endif

static RENDERDOC_API_1_1_2 *rdoc_api = NULL;
static int32_t renderdoc_capture_frames = 0;
#endif

#define CHECK_GL_ERROR() do { \
  GLenum error = glGetError(); \
  if (error != GL_NO_ERROR) {  \
      fprintf(stderr, "OpenGL error: 0x%X (%d) at %s:%d\n", error, error, __FILE__, __LINE__); \
      assert(!"OpenGL error detected");                                                        \
  } \
} while(0)

static bool has_GL_GREMEDY_frame_terminator = false;
static bool has_GL_KHR_debug = false;

#define STREAM_GL_DEBUG_MESSAGES
#ifdef STREAM_GL_DEBUG_MESSAGES

static const char *gl_debug_type_names[] = {
        "ERROR",
        "DEPRECATED",
        "UNDEFINED",
        "PORTABILITY",
        "PERFORMANCE",
        "MARKER",
        "PUSH_GROUP",
        "POP_GROUP",
        "OTHER",
};

static const char *gl_debug_severity_names[] = {
        "HIGH",
        "MEDIUM",
        "LOW",
        "NOTIFICATION",
};

static void APIENTRY print_gl_debug_message(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {

    const char *type_name = "<UNKNOWN>";
    const char *severity_name = "<UNKNOWN>";

    if (type != GL_DEBUG_TYPE_ERROR) {
        return;
    }

    switch (type) {
        default:
            break;
        case GL_DEBUG_TYPE_ERROR:
            type_name = gl_debug_type_names[0];
            break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            type_name = gl_debug_type_names[1];
            break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            type_name = gl_debug_type_names[2];
            break;
        case GL_DEBUG_TYPE_PORTABILITY:
            type_name = gl_debug_type_names[3];
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            type_name = gl_debug_type_names[4];
            break;
        case GL_DEBUG_TYPE_MARKER:
            type_name = gl_debug_type_names[5];
            break;
        case GL_DEBUG_TYPE_PUSH_GROUP:
            type_name = gl_debug_type_names[6];
            break;
        case GL_DEBUG_TYPE_POP_GROUP:
            type_name = gl_debug_type_names[7];
            break;
        case GL_DEBUG_TYPE_OTHER:
            type_name = gl_debug_type_names[8];
            break;
    }

    switch (severity) {
        default:
            break;
        case GL_DEBUG_SEVERITY_HIGH:
            severity_name = gl_debug_severity_names[0];
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            severity_name = gl_debug_severity_names[1];
            break;
        case GL_DEBUG_SEVERITY_LOW:
            severity_name = gl_debug_severity_names[2];
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            severity_name = gl_debug_severity_names[3];
            break;
    }

    if (length < 0) {
        fprintf(stderr,"GLDBG[%s][%s]> %s\n", type_name, severity_name, message);
    } else {
        fprintf(stderr,"GLDBG[%s][%s]> %*s\n", type_name, severity_name, length, message);
    }
}
#endif

void gl_debug_initialize(void)
{
    has_GL_KHR_debug = glo_check_extension("GL_KHR_debug");
    has_GL_GREMEDY_frame_terminator = glo_check_extension("GL_GREMEDY_frame_terminator");

    if (has_GL_KHR_debug) {
#if defined(__APPLE__)
        /* On macOS, calling glEnable(GL_DEBUG_OUTPUT) will result in error
         * GL_INVALID_ENUM.
         *
         * According to GL_KHR_debug this should work, therefore probably
         * not a bug in our code.
         *
         * It appears however that we can safely ignore this error, and the
         * debug functions which we depend on will still work as expected,
         * so skip the call for this platform.
         */
#else
        glEnable(GL_DEBUG_OUTPUT);
        assert(glGetError() == GL_NO_ERROR);
#endif

#ifdef STREAM_GL_DEBUG_MESSAGES
        glDebugMessageCallback(print_gl_debug_message, NULL);
#endif
    }

#ifdef CONFIG_RENDERDOC
    const char *renderdoc_lib;
    void* renderdoc;
#ifdef __APPLE__
    renderdoc_lib = "librenderdoc.dylib";
#elif _WIN32
    renderdoc_lib = "renderdoc.dll";
#else
    renderdoc_lib = "librenderdoc.so";
#endif

#ifdef _WIN32
    renderdoc = GetModuleHandleA(renderdoc_lib);
    if (renderdoc) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(
            renderdoc, "RENDERDOC_GetAPI");
#else
    renderdoc = dlopen(renderdoc_lib, RTLD_NOW | RTLD_NOLOAD);
    if (renderdoc) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(
            renderdoc, "RENDERDOC_GetAPI");
#endif
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2,
                                   (void **)&rdoc_api);
        assert(ret == 1 && "Failed to retrieve RenderDoc API.");
    }
#endif
}

void gl_debug_message(bool cc, const char *fmt, ...)
{
    if (!has_GL_KHR_debug) {
        return;
    }

    size_t n;
    char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    assert(n <= sizeof(buffer));
    va_end(ap);

    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER,
                         0, GL_DEBUG_SEVERITY_NOTIFICATION, n, buffer);
    if (cc) {
        fwrite(buffer, sizeof(char), n, stdout);
        fputc('\n', stdout);
    }
}

void gl_debug_group_begin(const char *fmt, ...)
{
    /* Debug group begin */
    if (has_GL_KHR_debug) {
        size_t n;
        char buffer[1024];
        va_list ap;
        va_start(ap, fmt);
        n = vsnprintf(buffer, sizeof(buffer), fmt, ap);
        assert(n <= sizeof(buffer));
        va_end(ap);

        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, n, buffer);
    }

    /* Check for errors before starting real commands in group */
    assert(glGetError() == GL_NO_ERROR);
}

void gl_debug_group_end(void)
{
    /* Check for errors when leaving group */
    assert(glGetError() == GL_NO_ERROR);

    /* Debug group end */
    if (has_GL_KHR_debug) {
        glPopDebugGroup();
    }
}

void gl_debug_label(GLenum target, GLuint name, const char *fmt, ...)
{
    if (!has_GL_KHR_debug) {
        return;
    }

    size_t n;
    char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    assert(n <= sizeof(buffer));
    va_end(ap);

    glObjectLabel(target, name, n, buffer);

    GLenum err = glGetError();
    assert(err == GL_NO_ERROR);
}

void gl_debug_frame_terminator(void)
{
    CHECK_GL_ERROR();

#ifdef CONFIG_RENDERDOC
    if (rdoc_api) {
        if (rdoc_api->IsTargetControlConnected()) {
            if (rdoc_api->IsFrameCapturing()) {
                rdoc_api->EndFrameCapture(NULL, NULL);
                GLenum error = glGetError();
                if (error != GL_NO_ERROR) {
                    fprintf(stderr,
                            "Renderdoc EndFrameCapture triggered GL error 0x%X - ignoring\n",
                            error);
                }
            }
            if (renderdoc_capture_frames) {
                rdoc_api->StartFrameCapture(NULL, NULL);
                GLenum error = glGetError();
                if (error != GL_NO_ERROR) {
                    fprintf(stderr,
                            "Renderdoc StartFrameCapture triggered GL error 0x%X - ignoring\n",
                            error);
                }
                --renderdoc_capture_frames;
            }
        }
    }
#endif
    if (!has_GL_GREMEDY_frame_terminator) {
        return;
    }

    glFrameTerminatorGREMEDY();
    CHECK_GL_ERROR();
}

#ifdef CONFIG_RENDERDOC
bool nv2a_dbg_renderdoc_available(void) {
    return rdoc_api != NULL;
}

void nv2a_dbg_renderdoc_capture_frames(uint32_t num_frames) {
    renderdoc_capture_frames = num_frames;
}
#endif

#endif // DEBUG_NV2A_GL

#ifdef ENABLE_NV2A_DEBUGGER
#include <memory.h>
#include "nv2a_int.h"
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "sysemu/runstate.h"

enum NV2A_DBG_STATE {
    NV2A_DBG_RUNNING = 0,
    NV2A_DBG_STOPPED_FRAMEBUFFER_SWAP,
    NV2A_DBG_STOPPED_BEGIN_END
};

typedef struct NV2ADebuggerVMState {
    // Indicates that the VM should be paused at the next framebuffer swap.
    uint32_t frame_break_requested;
    // Indicates that the VM should be paused after N begin_end::end operations.
    uint32_t draw_end_break_requested;

    enum NV2A_DBG_STATE debugger_state;

    struct NV2AState* device;

    bool force_wireframe;
} NV2ADebuggerVMState;

static NV2ADebuggerVMState g_debugger_state;
static NV2ADbgState g_nv2a_info;

typedef struct NV2ADebuggerTextureState {
    GLuint texture;
    GLint internal_format;
    GLint width;
    GLint height;
    GLenum format;
    GLenum type;
} NV2ADebuggerTextureState;

#define MAX_TEXTURE_INFOS 512
static NV2ADebuggerTextureState g_texture_info[MAX_TEXTURE_INFOS] = {0};

static uint32_t g_frame_counter = 0;
static uint32_t g_draw_ends_since_last_frame = 0;
void (*nv2a_dbg_on_frame_stepped)(void) = NULL;

void nv2a_dbg_initialize(struct NV2AState* device)
{
    memset(&g_debugger_state, 0, sizeof(g_debugger_state));
    g_debugger_state.device = device;

    memset(&g_nv2a_info, 0, sizeof(g_nv2a_info));
    g_nv2a_stats.vram_ptr = device->vram_ptr;
    g_nv2a_stats.ramin_ptr = device->ramin_ptr;
    g_nv2a_stats.pfifo_regs = device->pfifo.regs;
}

static void resume_vm(void)
{
    enum NV2A_DBG_STATE run_state = qatomic_read(
            &g_debugger_state.debugger_state);
    if (run_state != NV2A_DBG_RUNNING) {
        qatomic_set(&g_debugger_state.debugger_state, NV2A_DBG_RUNNING);
        qatomic_set(
                &g_debugger_state.device->pgraph.waiting_for_nv2a_debugger,
                false);
        vm_start();
    }
    g_nv2a_info.draw_info.last_draw_operation = NV2A_DRAW_TYPE_INVALID;
}

void nv2a_dbg_step_frame(void)
{
    if (qatomic_read(&g_debugger_state.debugger_state) != NV2A_DBG_RUNNING) {
        qatomic_set(&g_debugger_state.frame_break_requested, true);
        resume_vm();
    } else {
        qatomic_set(&g_debugger_state.frame_break_requested, true);
    }
}

void nv2a_dbg_step_begin_end(uint32_t num_draw_calls)
{
    if (qatomic_read(&g_debugger_state.debugger_state) != NV2A_DBG_RUNNING) {
        qatomic_set(&g_debugger_state.draw_end_break_requested, num_draw_calls);
        resume_vm();
    } else {
        qatomic_set(&g_debugger_state.draw_end_break_requested, num_draw_calls);
    }
}

void nv2a_dbg_continue(void)
{
    qatomic_set(&g_debugger_state.frame_break_requested, false);
    qatomic_set(&g_debugger_state.draw_end_break_requested, false);
    resume_vm();
}

static void pause_vm(void)
{
    qemu_system_vmstop_request_prepare();
    qemu_system_vmstop_request(RUN_STATE_PAUSED);
    qatomic_set(
        &g_debugger_state.device->pgraph.waiting_for_nv2a_debugger,
        true);
}

void nv2a_dbg_handle_frame_swap(void)
{
    ++g_frame_counter;
    g_draw_ends_since_last_frame = 0;
    g_nv2a_info.draw_info.frame_counter = g_frame_counter;
    g_nv2a_info.draw_info.draw_ends_since_last_frame = g_draw_ends_since_last_frame;

    if (!qatomic_read(&g_debugger_state.frame_break_requested) ||
        qatomic_read(&g_debugger_state.debugger_state) != NV2A_DBG_RUNNING) {

        return;
    }

    pause_vm();

    qatomic_set(&g_debugger_state.frame_break_requested, false);
    qatomic_set(&g_debugger_state.debugger_state,
                NV2A_DBG_STOPPED_FRAMEBUFFER_SWAP);

    if (nv2a_dbg_on_frame_stepped) {
        nv2a_dbg_on_frame_stepped();
    }
}

void nv2a_dbg_handle_begin_end(NV2ADbgDrawInfo* info)
{
    PGRAPHState* pg = &g_debugger_state.device->pgraph;

    info->frame_counter = g_frame_counter;
    ++g_draw_ends_since_last_frame;
    info->draw_ends_since_last_frame = g_draw_ends_since_last_frame;

    info->fixed_function = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                    NV_PGRAPH_CSV0_D_MODE) == 0;
    info->lighting_enabled = GET_MASK(pg->regs[NV_PGRAPH_CSV0_C],
                                      NV_PGRAPH_CSV0_C_LIGHTING);

    for (int i = 0; i < NV2A_DBG_VERTEX_ATTR__COUNT; ++i) {
        NV2ADbgVertexInfo* dst = &info->vertex_attributes[i];
        VertexAttribute* src = &pg->vertex_attributes[i];

        dst->format = src->format;
        dst->size = src->size;
        dst->count = src->count;
        dst->stride = src->stride;
        memcpy(dst->inline_value, src->inline_value, sizeof(dst->inline_value));
    }

    g_nv2a_info.draw_info = *info;
    uint32_t draw_end_break_count =
        qatomic_read(&g_debugger_state.draw_end_break_requested);
    if (!draw_end_break_count ||
        qatomic_read(&g_debugger_state.debugger_state) != NV2A_DBG_RUNNING) {
        return;
    }

    --draw_end_break_count;
    qatomic_set(&g_debugger_state.draw_end_break_requested, draw_end_break_count);
    if (draw_end_break_count) {
        return;
    }

    pause_vm();
    qatomic_set(&g_debugger_state.debugger_state, NV2A_DBG_STOPPED_BEGIN_END);
}

void nv2a_dbg_handle_generate_texture(GLuint texture,
                                      GLint internal_format,
                                      uint32_t width,
                                      uint32_t height,
                                      GLenum format,
                                      GLenum type)
{
    NV2ADebuggerTextureState* info = g_texture_info;
    for (uint32_t i = 0; i < MAX_TEXTURE_INFOS; ++i, ++info) {
        if (info->texture && info->texture != texture) {
            continue;
        }

        info->texture = texture;
        info->internal_format = internal_format;
        info->width = (GLint)width;
        info->height = (GLint)height;
        info->format = format;
        info->type = type;
        return;
    }

    printf("nv2a_dbg_handle_generate_texture: ran out of info slots.\n");
}

void nv2a_dbg_handle_delete_texture(GLuint texture)
{
    NV2ADebuggerTextureState* info = g_texture_info;
    for (uint32_t i = 0; i < MAX_TEXTURE_INFOS; ++i, ++info) {
        if (info->texture == texture) {
            info->texture = 0;
            return;
        }
    }

    // TODO: Resolve this problem. This definitely happens in real usage.
//    printf("nv2a_dbg_handle_delete_texture: failed to delete texture info.\n");
}

static NV2ADebuggerTextureState* find_texture_info(GLuint texture)
{
    NV2ADebuggerTextureState* info = g_texture_info;
    for (uint32_t i = 0; i < MAX_TEXTURE_INFOS; ++i, ++info) {
        if (info->texture == texture) {
            return info;
        }
    }
    return NULL;
}

NV2ADbgState* nv2a_dbg_fetch_state(void)
{
    nv2a_dbg_free_state(&g_nv2a_info);

    PGRAPHState* pg = &g_debugger_state.device->pgraph;

    g_nv2a_info.draw_info.primitive_mode = pg->primitive_mode;
    g_nv2a_info.backbuffer_width = pg->surface_binding_dim.width;
    g_nv2a_info.backbuffer_height = pg->surface_binding_dim.height;

    g_nv2a_info.draw_info.depth_buffer_float_mode = pg->surface_shape.z_format;
    g_nv2a_info.draw_info.depth_buffer_mode = pg->surface_shape.zeta_format;

    NV2ADbgTextureInfo* tex_info = g_nv2a_info.textures;
    for (int i = 0; i < NV2A_MAX_TEXTURES; ++i) {
        uint32_t ctl_0 = pg->regs[NV_PGRAPH_TEXCTL0_0 + i*4];
        bool enabled = GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_ENABLE);
        if (!enabled) {
            continue;
        }

        TextureBinding* binding = pg->texture_binding[i];

        tex_info->slot = i;
        tex_info->target = binding->gl_target;
        tex_info->texture = binding->gl_texture;

        NV2ADebuggerTextureState* state = find_texture_info(
            binding->gl_texture);
        if (!tex_info) {
            printf("nv2a_dbg_fetch_state: Failed to look up texture %d\n",
                   binding->gl_texture);
            // The texture can probably still be rendered, use some defaults.
            tex_info->width = 64;
            tex_info->height = 64;
        } else {
            tex_info->width = state->width;
            tex_info->height = state->height;
        }

        ++tex_info;
    }
    return &g_nv2a_info;
}

void nv2a_dbg_free_state(NV2ADbgState* state)
{
    memset(state->textures, 0, sizeof(state->textures));
}

void nv2a_dbg_invalidate_shader_cache(void)
{
    PGRAPHState* pg = &g_debugger_state.device->pgraph;
    g_hash_table_remove_all(pg->shader_cache);
}

void nv2a_dbg_toggle_wireframe(void)
{
    g_debugger_state.force_wireframe = !g_debugger_state.force_wireframe;
}

bool nv2a_dbg_should_force_wireframe(void) {
    return g_debugger_state.force_wireframe;
}
#endif // ENABLE_NV2A_DEBUGGER
