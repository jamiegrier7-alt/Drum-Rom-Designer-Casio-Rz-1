// Renders the main editor page and binds controls to slot parameters.
#include "drumrom/ui_editor_page.h"

#include <algorithm>

namespace drumrom::ui_editor_page {

void render(const Model& model, State* state, const Actions& actions) {
    if (state == nullptr || state->changed == nullptr) {
        return;
    }

    const ImGuiStyle& ui_style = ImGui::GetStyle();
    const float scaled_slot_height = 44.0f * model.ui_scale;
    const float row_spacing = ui_style.ItemSpacing.y * 0.5f;
    const float bottom_toolbar_height =
        (scaled_slot_height * 2.0f) +
        row_spacing +
        (ui_style.WindowPadding.y * 2.0f) +
        2.0f;
    const float status_bar_height = ImGui::GetTextLineHeightWithSpacing();
    const float reserved_bottom_height =
        bottom_toolbar_height +
        status_bar_height +
        (ui_style.ItemSpacing.y * 2.0f);
    const float min_waveform_height = 130.0f * model.ui_scale;
    const float max_waveform_height = 210.0f * model.ui_scale;
    const float scaled_waveform_height = std::clamp(model.waveform_pane_height * model.ui_scale, min_waveform_height, max_waveform_height);
    const float source_button_height = 28.0f;
    const float source_panel_height = source_button_height + (ui_style.WindowPadding.y * 2.0f) + 2.0f;

    ImGui::BeginChild("MainContent", ImVec2(0.0f, -reserved_bottom_height), false);

    if (ImGui::BeginTable("MainColumns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextColumn();
        if (actions.render_left_pane != nullptr) {
            actions.render_left_pane(state->slot_config, state->changed, scaled_waveform_height, source_panel_height);
        }

        ImGui::TableNextColumn();
        if (actions.render_action_pane != nullptr) {
            actions.render_action_pane(state->slot_config, state->changed);
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();

    if (actions.render_bottom_toolbar != nullptr) {
        actions.render_bottom_toolbar();
    }

    ImGui::Separator();
    if (model.status != nullptr && ImGui::GetTime() < model.status_expire_time) {
        ImGui::TextColored(model.status_color, "%s", model.status->c_str());
    }
}

}  // namespace drumrom::ui_editor_page
