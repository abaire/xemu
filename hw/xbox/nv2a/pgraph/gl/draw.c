/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
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

#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "debug.h"
#include "renderer.h"

void pgraph_gl_clear_surface(NV2AState *d, uint32_t parameter)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_DPRINTF("---------PRE CLEAR ------\n");
    pg->clearing = true;

    GLbitfield gl_mask = 0;

    bool write_color = (parameter & NV097_CLEAR_SURFACE_COLOR);
    bool write_zeta =
        (parameter & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL));

    if (write_zeta) {
        GLint gl_clear_stencil;
        GLfloat gl_clear_depth;
        pgraph_get_clear_depth_stencil_value(pg, &gl_clear_depth,
                                             &gl_clear_stencil);

        if (parameter & NV097_CLEAR_SURFACE_Z) {
            gl_mask |= GL_DEPTH_BUFFER_BIT;
            glDepthMask(GL_TRUE);
            glClearDepth(gl_clear_depth);
        }
        if (parameter & NV097_CLEAR_SURFACE_STENCIL) {
            gl_mask |= GL_STENCIL_BUFFER_BIT;
            glStencilMask(0xff);
            glClearStencil(gl_clear_stencil);
        }
    }
    if (write_color) {
        gl_mask |= GL_COLOR_BUFFER_BIT;
        glColorMask((parameter & NV097_CLEAR_SURFACE_R)
                         ? GL_TRUE : GL_FALSE,
                    (parameter & NV097_CLEAR_SURFACE_G)
                         ? GL_TRUE : GL_FALSE,
                    (parameter & NV097_CLEAR_SURFACE_B)
                         ? GL_TRUE : GL_FALSE,
                    (parameter & NV097_CLEAR_SURFACE_A)
                         ? GL_TRUE : GL_FALSE);

        GLfloat rgba[4];
        pgraph_get_clear_color(pg, rgba);
        glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    pgraph_gl_surface_update(d, true, write_color, write_zeta);

    /* FIXME: Needs confirmation */
    unsigned int xmin =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTX), NV_PGRAPH_CLEARRECTX_XMIN);
    unsigned int xmax =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTX), NV_PGRAPH_CLEARRECTX_XMAX);
    unsigned int ymin =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTY), NV_PGRAPH_CLEARRECTY_YMIN);
    unsigned int ymax =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTY), NV_PGRAPH_CLEARRECTY_YMAX);

    NV2A_DPRINTF(
        "------------------CLEAR 0x%x %d,%d - %d,%d  %x---------------\n",
        parameter, xmin, ymin, xmax, ymax,
        d->pgraph.regs_[NV_PGRAPH_COLORCLEARVALUE]);

    unsigned int scissor_width = xmax - xmin + 1,
                 scissor_height = ymax - ymin + 1;
    pgraph_apply_anti_aliasing_factor(pg, &xmin, &ymin);
    pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);

    NV2A_DPRINTF("Translated clear rect to %d,%d - %d,%d\n", xmin, ymin,
                 xmin + scissor_width - 1, ymin + scissor_height - 1);

    bool full_clear = !xmin && !ymin &&
                      scissor_width >= pg->surface_binding_dim.width &&
                      scissor_height >= pg->surface_binding_dim.height;

    pgraph_apply_scaling_factor(pg, &xmin, &ymin);
    pgraph_apply_scaling_factor(pg, &scissor_width, &scissor_height);

    /* FIXME: Respect window clip?!?! */
    glEnable(GL_SCISSOR_TEST);
    glScissor(xmin, ymin, scissor_width, scissor_height);

    /* Dither */
    /* FIXME: Maybe also disable it here? + GL implementation dependent */
    if (pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) & NV_PGRAPH_CONTROL_0_DITHERENABLE) {
        glEnable(GL_DITHER);
    } else {
        glDisable(GL_DITHER);
    }

    glClear(gl_mask);

    glDisable(GL_SCISSOR_TEST);

    pgraph_gl_set_surface_dirty(pg, write_color, write_zeta);

    if (r->color_binding) {
        r->color_binding->cleared = full_clear && write_color;
    }
    if (r->zeta_binding) {
        r->zeta_binding->cleared = full_clear && write_zeta;
    }

    pg->clearing = false;
}

