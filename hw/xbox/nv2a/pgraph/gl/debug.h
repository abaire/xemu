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

#ifndef HW_XBOX_NV2A_PGRAPH_GL_DEBUG_H
#define HW_XBOX_NV2A_PGRAPH_GL_DEBUG_H

#ifndef DEBUG_NV2A_GL
# define DEBUG_NV2A_GL 0
#endif

#if DEBUG_NV2A_GL

#include <stdbool.h>
#include "gloffscreen.h"
#include "config-host.h"

void gl_debug_initialize(void);
void gl_debug_message(bool cc, const char *fmt, ...);
void gl_debug_group_begin(const char *fmt, ...);
void gl_debug_group_end(void);
void gl_debug_label(GLenum target, GLuint name, const char *fmt, ...);
void gl_debug_frame_terminator(void);

# define NV2A_GL_DPRINTF(cc, format, ...) \
    gl_debug_message(cc, "nv2a: " format, ## __VA_ARGS__)
# define NV2A_GL_DGROUP_BEGIN(format, ...) \
    gl_debug_group_begin("nv2a: " format, ## __VA_ARGS__)
# define NV2A_GL_DGROUP_END() \
    gl_debug_group_end()
# define NV2A_GL_DLABEL(target, name, format, ...)  \
    gl_debug_label(target, name, "nv2a: { " format " }", ## __VA_ARGS__)
#define NV2A_GL_DFRAME_TERMINATOR() \
    gl_debug_frame_terminator()

#else

# define NV2A_GL_DPRINTF(cc, format, ...)          do { \
        if (cc) NV2A_DPRINTF(format "\n", ##__VA_ARGS__ ); \
    } while (0)
# define NV2A_GL_DGROUP_BEGIN(format, ...)         do { } while (0)
# define NV2A_GL_DGROUP_END()                      do { } while (0)
# define NV2A_GL_DLABEL(target, name, format, ...) do { } while (0)
# define NV2A_GL_DFRAME_TERMINATOR()               do { } while (0)
#endif

#endif


#ifdef ENABLE_NV2A_DEBUGGER
#include <epoxy/gl.h>

struct NV2AState;

typedef struct NV2ADbgTextureInfo {
    uint32_t slot;
    GLuint target;
    GLuint texture;
    GLint width;
    GLint height;
} NV2ADbgTextureInfo;

enum NV2A_DRAW_TYPE {
    NV2A_DRAW_TYPE_INVALID,
    NV2A_DRAW_TYPE_DRAW_ARRAYS,
    NV2A_DRAW_TYPE_INLINE_BUFFERS,
    NV2A_DRAW_TYPE_INLINE_ARRAYS,
    NV2A_DRAW_TYPE_INLINE_ELEMENTS,
    NV2A_DRAW_TYPE_EMPTY
};

typedef struct NV2ADbgVertexInfo {
    unsigned int format;
    unsigned int size; /* size of the data type */
    unsigned int count; /* number of components */
    uint32_t stride;

    float inline_value[4];
} NV2ADbgVertexInfo;

// TODO: make nv2a_reg.h includable from C++.
enum NV2A_DBG_VERTEX_ATTR {
    NV2A_DBG_VERTEX_ATTR_POSITION = 0,
    NV2A_DBG_VERTEX_ATTR_WEIGHT,
    NV2A_DBG_VERTEX_ATTR_NORMAL,
    NV2A_DBG_VERTEX_ATTR_DIFFUSE,
    NV2A_DBG_VERTEX_ATTR_SPECULAR,
    NV2A_DBG_VERTEX_ATTR_FOG,
    NV2A_DBG_VERTEX_ATTR_POINT_SIZE,
    NV2A_DBG_VERTEX_ATTR_BACK_DIFFUSE,
    NV2A_DBG_VERTEX_ATTR_BACK_SPECULAR,
    NV2A_DBG_VERTEX_ATTR_TEXTURE0,
    NV2A_DBG_VERTEX_ATTR_TEXTURE1,
    NV2A_DBG_VERTEX_ATTR_TEXTURE2,
    NV2A_DBG_VERTEX_ATTR_TEXTURE3,
    NV2A_DBG_VERTEX_ATTR__COUNT
};

typedef struct NV2ADbgTextureConfigInfo {
  union {
    struct {
      uint32_t COLOR_KEY_OP : 2;
      uint32_t ALPHA_KILL_ENABLE : 1;
      uint32_t IMAGE_FIELD_ENABLE : 1;
      uint32_t MAX_ANISO : 2;
      uint32_t MAX_LOD_CLAMP : 12;
      uint32_t MIN_LOD_CLAMP : 12;
      uint32_t ENABLE : 2;
    } bv;
    uint32_t v;
  } control0;

  union {
    struct {
      uint32_t _RESERVED : 16;
      uint32_t PITCH : 16;
    } bv;
    uint32_t v;
  } control1;

  union {
    struct {
      uint32_t U : 4;
      uint32_t CYLWRAP_U : 4;
      uint32_t V : 4;
      uint32_t CYLWRAP_V : 4;
      uint32_t P : 4;
      uint32_t CYLWRAP_P : 4;
      uint32_t CYLWRAP_Q : 4;
    } bv;
    uint32_t v;
  } address;

  union {
    struct {
      uint32_t _RESERVED0 : 1;
      uint32_t CONTEXT_DMA : 1;
      uint32_t CUBEMAP_ENABLE : 1;
      uint32_t BORDER_SOURCE : 1;
      uint32_t _RESERVED1 : 2;
      uint32_t DIMENSIONALITY : 2;
      uint32_t COLOR : 7;
      uint32_t _RESERVED2 : 1;
      uint32_t MIPMAP_LEVELS : 4;
      uint32_t BASE_SIZE_U : 4;
      uint32_t BASE_SIZE_V : 4;
      uint32_t BASE_SIZE_P : 4;
    } bv;
    uint32_t v;
  } format;

  union {
    struct {
      uint32_t LOD_BIAS : 13;
      uint32_t CONVOLUTION_KERNEL : 3;
      uint32_t MIN : 8;
      uint32_t MAG : 4;
      uint32_t A_SIGNED : 1;
      uint32_t R_SIGNED : 1;
      uint32_t G_SIGNED : 1;
      uint32_t B_SIGNED : 1;
    } bv;
    uint32_t v;
  } filter;

  union {
    struct {
      uint32_t HEIGHT : 16;
      uint32_t WIDTH : 16;
    } bv;
    uint32_t v;
  } image_rect;

  uint32_t border_color;
} NV2ADbgTextureConfigInfo;

// Keep in sync with NV2A_MAX_TEXTURES
// TODO: Make nv2a_regs.h includable from C++ files.
#define NV2A_DEBUGGER_NUM_TEXTURES 4

typedef struct NV2ADbgDrawInfo {
    uint32_t frame_counter;
    uint32_t draw_ends_since_last_frame;

    uint32_t primitive_mode;

    bool fixed_function;

    // Whether lighting was enabled.
    bool lighting_enabled;

    // The type of the last performed draw operation, indicating the vertex
    // data source.
    enum NV2A_DRAW_TYPE last_draw_operation;

    // The number of items (operation-dependent; e.g., vertices, indices) used
    // by the last draw operation.
    uint32_t last_draw_num_items;

    uint32_t depth_buffer_mode;
    bool depth_buffer_float_mode;

    // The vertex attributes used by the last draw call.
    NV2ADbgVertexInfo vertex_attributes[NV2A_DBG_VERTEX_ATTR__COUNT];

    NV2ADbgTextureConfigInfo texture_config[4];

    // The name of the texture containing the current nv2a backbuffer.
    GLint backbuffer_texture;
} NV2ADbgDrawInfo;

typedef struct NV2ADbgState {
    GLint backbuffer_width;
    GLint backbuffer_height;
    NV2ADbgTextureInfo textures[NV2A_DEBUGGER_NUM_TEXTURES];
    NV2ADbgDrawInfo draw_info;
} NV2ADbgState;

void nv2a_dbg_initialize(struct NV2AState* device);
void nv2a_dbg_step_frame(void);
void nv2a_dbg_step_begin_end(uint32_t num_draw_calls);
void nv2a_dbg_continue(void);
void nv2a_dbg_handle_frame_swap(void);
void nv2a_dbg_handle_begin_end(NV2ADbgDrawInfo* info);

extern void (*nv2a_dbg_on_frame_stepped)(void);

void nv2a_dbg_handle_generate_texture(GLuint texture,
                                    GLint internal_format,
                                    uint32_t width,
                                    uint32_t height,
                                    GLenum format,
                                    GLenum type);
void nv2a_dbg_handle_delete_texture(GLuint texture);

NV2ADbgState* nv2a_dbg_fetch_state(void);
void nv2a_dbg_free_state(NV2ADbgState* state);
void nv2a_dbg_invalidate_shader_cache(void);

void nv2a_dbg_toggle_wireframe(void);
bool nv2a_dbg_should_force_wireframe(void);

extern void (*nv2a_dbg_on_frame_stepped)(void);
#endif  //  ENABLE_NV2A_DEBUGGER
