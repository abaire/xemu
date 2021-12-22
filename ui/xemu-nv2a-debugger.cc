#include "xemu-nv2a-debugger.h"

#ifdef ENABLE_NV2A_DEBUGGER
#include <cstdio>
#include <string>

#include "hw/xbox/nv2a/nv2a_regs.h"
#include "imgui/imgui.h"
#include "xemu-custom-widgets.h"


static struct fbo* g_last_stored_backbuffer_fbo = NULL;
static struct fbo* g_texture_debugger_fbo[NV2A_DEBUGGER_NUM_TEXTURES] = {NULL};

NV2ADebugger::NV2ADebugger() : is_open(false), initialized(false), shader(NULL)
{
    texture_debugger_clear_color[0] = 1.0f;
    texture_debugger_clear_color[1] = 0.0f;
    texture_debugger_clear_color[2] = 1.0f;
}

void NV2ADebugger::Initialize()
{
    initialized = true;
    shader = create_decal_shader(SHADER_TYPE_BLIT);
    shader->flip = false;
}

void NV2ADebugger::Draw(ImFont *fixed_width_font,
                        float ui_scale,
                        float main_menu_height)
{
    if (!is_open) return;
    if (!initialized) {
        Initialize();
    }

    ImGuiIO& io = ImGui::GetIO();
    DrawDebuggerControls(io, fixed_width_font, ui_scale, main_menu_height);
    DrawLastDrawInfoOverlay(io, fixed_width_font, ui_scale, main_menu_height);
    DrawTextureOverlay(io, fixed_width_font, ui_scale, main_menu_height);
    DrawInstanceRamHashTableOverlay(io,
                                    fixed_width_font,
                                    ui_scale,
                                    main_menu_height);
    DrawSavedBackbufferOverlay(io, ui_scale, main_menu_height);
}

void NV2ADebugger::DrawDebuggerControls(ImGuiIO& io,
                                        ImFont *fixed_width_font,
                                        float ui_scale,
                                        float main_menu_height)
{
    static constexpr float button_width = 146.0f;
    static constexpr float button_height = 38.0f;
    static constexpr int spacer_height = 10.0f;
    static constexpr int num_buttons = 4;
    static constexpr int num_spacers = 1;
    static constexpr int window_height = button_height * num_buttons +
            spacer_height * num_spacers;

    static int32_t draw_step_size = 1;

    ImVec2 window_pos = ImVec2(5 * ui_scale,
                               main_menu_height);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(
            ImVec2((button_width + 16.0f) * ui_scale,
                   window_height * ui_scale),
            ImGuiCond_Once);
    if (ImGui::Begin("nv2a Debug", &is_open)) {

        ImGui::PushFont(fixed_width_font);

        if (ImGui::Button("Step frame",
                          ImVec2(button_width*ui_scale, 0))) {
            nv2a_dbg_step_frame();
        }
        if (ImGui::Button("Step draw",
                          ImVec2(button_width*ui_scale, 0))) {
            nv2a_dbg_step_begin_end(draw_step_size);
        }
        ImGui::InputInt("Cnt", &draw_step_size, 1, 10);
        if (ImGui::Button("Continue", ImVec2(button_width*ui_scale, 0))) {
            nv2a_dbg_continue();
        }
        draw_step_size &= 0x7FFF;

        ImGui::Dummy(ImVec2(0.0f, spacer_height));
        if (ImGui::Button("Invalidate shaders",
                          ImVec2(button_width*ui_scale, 0))) {
            nv2a_dbg_invalidate_shader_cache();
        }

        ImGui::PopFont();
    }
    ImGui::End();
}


static constexpr const char* vertex_attribute_names[] = {
    "Pos",
    "Weight",
    "Normal",
    "Diffuse",
    "Specular",
    "Fog",
    "PointSize",
    "BackDiffuse",
    "BackSpecular",
    "Tex0",
    "Tex1",
    "Tex2",
    "Tex3"
};

