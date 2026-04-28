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

#include "trace/control.h"

#ifdef CONFIG_RENDERDOC
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "thirdparty/renderdoc_app.h"
#endif
#include "qemu/log.h"

#define CHECK_GL_ERROR() do { \
  GLenum error = glGetError(); \
  if (error != GL_NO_ERROR) {  \
      fprintf(stderr, "OpenGL error: 0x%X (%d) at %s:%d\n", error, error, __FILE__, __LINE__); \
      assert(!"OpenGL error detected");                                                        \
  } \
} while(0)

static bool has_GL_GREMEDY_frame_terminator = false;
static bool has_GL_KHR_debug = false;

static int pgraph_dump_frames = 0;
static int pgraph_dump_frame_id = 0;
static int pgraph_dump_draw_id = 0;
static void *pgraph_dump_file = NULL;
static GMutex pgraph_dump_mutex;

static void write_initial_state_entry(gpointer key, gpointer value, gpointer user_data)
{
    FILE *f = (FILE *)user_data;
    uint32_t k = GPOINTER_TO_UINT(key);
    uint32_t p = GPOINTER_TO_UINT(value);

    uint32_t graphics_class = k >> 16;
    uint32_t method = k & 0xFFFF;

    /* Skip commands that trigger a draw, as they are not state and should
     * not be part of the initial hardware setup. */
    if (graphics_class == 0x97) {
        if (method < 0x200 ||
            method == NV097_SET_BEGIN_END ||
            method == NV097_ARRAY_ELEMENT16 ||
            method == NV097_ARRAY_ELEMENT32 ||
            method == NV097_DRAW_ARRAYS ||
            method == NV097_INLINE_ARRAY ||
            method == NV097_GET_REPORT ||
            method == NV097_CLEAR_SURFACE ||
            method == NV097_SET_CONTEXT_DMA_A ||
            method == NV097_SET_CONTEXT_DMA_B ||
            method == NV097_SET_CONTEXT_DMA_COLOR ||
            method == NV097_SET_CONTEXT_DMA_ZETA
            )
        {
            return;
        }
    }

    uint32_t entry[3];
    entry[0] = graphics_class;
    entry[1] = method;
    entry[2] = p;
    fwrite(entry, sizeof(uint32_t), 3, f);
}

static void nv2a_dbg_pgraph_log_handler(const char *fmt, va_list ap)
{
    if (g_str_has_prefix(fmt, "nv2a_pgraph_")) {
        g_mutex_lock(&pgraph_dump_mutex);
        if (pgraph_dump_file) {
            vfprintf((FILE *)pgraph_dump_file, fmt, ap);
        }
        g_mutex_unlock(&pgraph_dump_mutex);
    }
}

static void pgraph_dump_trace(const char *dir, const char *filename)
{
    g_mutex_lock(&pgraph_dump_mutex);
    if (!pgraph_dump_file) {
        g_mutex_unlock(&pgraph_dump_mutex);
        return;
    }
    fflush((FILE *)pgraph_dump_file);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);

    FILE *f_out = fopen(path, "w");
    if (f_out) {
        rewind((FILE *)pgraph_dump_file);
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), (FILE *)pgraph_dump_file)) > 0) {
            fwrite(buffer, 1, n, f_out);
        }
        fclose(f_out);

        if (ftruncate(fileno((FILE *)pgraph_dump_file), 0) != 0) {
            /* ignore error */
        }
        rewind((FILE *)pgraph_dump_file);
    }
    g_mutex_unlock(&pgraph_dump_mutex);
}

void nv2a_dbg_pgraph_dump_draws(int num_frames)
{
    if (num_frames > 0 && pgraph_dump_frames == 0) {
        trace_enable_events("nv2a_pgraph_*");
        g_mkdir_with_parents("pgraph_dump", S_IRWXU | S_IRWXG | S_IRWXO);
        pgraph_dump_file = (void *)fopen("pgraph_dump/trace.tmp", "w+");
        if (pgraph_dump_file) {
            qemu_set_log_handler(nv2a_dbg_pgraph_log_handler);
        }

        FILE *f_state = fopen("pgraph_dump/initial-state.bin", "wb");
        if (f_state) {
            g_hash_table_foreach(g_nv2a->pgraph.method_last_values,
                                 write_initial_state_entry, f_state);
            fclose(f_state);
        }
    }
    pgraph_dump_frames = num_frames;
    pgraph_dump_frame_id = 0;
    pgraph_dump_draw_id = 0;
    if (pgraph_dump_file && num_frames == 0) {
        qemu_set_log_handler(NULL);
        fclose((FILE *)pgraph_dump_file);
        pgraph_dump_file = NULL;
    }
}

static void pgraph_dump_vram(NV2AState *d, hwaddr addr, size_t size,
                             const char *path)
{
    if (addr + size > memory_region_size(d->vram)) {
        return;
    }
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(d->vram_ptr + addr, 1, size, f);
        fclose(f);
    }
}

