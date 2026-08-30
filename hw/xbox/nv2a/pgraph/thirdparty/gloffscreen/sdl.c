/*
 *  Offscreen OpenGL abstraction layer -- SDL based
 *
 *  Copyright (c) 2018-2024 Matt Borgerson
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "gloffscreen.h"

#include <SDL3/SDL.h>

typedef enum {
    GLO_TRANSFER_MODE_NONE,
    GLO_TRANSFER_MODE_SHARED_CONTEXT,
    GLO_TRANSFER_MODE_PBO_CHAIN
} GloTransferMode;

struct _GloContext {
    SDL_Window    *window;
    SDL_GLContext gl_context;

    GloTransferMode transfer_mode;

    union {
        struct {
            /* Shared Context mode */
            GLuint shared_texture;
        } shared;

        struct {
            GLuint pbos[3];
            int pbo_width[3];
            int pbo_height[3];
            GLenum pbo_format[3];
            GLenum pbo_type[3];
            bool pbo_valid[3];
            int pbo_index;

            void *pbo_cpu_data[3];
            int pbo_cpu_size[3];
            int pull_index;

            GLuint pull_texture;
            int pull_width;
            int pull_height;
            GLenum pull_format;
            GLenum pull_type;
        } pbo;
    };
};

static GloContext *glo_context_create_internal(GloContext *share, GloTransferMode transfer_mode)
{
    GloContext *context = (GloContext *)malloc(sizeof(GloContext));
    assert(context != NULL);
    memset(context, 0, sizeof(GloContext));

    context->transfer_mode = transfer_mode;
    if (transfer_mode == GLO_TRANSFER_MODE_PBO_CHAIN) {
        context->pbo.pull_index = -1;
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Initialize rendering context
    if (share) {
        SDL_GL_MakeCurrent(share->window, share->gl_context);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    } else {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE);

    // Create main window
    context->window = SDL_CreateWindow(
        "SDL Offscreen Window",
        640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (context->window == NULL) {
        fprintf(stderr, "%s: Failed to create window\n", __func__);
        SDL_Quit();
        exit(1);
    }

    context->gl_context = SDL_GL_CreateContext(context->window);
    if (context->gl_context == NULL) {
        fprintf(stderr, "%s: Failed to create GL context\n", __func__);
        SDL_DestroyWindow(context->window);
        SDL_Quit();
        exit(1);
    }

    glo_set_current(context);

    if (transfer_mode == GLO_TRANSFER_MODE_PBO_CHAIN) {
        glGenBuffers(3, context->pbo.pbos);
    }

    return context;
}

/* Create an OpenGL context */
GloContext *glo_context_create(void)
{
    return glo_context_create_internal(NULL, GLO_TRANSFER_MODE_NONE);
}

GloContext *glo_context_create_shared(GloContext *share)
{
    assert(share != NULL);
    return glo_context_create_internal(share, GLO_TRANSFER_MODE_SHARED_CONTEXT);
}

GloContext *glo_context_create_pbo(GloContext *share)
{
    return glo_context_create_internal(share, GLO_TRANSFER_MODE_PBO_CHAIN);
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
        SDL_GL_MakeCurrent(NULL, NULL);
    } else {
        SDL_GL_MakeCurrent(context->window, context->gl_context);
    }
}

static void glo_context_destroy_shared(GloContext *context)
{
    if (context->shared.shared_texture) {
        glDeleteTextures(1, &context->shared.shared_texture);
    }
}

static void glo_context_destroy_pbo(GloContext *context)
{
    glDeleteBuffers(3, context->pbo.pbos);
    for (int i = 0; i < 3; i++) {
        if (context->pbo.pbo_cpu_data[i]) {
            free(context->pbo.pbo_cpu_data[i]);
        }
    }
    if (context->pbo.pull_texture) {
        glDeleteTextures(1, &context->pbo.pull_texture);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) return;
    glo_set_current(context);

    if (context->transfer_mode == GLO_TRANSFER_MODE_SHARED_CONTEXT) {
        glo_context_destroy_shared(context);
    } else if (context->transfer_mode == GLO_TRANSFER_MODE_PBO_CHAIN) {
        glo_context_destroy_pbo(context);
    }

    glo_set_current(NULL);

    if (context->gl_context) {
        SDL_GL_DestroyContext(context->gl_context);
    }
    if (context->window) {
        SDL_DestroyWindow(context->window);
    }
    free(context);
}

static int get_bytes_per_pixel(GLenum format, GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: {
            switch (format) {
                case GL_RGBA:
                case GL_BGRA:
                    return 4;
                case GL_RGB:
                case GL_BGR:
                    return 3;
                case GL_RG:
                    return 2;
                case GL_RED:
                    return 1;
                default:
                    break;
            }
            break;
        }
        case GL_UNSIGNED_SHORT:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV:
            return 2;
        case GL_UNSIGNED_INT:
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV:
        case GL_UNSIGNED_INT_24_8:
            return 4;
        case GL_FLOAT:
            switch (format) {
                case GL_RGBA:
                case GL_BGRA:
                    return 16;
                case GL_RGB:
                case GL_BGR:
                    return 12;
                case GL_RG:
                    return 8;
                case GL_RED:
                    return 4;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    assert(0 && "Unsupported format/type combination in get_bytes_per_pixel");
    return 4;
}

void glo_context_push_framebuffer(GloContext *context, GLuint fbo, GLuint texture, int width, int height, GLenum format, GLenum type)
{
    if (context->transfer_mode == GLO_TRANSFER_MODE_NONE) {
        return;
    }
    if (context->transfer_mode == GLO_TRANSFER_MODE_SHARED_CONTEXT) {
        context->shared.shared_texture = texture;
        return;
    }

    int idx = context->pbo.pbo_index;
    GLuint pbo = context->pbo.pbos[idx];

    int bpp = get_bytes_per_pixel(format, type);
    int size = width * height * bpp;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    if (context->pbo.pbo_width[idx] != width || context->pbo.pbo_height[idx] != height ||
        context->pbo.pbo_format[idx] != format || context->pbo.pbo_type[idx] != type) {
        glBufferData(GL_PIXEL_PACK_BUFFER, size, NULL, GL_STREAM_READ);

        if (context->pbo.pbo_cpu_data[idx]) {
            free(context->pbo.pbo_cpu_data[idx]);
        }
        context->pbo.pbo_cpu_data[idx] = malloc(size);
        context->pbo.pbo_cpu_size[idx] = size;

        context->pbo.pbo_width[idx] = width;
        context->pbo.pbo_height[idx] = height;
        context->pbo.pbo_format[idx] = format;
        context->pbo.pbo_type[idx] = type;
    }

    glReadPixels(0, 0, width, height, format, type, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    context->pbo.pbo_valid[idx] = true;
    context->pbo.pbo_index = (context->pbo.pbo_index + 1) % 3;

    int copy_idx = -1;
    int prev_idx = (idx + 2) % 3;
    if (context->pbo.pbo_valid[prev_idx]) {
        copy_idx = prev_idx;
    } else if (context->pbo.pbo_valid[idx]) {
        // Fallback for the first frame
        copy_idx = idx;
    }

    if (copy_idx != -1) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, context->pbo.pbos[copy_idx]);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, context->pbo.pbo_cpu_size[copy_idx], context->pbo.pbo_cpu_data[copy_idx]);
        context->pbo.pull_index = copy_idx;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
}

GLuint glo_context_pull_framebuffer(GloContext *context)
{
    if (context->transfer_mode == GLO_TRANSFER_MODE_NONE) {
        return 0;
    }
    if (context->transfer_mode == GLO_TRANSFER_MODE_SHARED_CONTEXT) {
        return context->shared.shared_texture;
    }

    int idx = context->pbo.pull_index;
    if (idx < 0 || !context->pbo.pbo_valid[idx] || !context->pbo.pbo_cpu_data[idx]) {
        return 0;
    }

    int width = context->pbo.pbo_width[idx];
    int height = context->pbo.pbo_height[idx];
    GLenum format = context->pbo.pbo_format[idx];
    GLenum type = context->pbo.pbo_type[idx];
    void *data = context->pbo.pbo_cpu_data[idx];

    if (!context->pbo.pull_texture) {
        glGenTextures(1, &context->pbo.pull_texture);
    }

    glBindTexture(GL_TEXTURE_2D, context->pbo.pull_texture);
    if (context->pbo.pull_width != width || context->pbo.pull_height != height ||
        context->pbo.pull_format != format || context->pbo.pull_type != type) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLint internal_format = GL_RGBA8;
        if (format == GL_RGB) {
            internal_format = GL_RGB8;
        } else if (format == GL_RED) {
            internal_format = GL_R8;
        } else if (format == GL_RG) {
            internal_format = GL_RG8;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, NULL);
        context->pbo.pull_width = width;
        context->pbo.pull_height = height;
        context->pbo.pull_format = format;
        context->pbo.pull_type = type;
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, data);

    return context->pbo.pull_texture;
}