void pgraph_gl_draw_begin(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    // abaire
#if DEBUG_NV2A_GL
    NV2A_GL_DGROUP_BEGIN("NV097_SET_BEGIN_END: 0x%x %d", pg->primitive_mode, g_nv2a_current_frame_draw_count);
    {
        char buffer[256] = {0};
        sprintf(buffer, "frame_draw %d   ", g_nv2a_current_frame_draw_count);
        trace_nv2a_pgraph_method(-1, 0, 0, buffer, 0, 0);
    }
#endif
    // /abaire

    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    bool mask_alpha = control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
    bool mask_red = control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE;
    bool mask_green = control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE;
    bool mask_blue = control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
    bool color_write = mask_alpha || mask_red || mask_green || mask_blue;
    bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool stencil_test =
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    bool is_nop_draw = !(color_write || depth_test || stencil_test);

    pgraph_gl_surface_update(d, true, true, depth_test || stencil_test);

    if (is_nop_draw) {
        return;
    }

    assert(r->color_binding || r->zeta_binding);

    pgraph_gl_bind_textures(d);
    pgraph_gl_bind_shaders(pg);

    glColorMask(mask_red, mask_green, mask_blue, mask_alpha);
    glDepthMask(!!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE));
    glStencilMask(GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                           NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE));

    if (pgraph_reg_r(pg, NV_PGRAPH_BLEND) & NV_PGRAPH_BLEND_EN) {
        glEnable(GL_BLEND);
        uint32_t sfactor = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                                    NV_PGRAPH_BLEND_SFACTOR);
        uint32_t dfactor = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                                    NV_PGRAPH_BLEND_DFACTOR);
        assert(sfactor < ARRAY_SIZE(pgraph_blend_factor_gl_map));
        assert(dfactor < ARRAY_SIZE(pgraph_blend_factor_gl_map));
        glBlendFunc(pgraph_blend_factor_gl_map[sfactor],
                    pgraph_blend_factor_gl_map[dfactor]);

        uint32_t equation = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                                     NV_PGRAPH_BLEND_EQN);
        assert(equation < ARRAY_SIZE(pgraph_blend_equation_gl_map));
        glBlendEquation(pgraph_blend_equation_gl_map[equation]);

        uint32_t blend_color = pgraph_reg_r(pg, NV_PGRAPH_BLENDCOLOR);
        float gl_blend_color[4];
        pgraph_argb_pack32_to_rgba_float(blend_color, gl_blend_color);
        glBlendColor(gl_blend_color[0], gl_blend_color[1], gl_blend_color[2],
                     gl_blend_color[3]);
    } else {
        glDisable(GL_BLEND);
    }

    /* Face culling */
    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER)
            & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
        uint32_t cull_face = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
                                      NV_PGRAPH_SETUPRASTER_CULLCTRL);
        assert(cull_face < ARRAY_SIZE(pgraph_cull_face_gl_map));
        glCullFace(pgraph_cull_face_gl_map[cull_face]);
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }

    /* Front-face select */
    /* Winding is reverse here because clip-space y-coordinates are inverted */
    glFrontFace(pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER)
                    & NV_PGRAPH_SETUPRASTER_FRONTFACE
                        ? GL_CW : GL_CCW);

    /* Polygon offset */
    /* FIXME: GL implementation-specific, maybe do this in VS? */
    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
            NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE) {
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
            NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE) {
        glEnable(GL_POLYGON_OFFSET_LINE);
    } else {
        glDisable(GL_POLYGON_OFFSET_LINE);
    }
    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
            NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE) {
        glEnable(GL_POLYGON_OFFSET_POINT);
    } else {
        glDisable(GL_POLYGON_OFFSET_POINT);
    }
    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
            (NV_PGRAPH_SETUPRASTER_POFFSETFILLENABLE |
             NV_PGRAPH_SETUPRASTER_POFFSETLINEENABLE |
             NV_PGRAPH_SETUPRASTER_POFFSETPOINTENABLE)) {
        uint32_t zfactor_u32 = pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETFACTOR);
        GLfloat zfactor = *(float*)&zfactor_u32;
        uint32_t zbias_u32 = pgraph_reg_r(pg, NV_PGRAPH_ZOFFSETBIAS);
        GLfloat zbias = *(float*)&zbias_u32;
        // FIXME: with Linux and Mesa, zbias must be multiplied by 0.5 in
        // order to have the same depth value offset as Xbox.
        glPolygonOffset(zfactor, zbias);
    }

    /* Depth testing */
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);

        uint32_t depth_func = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0),
                                       NV_PGRAPH_CONTROL_0_ZFUNC);
        assert(depth_func < ARRAY_SIZE(pgraph_depth_func_gl_map));
        glDepthFunc(pgraph_depth_func_gl_map[depth_func]);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glEnable(GL_DEPTH_CLAMP);

    if (GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_3),
                 NV_PGRAPH_CONTROL_3_SHADEMODE) ==
        NV_PGRAPH_CONTROL_3_SHADEMODE_FLAT) {
        glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    }

    if (stencil_test) {
        glEnable(GL_STENCIL_TEST);

        uint32_t stencil_func = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                    NV_PGRAPH_CONTROL_1_STENCIL_FUNC);
        uint32_t stencil_ref = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                    NV_PGRAPH_CONTROL_1_STENCIL_REF);
        uint32_t func_mask = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ);
        uint32_t op_fail = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL);
        uint32_t op_zfail = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL);
        uint32_t op_zpass = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);

        assert(stencil_func < ARRAY_SIZE(pgraph_stencil_func_gl_map));
        assert(op_fail < ARRAY_SIZE(pgraph_stencil_op_gl_map));
        assert(op_zfail < ARRAY_SIZE(pgraph_stencil_op_gl_map));
        assert(op_zpass < ARRAY_SIZE(pgraph_stencil_op_gl_map));

        glStencilFunc(
            pgraph_stencil_func_gl_map[stencil_func],
            stencil_ref,
            func_mask);

        glStencilOp(
            pgraph_stencil_op_gl_map[op_fail],
            pgraph_stencil_op_gl_map[op_zfail],
            pgraph_stencil_op_gl_map[op_zpass]);

    } else {
        glDisable(GL_STENCIL_TEST);
    }

    /* Dither */
    /* FIXME: GL implementation dependent */
    if (pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) &
            NV_PGRAPH_CONTROL_0_DITHERENABLE) {
        glEnable(GL_DITHER);
    } else {
        glDisable(GL_DITHER);
    }

    glEnable(GL_PROGRAM_POINT_SIZE);

    bool anti_aliasing = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_ANTIALIASING), NV_PGRAPH_ANTIALIASING_ENABLE);

    /* Edge Antialiasing */
    if (!anti_aliasing && pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
                              NV_PGRAPH_SETUPRASTER_LINESMOOTHENABLE) {
        glEnable(GL_LINE_SMOOTH);
        glLineWidth(MIN(r->supported_smooth_line_width_range[1], pg->surface_scale_factor));
    } else {
        glDisable(GL_LINE_SMOOTH);
        glLineWidth(MIN(r->supported_aliased_line_width_range[1], pg->surface_scale_factor));
    }
    if (!anti_aliasing && pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
                              NV_PGRAPH_SETUPRASTER_POLYSMOOTHENABLE) {
        glEnable(GL_POLYGON_SMOOTH);
    } else {
        glDisable(GL_POLYGON_SMOOTH);
    }

    unsigned int vp_width = pg->surface_binding_dim.width,
                 vp_height = pg->surface_binding_dim.height;
    pgraph_apply_scaling_factor(pg, &vp_width, &vp_height);
    glViewport(0, 0, vp_width, vp_height);

    /* Surface clip */
    /* FIXME: Consider moving to PSH w/ window clip */
    unsigned int xmin = pg->surface_shape.clip_x,
                 ymin = pg->surface_shape.clip_y;

    unsigned int scissor_width = pg->surface_shape.clip_width,
                 scissor_height = pg->surface_shape.clip_height;

    pgraph_apply_anti_aliasing_factor(pg, &xmin, &ymin);
    pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);
    pgraph_apply_scaling_factor(pg, &xmin, &ymin);
    pgraph_apply_scaling_factor(pg, &scissor_width, &scissor_height);

    glEnable(GL_SCISSOR_TEST);
    glScissor(xmin, ymin, scissor_width, scissor_height);

    /* Visibility testing */
    if (pg->zpass_pixel_count_enable) {
        r->gl_zpass_pixel_count_query_count++;
        r->gl_zpass_pixel_count_queries = (GLuint*)g_realloc(
            r->gl_zpass_pixel_count_queries,
            sizeof(GLuint) * r->gl_zpass_pixel_count_query_count);

        GLuint gl_query;
        glGenQueries(1, &gl_query);
        r->gl_zpass_pixel_count_queries[
            r->gl_zpass_pixel_count_query_count - 1] = gl_query;
        glBeginQuery(GL_SAMPLES_PASSED, gl_query);
    }
}

