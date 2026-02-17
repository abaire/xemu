/*
 * Geforce NV2A PGRAPH OpenGL Renderer
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

#include "renderer.h"
#include "debug.h"

#if DEBUG_NV2A_GL

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#ifdef CONFIG_RENDERDOC
#include "trace/control.h"

#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "thirdparty/renderdoc_app.h"
#endif

#include "hw/xbox/nv2a/debug_gl.h"

static void dump_extensions(void)
{
    GLint num_extensions;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    for (int i = 0; i < num_extensions; ++i) {
        const GLubyte *ext = glGetStringi(GL_EXTENSIONS, i);
        fprintf(stderr, "GL ext: %s\n", ext);
    }
}

static bool has_GL_GREMEDY_frame_terminator = false;
static bool has_GL_KHR_debug = false;

static const char *gl_debug_type_names[] = {
    "ERROR",  "DEPRECATED", "UNDEFINED", "PORTABILITY", "PERFORMANCE",
    "MARKER", "PUSH_GROUP", "POP_GROUP", "OTHER",
};

static const char *gl_debug_severity_names[] = {
    "HIGH",
    "MEDIUM",
    "LOW",
    "NOTIFICATION",
};

static void APIENTRY print_gl_debug_message(GLenum source, GLenum type,
                                            GLuint id, GLenum severity,
                                            GLsizei length,
                                            const GLchar *message,
                                            const void *userParam)
{
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
        fprintf(stderr, "GLDBG[%s][%s]> %s\n", type_name, severity_name,
                message);
    } else {
        fprintf(stderr, "GLDBG[%s][%s]> %*s\n", type_name, severity_name,
                length, message);
    }
}

void gl_debug_initialize(void)
{
    dump_extensions();
    has_GL_KHR_debug = glo_check_extension("GL_KHR_debug");
    has_GL_GREMEDY_frame_terminator =
        glo_check_extension("GL_GREMEDY_frame_terminator");

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
#endif // defined(__APPLE__)

        glDebugMessageCallback(print_gl_debug_message, NULL);
        glDebugMessageControl(/* source= */ GL_DONT_CARE,
                              /* type= */ GL_DEBUG_TYPE_ERROR,
                              /* severity= */ GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(/* source= */ GL_DONT_CARE,
                              /* type= */ GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR,
                              /* severity= */ GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(/* source= */ GL_DONT_CARE,
                              /* type= */ GL_DEBUG_TYPE_PERFORMANCE,
                              /* severity= */ GL_DONT_CARE, 0, NULL, GL_TRUE);
        ASSERT_NO_GL_ERROR();
    }

#ifdef CONFIG_RENDERDOC
    nv2a_dbg_renderdoc_init();
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

    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                         GL_DEBUG_SEVERITY_NOTIFICATION, n, buffer);
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
    ASSERT_NO_GL_ERROR();
}

void gl_debug_group_end(void)
{
    /* Check for errors when leaving group */
    ASSERT_NO_GL_ERROR();

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

    ASSERT_NO_GL_ERROR();
}

void gl_debug_frame_terminator(void)
{
    ASSERT_NO_GL_ERROR();

#ifdef CONFIG_RENDERDOC
    if (nv2a_dbg_renderdoc_available()) {
        RENDERDOC_API_1_6_0 *rdoc_api = nv2a_dbg_renderdoc_get_api();

        if (rdoc_api->IsTargetControlConnected()) {
            bool capturing = rdoc_api->IsFrameCapturing();
            if (capturing && renderdoc_capture_frames == 0) {
                rdoc_api->EndFrameCapture(NULL, NULL);
                GLenum error = glGetError();
                if (error != GL_NO_ERROR) {
                    fprintf(stderr,
                            "Renderdoc EndFrameCapture triggered GL error 0x%X "
                            "- ignoring\n",
                            error);
                }
                if (renderdoc_trace_frames) {
                    trace_enable_events("-nv2a_pgraph_*");
                    renderdoc_trace_frames = false;
                }
            }
            if (renderdoc_capture_frames > 0) {
                if (!capturing) {
                    if (renderdoc_trace_frames) {
                        trace_enable_events("nv2a_pgraph_*");
                    }
                    rdoc_api->StartFrameCapture(NULL, NULL);
                    GLenum error = glGetError();
                    if (error != GL_NO_ERROR) {
                        fprintf(stderr,
                                "Renderdoc StartFrameCapture triggered GL "
                                "error 0x%X - ignoring\n",
                                error);
                    }
                }
                --renderdoc_capture_frames;
            }
        }
    }
#endif
    if (has_GL_GREMEDY_frame_terminator) {
        glFrameTerminatorGREMEDY();
        ASSERT_NO_GL_ERROR();
    }
}

void gl_debug_dump_log()
{
    if (!has_GL_KHR_debug) {
        return;
    }

    GLint num_messages = 0;
    glGetIntegerv(GL_DEBUG_LOGGED_MESSAGES, &num_messages);
    if (!num_messages) {
        return;
    }

    GLint max_message_length = 0;
    glGetIntegerv(GL_MAX_DEBUG_MESSAGE_LENGTH, &max_message_length);

    const size_t buffer_size = num_messages * max_message_length;
    GLchar *message_data = malloc(buffer_size);
    GLenum *sources = calloc(num_messages, sizeof(GLenum));
    GLenum *types = calloc(num_messages, sizeof(GLenum));
    GLenum *severities = calloc(num_messages, sizeof(GLenum));
    GLuint *ids = calloc(num_messages, sizeof(GLuint));
    GLsizei *message_lengths = calloc(num_messages, sizeof(GLsizei));

    if (!(message_data && sources && types && severities && ids &&
          message_lengths)) {
        fprintf(stderr, "Failed to alloc memory for %d GL debug messages\n",
                num_messages);
        goto cleanup;
    }

    GLuint retrieved =
        glGetDebugMessageLog(num_messages, buffer_size, sources, types, ids,
                             severities, message_lengths, message_data);

    GLchar *next_message = message_data;
    for (GLuint i = 0; i < retrieved; ++i) {
        print_gl_debug_message(sources[i], types[i], ids[i], severities[i],
                               message_lengths[i], next_message, NULL);
        next_message += message_lengths[i];
    }

cleanup:
    if (message_data) {
        free(message_data);
    }
    if (sources) {
        free(sources);
    }
    if (types) {
        free(types);
    }
    if (severities) {
        free(severities);
    }
    if (ids) {
        free(ids);
    }
    if (message_lengths) {
        free(message_lengths);
    }
}

#endif // DEBUG_NV2A_GL