static constexpr const char* vertex_attribute_types[] = {
    "UB_D3D",
    "S1",
    "F",
    "UB_OGL",
    "S32K",
    "CMP"
};

static void ShowVertexAttributeInfo(const NV2ADbgVertexInfo* info) {
    for (auto name : vertex_attribute_names) {
        if (!info->count) {
            ImGui::Text("%s: X", name);
            ImGui::SameLine();
            ImGui::Text("%f, %f, %f, %f",
                        info->inline_value[0],
                        info->inline_value[1],
                        info->inline_value[2],
                        info->inline_value[3]);
        } else {
            ImGui::Text("%s: %d %s",
                        name,
                        info->count,
                        vertex_attribute_types[info->format]);
        }

        ++info;
    }
}

static void ShowTextureConfigInfo(const NV2ADbgTextureConfigInfo* info) {
    for (auto i = 0; i < NV2A_DEBUGGER_NUM_TEXTURES; ++i, ++info) {
        ImGui::Separator();
        if (!(info->control0.bv.ENABLE)) {
            ImGui::Text("%d: Disabled", i);
            continue;
        }

        ImGui::Text("%d:", i);

        switch (info->control0.bv.COLOR_KEY_OP) {
          default:
            break;
          case 1:
            ImGui::Text("ColorKey: Alpha");
            break;
          case 2:
            ImGui::Text("ColorKey: RGBA");
            break;
          case 3:
            ImGui::Text("ColorKey: Kill");
            break;
        }

        if (info->control0.bv.ALPHA_KILL_ENABLE) {
          ImGui::Text("Alpha kill enabled");
        }

        if (info->control0.bv.IMAGE_FIELD_ENABLE) {
          ImGui::Text("Image field enabled");
        }

        ImGui::Text("DMA channel %d", info->format.bv.CONTEXT_DMA);
        ImGui::Text("Format: 0x%X", info->format.bv.COLOR);
        ImGui::Text("MaxAniso: %d", 1 << info->control0.bv.MAX_ANISO);
        ImGui::Text("MaxLOD: %d", info->control0.bv.MAX_LOD_CLAMP);
        ImGui::Text("MinLOD: %d", info->control0.bv.MIN_LOD_CLAMP);
        ImGui::Text("Mipmap Levels: %d", info->format.bv.MIPMAP_LEVELS);

        ImGui::Text("Width: %d", info->image_rect.bv.WIDTH);
        ImGui::Text("Height: %d", info->image_rect.bv.HEIGHT);
        ImGui::Text("Pitch: %d", info->control1.bv.PITCH);
        ImGui::Text("Dimensionality: %d", info->format.bv.DIMENSIONALITY);
        ImGui::Text("Base U: %d", 1 << info->format.bv.BASE_SIZE_U);
        ImGui::Text("Base V: %d", 1 << info->format.bv.BASE_SIZE_V);
        ImGui::Text("Base P: %d", 1 << info->format.bv.BASE_SIZE_P);
        if (info->format.bv.CUBEMAP_ENABLE) {
          ImGui::Text("Cubemap");
        }

        ImGui::Text("LOD bias: %d", info->filter.bv.LOD_BIAS);
        switch (info->filter.bv.CONVOLUTION_KERNEL) {
          default:
            ImGui::Text("Kernel: Unknown %d", info->filter.bv.CONVOLUTION_KERNEL);
            break;
          case 1:
            ImGui::Text("Kernel: Quincunx");
            break;
          case 2:
            ImGui::Text("ColorKey: Gaussian3");
            break;
        }

        switch (info->filter.bv.MIN) {
          default:
            ImGui::Text("Min: Unknown %d", info->filter.bv.MIN);
            break;
          case 1:
            ImGui::Text("Min: BoxLOD0");
            break;
          case 2:
            ImGui::Text("Min: TentLOD0");
            break;
          case 3:
            ImGui::Text("Min: BoxNearestLOD");
            break;
          case 4:
            ImGui::Text("Min: TentNearestLOD");
            break;
          case 5:
            ImGui::Text("Min: BoxTentLOD");
            break;
          case 6:
            ImGui::Text("Min: TentTentLOD");
            break;
          case 7:
            ImGui::Text("Min: Convolution2dLOD0");
            break;
        }

        switch (info->filter.bv.MAG) {
          default:
            ImGui::Text("Mag: Unknown %d", info->filter.bv.MIN);
            break;
          case 1:
            ImGui::Text("Mag: BoxLOD0");
            break;
          case 2:
            ImGui::Text("Mag: TentLOD0");
            break;
          case 3:
            ImGui::Text("Mag: Unknown3");
            break;
          case 4:
            ImGui::Text("Mag: Convolution2dLOD0");
            break;
        }

        if (info->filter.bv.R_SIGNED) {
          ImGui::Text("Signed red");
        }
        if (info->filter.bv.G_SIGNED) {
          ImGui::Text("Signed green");
        }
        if (info->filter.bv.B_SIGNED) {
          ImGui::Text("Signed blue");
        }
        if (info->filter.bv.A_SIGNED) {
          ImGui::Text("Signed alpha");
        }

        auto print_wrap = [](char component, uint32_t mode, bool cyl_wrap) {
          switch (mode) {
            default:
              ImGui::Text("Wrap %c: Unknown %d", component, mode);
              break;
            case 1:
              ImGui::Text("Wrap %c: Repeat", component);
              break;
            case 2:
              ImGui::Text("Wrap %c: Mirror", component);
              break;
            case 3:
              ImGui::Text("Wrap %c: Clamp_Edge", component);
              break;
            case 4:
              ImGui::Text("Wrap %c: Border", component);
              break;
            case 5:
              ImGui::Text("Wrap %c: Clamp_OGL", component);
              break;
          }

          if (cyl_wrap) {
            ImGui::Text("Cylwrap %c", component);
          }
        };
      print_wrap('u', info->address.bv.U, info->address.bv.CYLWRAP_U);
      print_wrap('v', info->address.bv.V, info->address.bv.CYLWRAP_V);
      print_wrap('p', info->address.bv.P, info->address.bv.CYLWRAP_P);
      if (info->address.bv.CYLWRAP_Q) {
        ImGui::Text("Cylwrap q");
      }
      if (info->format.bv.BORDER_SOURCE) {
        ImGui::Text("Border from color");
      }  else {
        ImGui::Text("Border from texture");
      }
      ImGui::Text("Border color: 0x%X", info->border_color);

    }
}