void pgraph_gl_draw_end(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

#if DEBUG_NV2A_GL
    ++g_nv2a_current_frame_draw_count;  // abaire
#endif

    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    bool mask_alpha = control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
    bool mask_red = control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE;
    bool mask_green = control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE;
    bool mask_blue = control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
    bool color_write = mask_alpha || mask_red || mask_green || mask_blue;
    bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool stencil_test =
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    bool is_nop_draw = !(color_write || depth_test || stencil_test);

    if (is_nop_draw) {
        // FIXME: Check PGRAPH register 0x880.
        // HW uses bit 11 in 0x880 to enable or disable a color/zeta limit
        // check that will raise an exception in the case that a draw should
        // modify the color and/or zeta buffer but the target(s) are masked
        // off. This check only seems to trigger during the fragment
        // processing, it is legal to attempt a draw that is entirely
        // clipped regardless of 0x880. See xemu#635 for context.
        NV2A_GL_DGROUP_END();
        return;
    }

    pgraph_gl_flush_draw(d);

    /* End of visibility testing */
    if (pg->zpass_pixel_count_enable) {
        nv2a_profile_inc_counter(NV2A_PROF_QUERY);
        glEndQuery(GL_SAMPLES_PASSED);
    }

    pg->draw_time++;
    if (r->color_binding && pgraph_color_write_enabled(pg)) {
        r->color_binding->draw_time = pg->draw_time;
    }
    if (r->zeta_binding && pgraph_zeta_write_enabled(pg)) {
        r->zeta_binding->draw_time = pg->draw_time;
    }

    pgraph_gl_set_surface_dirty(pg, color_write, depth_test || stencil_test);
    NV2A_GL_DGROUP_END();
}

