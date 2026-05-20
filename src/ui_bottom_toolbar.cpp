// Renders the bottom toolbar for slot selection and quick pad actions.
#include "drumrom/ui_bottom_toolbar.h"

namespace drumrom::ui_bottom_toolbar {

namespace {

bool layout_has_join_12(RamSampleLayout layout) {
    return layout == RamSampleLayout::Join12 || layout == RamSampleLayout::Join12And34;
}

bool layout_has_join_34(RamSampleLayout layout) {
    return layout == RamSampleLayout::Join34 || layout == RamSampleLayout::Join12And34;
}

RamSampleLayout toggle_join_12(RamSampleLayout layout) {
    if (layout == RamSampleLayout::JoinAll) {
        return RamSampleLayout::Join12;
    }
    const bool next_join_12 = !layout_has_join_12(layout);
    const bool next_join_34 = layout_has_join_34(layout);
    if (next_join_12 && next_join_34) {
        return RamSampleLayout::Join12And34;
    }
    if (next_join_12) {
        return RamSampleLayout::Join12;
    }
    if (next_join_34) {
        return RamSampleLayout::Join34;
    }
    return RamSampleLayout::None;
}

RamSampleLayout toggle_join_34(RamSampleLayout layout) {
    if (layout == RamSampleLayout::JoinAll) {
        return RamSampleLayout::Join34;
    }
    const bool next_join_12 = layout_has_join_12(layout);
    const bool next_join_34 = !layout_has_join_34(layout);
    if (next_join_12 && next_join_34) {
        return RamSampleLayout::Join12And34;
    }
    if (next_join_12) {
        return RamSampleLayout::Join12;
    }
    if (next_join_34) {
        return RamSampleLayout::Join34;
    }
    return RamSampleLayout::None;
}

}  // namespace

void render(Model* model, const Actions& actions) {
    if (model == nullptr || model->slots == nullptr) {
        return;
    }

    constexpr float kSlotButtonHeight = 44.0f;
    constexpr std::size_t kPadsPerRow = 8;

    const float scaled_slot_height = kSlotButtonHeight * model->ui_scale;

    ImGui::Separator();

    constexpr ImVec4 kSampleBtnNormal = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
    constexpr ImVec4 kSampleBtnHovered = ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    constexpr ImVec4 kSampleBtnActive = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    constexpr ImVec4 kSampleBtnSelected = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    constexpr ImVec4 kSampleTextColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    constexpr ImVec4 kDrumBtnNormal = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
    constexpr ImVec4 kDrumBtnHovered = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
    constexpr ImVec4 kDrumBtnActive = ImVec4(0.38f, 0.38f, 0.38f, 1.0f);
    constexpr ImVec4 kDrumBtnSelected = ImVec4(0.42f, 0.42f, 0.42f, 1.0f);
    constexpr ImVec4 kDrumTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr float kPadButtonRounding = 5.0f; // Reduced corner radius

    const float row_spacing = ImGui::GetStyle().ItemSpacing.y * 0.5f;
    const float slot_panel_height =
        (scaled_slot_height * 2.0f) +
        row_spacing +
        (ImGui::GetStyle().WindowPadding.y * 2.0f) +
        2.0f;

    if (model->show_slot_selection) {
        const float slot_panel_w = ImGui::GetContentRegionAvail().x * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
        if (ImGui::BeginChild(
                "SlotPanel",
                ImVec2(slot_panel_w, slot_panel_height),
                true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const float control_gap = ImGui::GetStyle().ItemSpacing.x;
            const float control_panel_width = 136.0f * model->ui_scale;
            const float slot_gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
            const float content_width = ImGui::GetContentRegionAvail().x;
            const float slot_area_available_width = std::max(
                0.0f,
                content_width - control_panel_width - control_gap);
            const float slot_row_gaps = slot_gap * static_cast<float>(kPadsPerRow - 1);
            const float slot_width_from_available =
                (slot_area_available_width - slot_row_gaps) / static_cast<float>(kPadsPerRow);
            const float preferred_slot_width = 64.0f * model->ui_scale;
            const float min_slot_width = 48.0f * model->ui_scale;
            const float scaled_slot_width = std::max(
                min_slot_width,
                std::min(preferred_slot_width, slot_width_from_available));
            const float slot_area_width =
                (scaled_slot_width * static_cast<float>(kPadsPerRow)) + slot_row_gaps;

            for (std::size_t i = 0; i < model->slots->size(); ++i) {
                if ((i % kPadsPerRow) != 0) {
                    ImGui::SameLine(0.0f, slot_gap);
                } else if (i > 0) {
                    ImGui::Dummy(ImVec2(0.0f, row_spacing));
                }

                const bool enabled = (*model->slots)[i].is_enabled;
                const bool selected = (i == model->selected_slot) && enabled;
                const bool is_sample_pad = (*model->slots)[i].is_sample_pad;

                ImVec4 bg_normal;
                ImVec4 bg_hovered;
                ImVec4 bg_active;
                ImVec4 bg_selected;
                ImVec4 text_color;
                if (is_sample_pad) {
                    bg_normal = kSampleBtnNormal;
                    bg_hovered = kSampleBtnHovered;
                    bg_active = kSampleBtnActive;
                    bg_selected = kSampleBtnSelected;
                    text_color = kSampleTextColor;
                } else {
                    bg_normal = kDrumBtnNormal;
                    bg_hovered = kDrumBtnHovered;
                    bg_active = kDrumBtnActive;
                    bg_selected = kDrumBtnSelected;
                    text_color = kDrumTextColor;
                }

                const ImVec4 bg = selected ? bg_selected : bg_normal;
                ImGui::PushStyleColor(ImGuiCol_Button, bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_hovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_active);
                ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kPadButtonRounding * model->ui_scale);
                if (!enabled) {
                    ImGui::BeginDisabled();
                }

                if (ImGui::Button((*model->slots)[i].label.c_str(), ImVec2(scaled_slot_width, scaled_slot_height)) && actions.select_slot != nullptr) {
                    // If in JoinAll mode, only allow selecting sample 1 (slot 6)
                    if (model->ram_sample_layout == RamSampleLayout::JoinAll) {
                        actions.select_slot(6, false);
                    } else {
                        actions.select_slot(i, false);
                    }
                }

                if (!enabled) {
                    ImGui::EndDisabled();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }

            const float control_x_max = std::max(
                ImGui::GetStyle().WindowPadding.x,
                content_width - control_panel_width);
            const float control_right_nudge = 12.0f * model->ui_scale;
            const float control_x = std::max(
                ImGui::GetStyle().WindowPadding.x,
                std::min(slot_area_width + control_gap + control_right_nudge, control_x_max));
            const float control_button_gap = ImGui::GetStyle().ItemSpacing.x * 0.5f;
            const float control_button_width = (control_panel_width - control_button_gap) * 0.5f;
            const float control_button_height = ImGui::GetFrameHeight();
            const float row_button_offset = std::max(0.0f, (scaled_slot_height - control_button_height) * 0.5f);

            const auto render_state_button = [&](const char* label, bool active, const ImVec2& pos, auto&& on_press) {
                ImGui::SetCursorPos(pos);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.50f, 0.16f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.09f, 0.40f, 0.13f, 1.0f));
                }
                const bool pressed = ImGui::Button(label, ImVec2(control_button_width, control_button_height));
                if (active) {
                    ImGui::PopStyleColor(3);
                }
                if (pressed) {
                    on_press();
                }
            };

            render_state_button(
                "None",
                model->ram_sample_layout == RamSampleLayout::None,
                ImVec2(control_x, ImGui::GetStyle().WindowPadding.y + row_button_offset),
                [&]() {
                    if (actions.set_ram_sample_layout != nullptr) {
                        actions.set_ram_sample_layout(RamSampleLayout::None);
                    }
                });
            render_state_button(
                "1+2",
                layout_has_join_12(model->ram_sample_layout),
                ImVec2(control_x + control_button_width + control_button_gap,
                       ImGui::GetStyle().WindowPadding.y + row_button_offset),
                [&]() {
                    if (actions.set_ram_sample_layout != nullptr) {
                        actions.set_ram_sample_layout(toggle_join_12(model->ram_sample_layout));
                    }
                });
            render_state_button(
                "3+4",
                layout_has_join_34(model->ram_sample_layout),
                ImVec2(control_x,
                       ImGui::GetStyle().WindowPadding.y + scaled_slot_height + row_spacing + row_button_offset),
                [&]() {
                    if (actions.set_ram_sample_layout != nullptr) {
                        actions.set_ram_sample_layout(toggle_join_34(model->ram_sample_layout));
                    }
                });
            render_state_button(
                "All",
                model->ram_sample_layout == RamSampleLayout::JoinAll,
                ImVec2(control_x + control_button_width + control_button_gap,
                       ImGui::GetStyle().WindowPadding.y + scaled_slot_height + row_spacing + row_button_offset),
                [&]() {
                    if (actions.set_ram_sample_layout != nullptr) {
                        actions.set_ram_sample_layout(RamSampleLayout::JoinAll);
                    }
                });
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

}  // namespace drumrom::ui_bottom_toolbar