void NV2ADebugger::DrawLastDrawInfoOverlay(ImGuiIO& io,
                                           ImFont *fixed_width_font,
                                           float ui_scale,
                                           float main_menu_height)
{
    ImVec2 window_pos = ImVec2(170 * ui_scale,
                               main_menu_height);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(
            ImVec2(200.0f * ui_scale, main_menu_height * 4.25f),
            ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    if (ImGui::Begin("nv2a last BeginEnd")) {
        ImGui::PushFont(fixed_width_font);
        NV2ADbgState* state = nv2a_dbg_fetch_state();
        const NV2ADbgDrawInfo& info = state->draw_info;

        ImGui::Text("Frame: %u", info.frame_counter);
        ImGui::Text("Subframe: %u", info.draw_ends_since_last_frame);

        if (info.last_draw_operation != NV2A_DRAW_TYPE_INVALID) {
            switch(info.primitive_mode) {
            // TODO: Use ShaderPrimitiveMode directly.
            case 0:
                ImGui::Text("Mode: NONE");
                break;
            case 1:
                ImGui::Text("Mode: POINTS");
                break;
            case 2:
                ImGui::Text("Mode: LINES");
                break;
            case 3:
                ImGui::Text("Mode: LINE_LOOP");
                break;
            case 4:
                ImGui::Text("Mode: LINE_STRIP");
                break;
            case 5:
                ImGui::Text("Mode: TRIANGLES");
                break;
            case 6:
                ImGui::Text("Mode: TRIANGLE_STRIP");
                break;
            case 7:
                ImGui::Text("Mode: TRIANGLE_FAN");
                break;
            case 8:
                ImGui::Text("Mode: QUADS");
                break;
            case 9:
                ImGui::Text("Mode: QUAD_STRIP");
                break;
            case 10:
                ImGui::Text("Mode: POLYGON");
                break;
            default:
                ImGui::Text("Mode: %d", info.primitive_mode);
                break;
            }
        }

        switch (info.last_draw_operation) {
        case NV2A_DRAW_TYPE_DRAW_ARRAYS:
            ImGui::Text("DRAW_ARRAYS: %d indices", info.last_draw_num_items);
            break;
        case NV2A_DRAW_TYPE_INLINE_BUFFERS:
            ImGui::Text("INLINE_BUFFERS: %d indices", info.last_draw_num_items);
            break;
        case NV2A_DRAW_TYPE_INLINE_ARRAYS:
            ImGui::Text("INLINE_ARRAYS: %d indices", info.last_draw_num_items);
            break;
        case NV2A_DRAW_TYPE_INLINE_ELEMENTS:
            ImGui::Text("INLINE_ELEMENTS: %d elements", info.last_draw_num_items);
            break;
        case NV2A_DRAW_TYPE_EMPTY:
            ImGui::Text("EMPTY");
            break;
        case NV2A_DRAW_TYPE_INVALID:
            ImGui::Text("<<Requires step mode>>");
            break;
        }

        if (!info.lighting_enabled) {
            ImGui::Text("Lighting disabled");
        }

        switch (info.depth_buffer_mode) {
            case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
                ImGui::Text(info.depth_buffer_float_mode ? "z16-float" : "z16-fixed");
                break;

            case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
                ImGui::Text(info.depth_buffer_float_mode ? "z24s8-float" : "z24s8-fixed");
                break;

            default:
                ImGui::Text("Invalid depth buffer mode");
                break;
        }

        if (ImGui::TreeNode("Vertex format")) {
            ShowVertexAttributeInfo(info.vertex_attributes);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Texture settings")) {
            ShowTextureConfigInfo(info.texture_config);
            ImGui::TreePop();
        }

        ImGui::PopFont();
    }
    ImGui::End();
}

static void ResizeFBO(struct fbo* fbo_obj, int width, int height)
{
    if (fbo_obj->w == width && fbo_obj->h == height) {
        return;
    }

    fbo_obj->w = width;
    fbo_obj->h = height;

    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_2D, &current_texture);
    glBindTexture(GL_TEXTURE_2D, fbo_obj->tex);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 NULL);
    glBindTexture(GL_TEXTURE_2D, current_texture);
}