static inline void activate_register_carryover_state(PGRAPHGLState *r)
{
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER,
                  r->transform_feedback_texture_buffer_objects
                      [r->transform_feedback_read_buffer_index]);
}

static inline void deactivate_register_carryover_state(PGRAPHGLState *r)
{
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

static inline GLenum transform_feedback_primitive_for_gl_primitive(GLenum mode)
{
    switch (mode) {
    case GL_POINTS:
        return GL_POINTS;
    case GL_LINES:
    case GL_LINE_LOOP:
    case GL_LINE_STRIP:
    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
        return GL_LINES;
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        return GL_TRIANGLES;
    default:
        assert(!"Unsupported primitive mode for transform feedback");
    }
}

static inline GLenum transform_feedback_primitive_for_shader_primitive(enum ShaderPrimitiveMode mode)
{
    switch (mode) {
    case PRIM_TYPE_POINTS:
        return GL_POINTS;
    case PRIM_TYPE_LINES:
    case PRIM_TYPE_LINE_LOOP:
    case PRIM_TYPE_LINE_STRIP:
        return GL_LINES;
    case PRIM_TYPE_TRIANGLES:
    case PRIM_TYPE_TRIANGLE_STRIP:
    case PRIM_TYPE_TRIANGLE_FAN:
    case PRIM_TYPE_QUADS:
    case PRIM_TYPE_QUAD_STRIP:
    case PRIM_TYPE_POLYGON:
        return GL_TRIANGLES;
    default:
        assert(!"Unsupported primitive mode");
    }
}

static inline void setup_transform_feedback(PGRAPHGLState *r)
{
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,
                            r->transform_feedback_object);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                     r->transform_feedback_buffers
                         [!r->transform_feedback_read_buffer_index]);

    GLenum feedback_primitive_mode;
    if (pgraph_glsl_need_geom(&r->shader_binding->state.geom)) {
        feedback_primitive_mode =
            transform_feedback_primitive_for_shader_primitive(
                pgraph_glsl_get_geom_output_primitive(
                    &r->shader_binding->state.geom));
    } else {
        feedback_primitive_mode = transform_feedback_primitive_for_gl_primitive(
            r->shader_binding->gl_primitive_mode);
    }

    glBeginTransformFeedback(feedback_primitive_mode);
    // TODO: Replace with check macro
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr,
                    "BeginTransformFeedback failed: GL error: 0x%X %d - "
                    "primitive mode %d\n",
                    err, err, r->shader_binding->gl_primitive_mode);
            assert(!"glBeginTransformFeedback failed");
        }
    }
}

