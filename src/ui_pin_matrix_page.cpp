// Renders the pin matrix page used for pin-bend routing/configuration.
#include "drumrom/ui_pin_matrix_page.h"

namespace drumrom::ui_pin_matrix_page {

void render(Model* model, const Actions& actions) {
    if (model == nullptr || model->pinbend_matrix == nullptr || model->pin_labels == nullptr) {
        return;
    }

    ImGui::SeparatorText("Virtual Pin Bend Matrix");
    ImGui::TextUnformatted("27C256 pin interconnect matrix (0 = open, 1 = connected). Click a cell to toggle.");
    ImGui::TextUnformatted("Diagonal cells are fixed (same pin). Off-diagonal edits are mirrored for symmetry.");

    const float scaled_clear_matrix_width = 140.0f * model->ui_scale;
    if (ImGui::Button("Clear Matrix", ImVec2(scaled_clear_matrix_width, 0.0f))) {
        for (std::size_t y = 0; y < kDefaultEpromPinCount; ++y) {
            for (std::size_t x = 0; x < kDefaultEpromPinCount; ++x) {
                (*model->pinbend_matrix)[y][x] = 0;
            }
        }
        if (actions.set_status != nullptr) {
            actions.set_status("Pin matrix cleared");
        }
    }
    ImGui::SameLine();
    const float scaled_send_matrix_width = 200.0f * model->ui_scale;
    if (ImGui::Button("Send Matrix to OneROM", ImVec2(scaled_send_matrix_width, 0.0f)) && actions.send_pin_matrix_to_onerom_usb != nullptr) {
        (void)actions.send_pin_matrix_to_onerom_usb();
    }

    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX;
    const float first_col_w = 84.0f;
    const float cell_w = 48.0f;

    if (ImGui::BeginTable("PinMatrixTable", static_cast<int>(kDefaultEpromPinCount + 1), tf)) {
        ImGui::TableSetupScrollFreeze(1, 0);
        ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, first_col_w);
        for (std::size_t x = 0; x < kDefaultEpromPinCount; ++x) {
            ImGui::TableSetupColumn((*model->pin_labels)[x], ImGuiTableColumnFlags_WidthFixed, cell_w);
        }
        ImGui::TableHeadersRow();

        for (std::size_t y = 0; y < kDefaultEpromPinCount; ++y) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted((*model->pin_labels)[y]);

            for (std::size_t x = 0; x < kDefaultEpromPinCount; ++x) {
                ImGui::TableSetColumnIndex(static_cast<int>(x + 1));
                ImGui::PushID(static_cast<int>((y * kDefaultEpromPinCount) + x));

                if (x == y) {
                    ImGui::TextUnformatted("-");
                    ImGui::PopID();
                    continue;
                }

                const bool on = ((*model->pinbend_matrix)[y][x] != 0);
                const ImVec4 base = on ? model->color_green_dark : ImVec4(0.25f, 0.10f, 0.10f, 1.0f);
                const ImVec4 hov = on ? model->color_green : ImVec4(0.35f, 0.14f, 0.14f, 1.0f);
                const ImVec4 act = on ? model->color_green_lite : ImVec4(0.45f, 0.18f, 0.18f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);

                const float scaled_toggle_width = 30.0f * model->ui_scale;
                const float scaled_toggle_height = 24.0f * model->ui_scale;
                if (ImGui::Button(on ? "1" : "0", ImVec2(scaled_toggle_width, scaled_toggle_height))) {
                    const std::uint8_t v = on ? 0u : 1u;
                    (*model->pinbend_matrix)[y][x] = v;
                    (*model->pinbend_matrix)[x][y] = v;
                }

                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}

}  // namespace drumrom::ui_pin_matrix_page