static void PrepareFBO(uint32_t slot, const NV2ADbgTextureInfo& info)
{
    if (!g_texture_debugger_fbo[slot]) {
        g_texture_debugger_fbo[slot] = create_fbo(info.width,
                                                  info.height);
        return;
    }

    ResizeFBO(g_texture_debugger_fbo[slot], info.width, info.height);
}

static void RenderTexture(uint32_t slot,
                          const NV2ADbgTextureInfo& info,
                          struct decal_shader* shader,
                          float *clear_color)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    GLint current_bind_texture;
    glGetIntegerv(info.target, &current_bind_texture);
    glBindTexture(info.target, info.texture);

    glViewport(0, 0, info.width, info.height);
    glBindVertexArray(shader->vao);
    glUniform1i(shader->FlipY_loc, shader->flip);
    glUniform4f(shader->ScaleOffset_loc, 1.0f, 1.0f, 0, 0);
    glUniform4f(shader->TexScaleOffset_loc, 1.0, 1.0, 0, 0);
    glUniform1i(shader->tex_loc, slot);

    glClearColor(clear_color[0], clear_color[1], clear_color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);

    glBindTexture(info.target, current_bind_texture);
}

void NV2ADebugger::DrawTextureOverlay(ImGuiIO& io,
                                      ImFont *fixed_width_font,
                                      float ui_scale,
                                      float main_menu_height)
{
    float window_width = 512.0f * ui_scale;
    float window_height = (512.0f + main_menu_height * 2) * ui_scale;
    ImVec2 window_pos = ImVec2(io.DisplaySize.x - window_width,
                               main_menu_height * 2);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
                             ImGuiCond_Once);

    if (ImGui::Begin("nv2a textures", NULL)) {
        ImGui::PushFont(fixed_width_font);

        NV2ADbgState* state = nv2a_dbg_fetch_state();
        NV2ADbgTextureInfo* info = state->textures;

        glUseProgram(shader->prog);
        bool has_textures = false;
        for (int i = 0;
             i < NV2A_DEBUGGER_NUM_TEXTURES && info->width;
             ++i, ++info) {
            has_textures = true;
            PrepareFBO(i, *info);
            ImTextureID id = (ImTextureID)(intptr_t)render_to_fbo(
                g_texture_debugger_fbo[i]);
            ImGui::Image(id, ImVec2(info->width, info->height));
            RenderTexture(i, *info, shader, texture_debugger_clear_color);

            if (ImGui::IsItemHovered())
            {
                render_to_default_fb();
                ImGui::BeginTooltip();
                ImGui::Text("Slot %d", info->slot);
                ImGui::Text("%d x %d", info->width, info->height);
                ImGui::EndTooltip();
            }
        }
        render_to_default_fb();
        glUseProgram(0);

        if (!has_textures) {
            ImGui::Text("No textures");
        }

        ImGui::PopFont();
    }
    ImGui::End();
}