static inline void teardown_transform_feedback(PGRAPHGLState *r)
{
    glEndTransformFeedback();
    // TODO: Replace with check macro
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            fprintf(stderr, "glEndTransformFeedback: GL error: 0x%X %d\n", err,
                    err);
            assert(!"glEndTransformFeedback failed");
        }
    }

    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);

    r->transform_feedback_read_buffer_index =
        !r->transform_feedback_read_buffer_index;
}

static inline void draw_last_primitive_elements(GLenum mode, GLsizei count,
                                                const GLvoid *indices)
{
    GLenum new_mode = mode;
    GLsizei new_count = 0;
    const GLuint *u32_indices = (const GLuint *)indices;
    GLuint new_indices[6];

    switch (mode) {
    case GL_POINTS:
        assert(count >= 1 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_count = 1;
        new_indices[0] = u32_indices[count - 1];
        break;

    case GL_LINES:
        assert(count >= 2 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_count = 2;
        new_indices[0] = u32_indices[count - 2];
        new_indices[1] = u32_indices[count - 1];
        break;

    case GL_LINE_STRIP:
        assert(count >= 2 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_mode = GL_LINES;
        new_count = 2;
        new_indices[0] = u32_indices[count - 2];
        new_indices[1] = u32_indices[count - 1];
        break;

    case GL_LINE_LOOP:
        assert(count >= 2 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_mode = GL_LINES;
        new_count = 2;
        new_indices[0] = u32_indices[0];
        new_indices[1] = u32_indices[count - 1];
        break;

    case GL_TRIANGLES:
        assert(count >= 3 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_count = 3;
        new_indices[0] = u32_indices[count - 3];
        new_indices[1] = u32_indices[count - 2];
        new_indices[2] = u32_indices[count - 1];
        break;

    case GL_TRIANGLE_STRIP:
        assert(count >= 3 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_mode = GL_TRIANGLES;
        new_count = 3;
        new_indices[0] = u32_indices[count - 3];
        new_indices[1] = u32_indices[count - 2];
        new_indices[2] = u32_indices[count - 1];
        break;

    case GL_TRIANGLE_FAN:
        assert(count >= 3 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_mode = GL_TRIANGLES;
        new_count = 3;
        new_indices[0] = u32_indices[0];
        new_indices[1] = u32_indices[count - 2];
        new_indices[2] = u32_indices[count - 1];
        break;

    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
    case GL_QUADS:
    case GL_QUAD_STRIP:
        assert(count >= 4 && "draw_last_primitive_elements: not enough vertices for primitive");
        new_count = 4;
        new_indices[0] = u32_indices[count - 4];
        new_indices[1] = u32_indices[count - 3];
        new_indices[2] = u32_indices[count - 2];
        new_indices[3] = u32_indices[count - 1];
        break;

    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        assert(count >= 6 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_count = 6;
        new_indices[0] = u32_indices[count - 6];
        new_indices[1] = u32_indices[count - 5];
        new_indices[2] = u32_indices[count - 4];
        new_indices[3] = u32_indices[count - 3];
        new_indices[4] = u32_indices[count - 2];
        new_indices[5] = u32_indices[count - 1];
        break;

    case GL_POLYGON:
        break;

    default:
        assert(!"draw_last_primitive_elements: Unsupported primitive mode");
    }

    glDrawElements(new_mode, new_count, GL_UNSIGNED_INT, new_indices);
}

static inline void draw_last_primitive_arrays(GLenum mode, GLint first,
                                              GLsizei count)
{
    GLenum new_mode = mode;
    GLint new_first = 0;
    GLsizei new_count = 0;

    switch (mode) {
    case GL_POINTS:
        assert(count >= 1 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_first = first + count - 1;
        new_count = 1;
        break;

    case GL_LINES:
        assert(count >= 2 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_first = first + count - 2;
        new_count = 2;
        break;

    case GL_LINE_STRIP:
        assert(count >= 2 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_mode = GL_LINES;
        new_first = first + count - 2;
        new_count = 2;
        break;

    case GL_LINE_LOOP:
        assert(count >= 2 && "draw_last_primitive_arrays: not enough vertices for primitive");
        {
            GLuint new_indices[2] = { first, first + count - 1 };
            glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, new_indices);
        }
        return;

    case GL_TRIANGLES:
        assert(count >= 3 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_first = first + count - 3;
        new_count = 3;
        break;

    case GL_TRIANGLE_STRIP:
        assert(count >= 3 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_mode = GL_TRIANGLES;
        new_first = first + count - 3;
        new_count = 3;
        break;

    case GL_TRIANGLE_FAN:
        assert(count >= 3 && "draw_last_primitive_arrays: not enough vertices for primitive");
        {
            GLuint new_indices[3] = { first, first + count - 2,
                                      first + count - 1 };
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, new_indices);
        }
        return;

    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
    case GL_QUADS:
    case GL_QUAD_STRIP:
        assert(count >= 4 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_first = first + count - 4;
        new_count = 4;
        break;

    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        assert(count >= 6 && "draw_last_primitive_arrays: not enough vertices for primitive");
        new_first = first + count - 6;
        new_count = 6;
        break;

    case GL_POLYGON:
        break;

    default:
        assert(!"draw_last_primitive_arrays: Unsupported primitive mode");
    }

    glDrawArrays(new_mode, new_first, new_count);
}

// Re-renders the last primitive from the most recent flush_draw with transform
// feedback enabled.
//
// NOTE: This method may only be called from pgraph_gl_flush_draw as it skips
//   state validation and setup.
static inline void carryover_registers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_GL_DGROUP_BEGIN("CARRYOVER_REGISTERS");
    glResumeTransformFeedback();

    glEnable(GL_RASTERIZER_DISCARD);

    GLenum mode = r->shader_binding->gl_primitive_mode;

    // TODO: If a geometry shader is enabled, it is important to set the
    //  transform buffer primitive mode to match the geometry shader, otherwise
    //  a GL error will be emitted when attempting the last-primitive draw.
    //
    // This is most easily reproduced by rendering with polygon fill mode
    // disabled. E.g., the Line width tests:
    // https://abaire.github.io/nxdk_pgraph_tests_golden_results/results/Line_width/index.html

    if (pg->draw_arrays_length) {
        const int last_draw_idx = pg->draw_arrays_length - 1;
        const GLint first = pg->draw_arrays_start[last_draw_idx];
        const GLsizei count = pg->draw_arrays_count[last_draw_idx];
        draw_last_primitive_arrays(mode, first, count);
    } else if (pg->inline_elements_length) {
        draw_last_primitive_elements(mode, pg->inline_elements_length,
                                     pg->inline_elements);
    } else if (pg->inline_buffer_length) {
        draw_last_primitive_arrays(mode, 0, pg->inline_buffer_length);
    } else if (pg->inline_array_length) {
        unsigned int count = pgraph_gl_bind_inline_array(d);
        draw_last_primitive_arrays(mode, 0, count);
    }

    glDisable(GL_RASTERIZER_DISCARD);

    NV2A_GL_DGROUP_END();
}

void pgraph_gl_flush_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    if (!(r->color_binding || r->zeta_binding)) {
        return;
    }
    assert(r->shader_binding);

    activate_register_carryover_state(r);

    // macOS 15.7.1 has a GL bug that causes a segfault if a
    // glBeginTransformFeedback/glEndTransformFeedback block is executed
    // immediately after the primary rendering.
    // Because only the last vertex is of interest, feedback is immediately
    // paused and only re-enabled for a redraw of the last primitive.
    setup_transform_feedback(r);
    glPauseTransformFeedback();

    if (pg->draw_arrays_length) {
        NV2A_GL_DPRINTF(false, "Draw Arrays");
        nv2a_profile_inc_counter(NV2A_PROF_DRAW_ARRAYS);
        assert(pg->inline_elements_length == 0);
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        pgraph_gl_bind_vertex_attributes(d, pg->draw_arrays_min_start,
                                      pg->draw_arrays_max_count - 1,
                                      false, 0,
                                      pg->draw_arrays_max_count - 1);
        glMultiDrawArrays(r->shader_binding->gl_primitive_mode,
                          pg->draw_arrays_start,
                          pg->draw_arrays_count,
                          pg->draw_arrays_length);
    } else if (pg->inline_elements_length) {
        NV2A_GL_DPRINTF(false, "Inline Elements");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ELEMENTS);
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        uint32_t min_element = (uint32_t)-1;
        uint32_t max_element = 0;
        for (int i=0; i < pg->inline_elements_length; i++) {
            max_element = MAX(pg->inline_elements[i], max_element);
            min_element = MIN(pg->inline_elements[i], min_element);
        }

        pgraph_gl_bind_vertex_attributes(
                d, min_element, max_element, false, 0,
                pg->inline_elements[pg->inline_elements_length - 1]);

        VertexKey k;
        memset(&k, 0, sizeof(VertexKey));
        k.count = pg->inline_elements_length;
        k.gl_type = GL_UNSIGNED_INT;
        k.gl_normalize = GL_FALSE;
        k.stride = sizeof(uint32_t);
        uint64_t h = fast_hash((uint8_t*)pg->inline_elements,
                               pg->inline_elements_length * 4);

        LruNode *node = lru_lookup(&r->element_cache, h, &k);
        VertexLruNode *found = container_of(node, VertexLruNode, node);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, found->gl_buffer);
        if (!found->initialized) {
            nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_4);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         pg->inline_elements_length * 4,
                         pg->inline_elements, GL_STATIC_DRAW);
            found->initialized = true;
        } else {
            nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_4_NOTDIRTY);
        }
        glDrawElements(r->shader_binding->gl_primitive_mode,
                       pg->inline_elements_length, GL_UNSIGNED_INT,
                       (void *)0);
    } else if (pg->inline_buffer_length) {
        NV2A_GL_DPRINTF(false, "Inline Buffer");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_BUFFERS);
        assert(pg->inline_array_length == 0);

        if (pg->compressed_attrs) {
            pg->compressed_attrs = 0;
            pgraph_gl_bind_shaders(pg);
        }

        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
            VertexAttribute *attr = &pg->vertex_attributes[i];
            if (attr->inline_buffer_populated) {
                nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_3);
                glBindBuffer(GL_ARRAY_BUFFER, r->gl_inline_buffer[i]);
                glBufferData(GL_ARRAY_BUFFER,
                             pg->inline_buffer_length * sizeof(float) * 4,
                             attr->inline_buffer, GL_STREAM_DRAW);
                glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 0, 0);
                glEnableVertexAttribArray(i);
                attr->inline_buffer_populated = false;
                memcpy(attr->inline_value,
                       attr->inline_buffer + (pg->inline_buffer_length - 1) * 4,
                       sizeof(attr->inline_value));
            } else {
                glDisableVertexAttribArray(i);
                glVertexAttrib4fv(i, attr->inline_value);
            }
        }

        glDrawArrays(r->shader_binding->gl_primitive_mode,
                     0, pg->inline_buffer_length);
    } else if (pg->inline_array_length) {
        NV2A_GL_DPRINTF(false, "Inline Array");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ARRAYS);

        unsigned int index_count = pgraph_gl_bind_inline_array(d);
        glDrawArrays(r->shader_binding->gl_primitive_mode,
                     0, index_count);
    } else {
        NV2A_GL_DPRINTF(true, "EMPTY NV097_SET_BEGIN_END");
        NV2A_UNCONFIRMED("EMPTY NV097_SET_BEGIN_END");
    }

    carryover_registers(d);
    teardown_transform_feedback(r);
    deactivate_register_carryover_state(r);
}
