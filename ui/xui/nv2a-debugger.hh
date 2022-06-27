#pragma once

#include "common.hh"

#ifdef ENABLE_NV2A_DEBUGGER
struct ImFont;
struct ImGuiIO;

class NV2ADebugger
{
public:
    bool is_open;

    NV2ADebugger();
    void Draw();

private:
    bool initialized;

    void Initialize();

    void DrawDebuggerControls(ImGuiIO& io,
                              ImFont *fixed_width_font,
                              float ui_scale,
                              float main_menu_height);
    void DrawLastDrawInfoOverlay(ImGuiIO& io,
                                 ImFont *fixed_width_font,
                                 float ui_scale,
                                 float main_menu_height);
    void DrawInstanceRamHashTableOverlay(ImGuiIO& io,
                                         ImFont *fixed_width_font,
                                         float ui_scale,
                                         float main_menu_height);
};

extern NV2ADebugger nv2a_debugger_window;
#endif // ENABLE_NV2A_DEBUGGER