void nv2a_dbg_pgraph_dump_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    if (pgraph_dump_frames <= 0) {
        return;
    }

    char dir[512];
    snprintf(dir, sizeof(dir), "pgraph_dump/frame_%03d/draw_%04d",
             pgraph_dump_frame_id, pgraph_dump_draw_id);
    int create_dir = g_mkdir_with_parents(dir, S_IRWXU | S_IRWXG | S_IRWXO);
    g_assert(create_dir == 0);

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (pgraph_is_texture_enabled(pg, i)) {
            TextureShape s = pgraph_get_texture_shape(pg, i);
            hwaddr addr = pgraph_get_texture_phys_addr(pg, i);
            size_t len = pgraph_get_texture_length(pg, &s);
            char path[1024];
            snprintf(path, sizeof(path), "%s/tex_%d_0x%08" PRIx64 ".bin", dir,
                     i, addr);
            pgraph_dump_vram(d, addr, len, path);

            size_t pal_len;
            hwaddr pal_addr =
                pgraph_get_texture_palette_phys_addr_length(pg, i, &pal_len);
            if (pal_len > 0) {
                snprintf(path, sizeof(path), "%s/pal_%d_0x%08" PRIx64 ".bin",
                         dir, i, pal_addr);
                pgraph_dump_vram(d, pal_addr, pal_len, path);
            }
        }
    }

    uint32_t min_vertex = 0;
    uint32_t max_vertex = 0;
    bool has_vertices = false;

    if (pg->draw_arrays_length) {
        min_vertex = pg->draw_arrays_min_start;
        max_vertex = pg->draw_arrays_max_count - 1;
        has_vertices = true;
    } else if (pg->inline_elements_length) {
        min_vertex = (uint32_t)-1;
        max_vertex = 0;
        for (int i = 0; i < pg->inline_elements_length; i++) {
            min_vertex = MIN(min_vertex, pg->inline_elements[i]);
            max_vertex = MAX(max_vertex, pg->inline_elements[i]);
        }
        has_vertices = true;
    } else if (pg->inline_array_length) {
        unsigned int offset = 0;
        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
            VertexAttribute *attr = &pg->vertex_attributes[i];
            if (attr->count == 0)
                continue;
            offset = ROUND_UP(offset, attr->size);
            offset += attr->size * attr->count;
            offset = ROUND_UP(offset, attr->size);
        }
        unsigned int vertex_size = offset;
        unsigned int index_count = pg->inline_array_length * 4 / vertex_size;
        min_vertex = 0;
        max_vertex = index_count - 1;
        has_vertices = true;
    }

    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attr = &pg->vertex_attributes[i];
        if (attr->count == 0)
            continue;

        char path[1024];
        if (pg->inline_buffer_length > 0) {
            snprintf(path, sizeof(path), "%s/vtx_%d_inline_buf.bin", dir, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(attr->inline_buffer, 1,
                       pg->inline_buffer_length * 4 * sizeof(float), f);
                fclose(f);
            }
        } else if (has_vertices && attr->offset != 0) {
            snprintf(path, sizeof(path), "%s/vtx_%d_0x%08" PRIx64 ".bin", dir,
                     i, attr->offset);
            uint32_t stride = attr->stride;
            uint32_t elem_size = attr->size * attr->count;
            if (stride == 0) {
                pgraph_dump_vram(d, attr->offset, elem_size, path);
            } else {
                hwaddr start = attr->offset + min_vertex * stride;
                hwaddr end = attr->offset + max_vertex * stride + elem_size;
                pgraph_dump_vram(d, start, end - start, path);
            }
        } else {
            snprintf(path, sizeof(path), "%s/vtx_%d_constant.bin", dir, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(attr->inline_value, 1, sizeof(attr->inline_value), f);
                fclose(f);
            }
        }
    }

    if (pg->inline_array_length > 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/inline_array.bin", dir);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(pg->inline_array, 1, pg->inline_array_length * 4, f);
            fclose(f);
        }
    }

    if (pg->inline_elements_length > 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/indices.bin", dir);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(pg->inline_elements, 1, pg->inline_elements_length * 4, f);
            fclose(f);
        }
    }

    char info_path[1024];
    snprintf(info_path, sizeof(info_path), "%s/info.txt", dir);
    FILE *f_info = fopen(info_path, "w");
    if (f_info) {
        fprintf(f_info, "Primitive Mode: %d\n", pg->primitive_mode);
        fprintf(f_info, "Draw Arrays Length: %d\n", pg->draw_arrays_length);
        fprintf(f_info, "Inline Elements Length: %d\n",
                pg->inline_elements_length);
        fprintf(f_info, "Inline Buffer Length: %d\n", pg->inline_buffer_length);
        fprintf(f_info, "Inline Array Length: %d\n", pg->inline_array_length);
        if (has_vertices) {
            fprintf(f_info, "Vertex Range: [%u, %u]\n", min_vertex, max_vertex);
        }
        fclose(f_info);
    }

    pgraph_dump_trace(dir, "pgraph-draw.txt");
    pgraph_dump_draw_id++;
}

void gl_debug_initialize(void)
{
    g_mutex_init(&pgraph_dump_mutex);
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

    if (pgraph_dump_frames > 0) {
        if (pgraph_dump_file) {
            char dir[512];
            snprintf(dir, sizeof(dir), "pgraph_dump/frame_%03d",
                     pgraph_dump_frame_id);
            pgraph_dump_trace(dir, "pgraph-terminator.txt");
        }
        --pgraph_dump_frames;
        ++pgraph_dump_frame_id;
        pgraph_dump_draw_id = 0;
        if (pgraph_dump_frames == 0) {
            trace_enable_events("-nv2a_pgraph_*");
            fprintf(stderr, "Frame recording completed\n");
            if (pgraph_dump_file) {
                qemu_set_log_handler(NULL);
                fclose((FILE *)pgraph_dump_file);
                pgraph_dump_file = NULL;
            }
        }
    }

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
                            "Renderdoc EndFrameCapture triggered GL error 0x%X - ignoring\n",
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
                                "Renderdoc StartFrameCapture triggered GL error 0x%X - ignoring\n",
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
        CHECK_GL_ERROR();
    }
}

#endif // DEBUG_NV2A_GL