void NV2ADebugger::DrawSavedBackbufferOverlay(ImGuiIO& io,
                                              float ui_scale,
                                              float main_menu_height)
{
    float window_width = 640.0f * ui_scale;
    float window_height = (480.0f + main_menu_height * 1.5f) * ui_scale;
    ImVec2 window_pos = ImVec2(0, main_menu_height * 8);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
                             ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    if (ImGui::Begin("nv2a backbuffer", NULL, ImGuiWindowFlags_HorizontalScrollbar)) {
        StoreBackbuffer(io);

        glUseProgram(shader->prog);
        if (!g_last_stored_backbuffer_fbo) {
            ImGui::Text("No backbuffer available");
        } else {
            ImTextureID id =
                (ImTextureID)(intptr_t)g_last_stored_backbuffer_fbo->tex;
            ImGui::Image(id,
                         ImVec2(g_last_stored_backbuffer_fbo->w ,
                                g_last_stored_backbuffer_fbo->h));
        }
        glUseProgram(0);
    }
    ImGui::End();
}

void NV2ADebugger::DrawInstanceRamHashTableOverlay(ImGuiIO& io,
                                                   ImFont *fixed_width_font,
                                                   float ui_scale,
                                                   float main_menu_height)
{
    float window_width = 470.0f * ui_scale;
    float window_height = 430.0f * ui_scale;
    ImVec2 window_pos = ImVec2(io.DisplaySize.x - window_width,
                               main_menu_height);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
                             ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    if (ImGui::Begin("nv2a instance RAM", NULL)) {
        const uint8_t *instance_ram = g_nv2a_stats.ramin_ptr;

        uint32_t hashtable_offset = nv2a_get_ramht_offset();
        const uint8_t *hashtable_ram = instance_ram + hashtable_offset;

        ImGui::PushFont(fixed_width_font);

        const uint32_t *ramht = (uint32_t*)hashtable_ram;
        uint32_t max_entries = nv2a_get_ramht_size() / 8;

        ImGui::Text("Hash table");

        for (uint32_t i = 0; i < max_entries; ++i) {
            uint32_t channel = *ramht++;
            uint32_t data = *ramht++;

            if (!channel && !data) {
                continue;
            }

            bool is_graphics = (data >> 16) & 0x0F;
            uint32_t context_object_offset = (data & 0xFFFF) << 4;

            char buf[256] = {0};
            snprintf(buf,
                     255,
                     "Channel: %3d Subchannel: %3d IsGR: %s "
                     "InstanceOffset: 0x%05x",
                     channel,
                     (data >> 24) & 0xFF,
                     is_graphics ? "Y" : "N",
                     context_object_offset);
            ImGui::Button(buf);

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                const uint32_t *context_object =
                        (uint32_t*)(instance_ram + context_object_offset);

                if (is_graphics) {
                    // Graphics related context.
                    uint32_t flags = *context_object++;
                    uint32_t flags_3d = *context_object++;

                    uint32_t class_id = flags & 0xFF;
                    ImGui::Text("Class: %02x", class_id);
                    ImGui::Text("Flags3d: 0x%08x", flags_3d);
                } else {
                    // Raw DMA context.
                    uint32_t flags = *context_object++;
                    uint32_t limit = *context_object++;
                    uint32_t addr_1 = *context_object++;
                    uint32_t addr_2 = *context_object++;

                    uint32_t class_id = flags & 0xFF;
                    bool is_agp = (flags & 0x00030000) == 0x00030000;
                    bool is_system = flags & 0x00020000;

                    ImGui::Text("Class: %02x", class_id);
                    ImGui::Text("Flags: 0x%08x", flags);
                    if (is_agp) {
                        ImGui::Text("[AGP Mem]");
                    } else if (is_system) {
                        ImGui::Text("[System Mem]");
                    }

                    ImGui::Text("Limit: 0x%08x", limit);
                    ImGui::Text("Address 1: 0x%08x", addr_1);
                    ImGui::Text("Address 2: 0x%08x", addr_2);
                }
                ImGui::EndTooltip();
            }
        }

        ImGui::PopFont();
    }
    ImGui::End();
}

