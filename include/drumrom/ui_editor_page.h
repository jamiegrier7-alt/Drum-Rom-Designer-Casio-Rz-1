#pragma once

#include "imgui.h"

#include <string>

namespace drumrom::ui_editor_page {

struct Model {
    float ui_scale = 1.0f;
    float waveform_pane_height = 350.0f;
    const std::string* status = nullptr;
    double status_expire_time = 0.0;
    ImVec4 status_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

struct State {
    void* slot_config = nullptr;
    bool* changed = nullptr;
};

struct Actions {
    void (*render_left_pane)(void* slot_config, bool* changed, float scaled_waveform_height, float source_panel_height) = nullptr;
    void (*render_action_pane)(void* slot_config, bool* changed) = nullptr;
    void (*render_bottom_toolbar)() = nullptr;
};

void render(const Model& model, State* state, const Actions& actions);

}  // namespace drumrom::ui_editor_page