void NV2ADebugger::StoreBackbuffer(ImGuiIO& io)
{
    NV2ADbgState* state = nv2a_dbg_fetch_state();
    if (!g_last_stored_backbuffer_fbo) {
        g_last_stored_backbuffer_fbo = create_fbo(state->backbuffer_width,
                                                  state->backbuffer_height);
    } else {
        ResizeFBO(g_last_stored_backbuffer_fbo,
                  state->backbuffer_width,
                  state->backbuffer_height);
    }

    render_to_fbo(g_last_stored_backbuffer_fbo);

    GLint backbuffer = state->draw_info.backbuffer_texture;
    if (backbuffer) {
        glUseProgram(shader->prog);

        glActiveTexture(GL_TEXTURE0);
        GLint current_bind_texture;
        glGetIntegerv(GL_TEXTURE_2D, &current_bind_texture);

        glBindTexture(GL_TEXTURE_2D, backbuffer);
        glViewport(0, 0, state->backbuffer_width, state->backbuffer_height);
        glBindVertexArray(shader->vao);
        glUniform1i(shader->FlipY_loc, 1);
        glUniform4f(shader->ScaleOffset_loc, 1.0f, 1.0f, 0, 0);
        glUniform4f(shader->TexScaleOffset_loc, 1.0, 1.0, 0, 0);
        glUniform1i(shader->tex_loc, 0);

        glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);

        glBindTexture(GL_TEXTURE_2D, current_bind_texture);
        glUseProgram(0);
    } else {
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    render_to_default_fb();
}


#endif // ENABLE_NV2A_DEBUGGER
