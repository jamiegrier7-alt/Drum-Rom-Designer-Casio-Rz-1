// Overlay control logic for waveform/parameter visualization and interactive editing.
#include "drumrom/main_ui_overlay_controls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "imgui_internal.h"

namespace drumrom::main_ui_overlay_controls {
namespace {

using drumrom::synth::EnvelopeShape;

float clampf(float value, float min_value, float max_value) {
    return std::clamp(value, min_value, max_value);
}

float ui_rate_from_internal(float internal_rate, float min_rate, float max_rate) {
    const float r = clampf(internal_rate, min_rate, max_rate);
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float internal_rate_from_ui(float ui_rate, float min_rate, float max_rate) {
    const float r = clampf(ui_rate, min_rate, max_rate);
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float ui_attack_from_internal(float internal_rate, float min_rate, float max_rate) {
    const float r = clampf(internal_rate, min_rate, max_rate);
    if (r <= (min_rate + 0.0001f)) {
        return min_rate;
    }
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float internal_attack_from_ui(float ui_rate, float min_rate, float max_rate) {
    const float u = clampf(ui_rate, min_rate, max_rate);
    if (u <= (min_rate + 0.0001f)) {
        return min_rate;
    }
    const float r = (min_rate + max_rate) - u;
    return clampf(r, min_rate + 0.0001f, max_rate);
}

void mark_commit(State* state, bool changed) {
    if (state == nullptr) {
        return;
    }
    if (state->auto_upload_commit_requested != nullptr) {
        *state->auto_upload_commit_requested |= changed || ImGui::IsItemDeactivatedAfterEdit();
    }
    if (state->auto_play_commit_requested != nullptr) {
        *state->auto_play_commit_requested |= ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());
    }
}

void select_overlay(State* state, int overlay_to_select) {
    if (state != nullptr && state->selected_overlay != nullptr) {
        *state->selected_overlay = overlay_to_select;
    }
}

int decimal_places_from_format(const char* format, int fallback) {
    if (format == nullptr) {
        return fallback;
    }
    const char* dot = std::strchr(format, '.');
    if (dot == nullptr) {
        return fallback;
    }
    int places = 0;
    const char* p = dot + 1;
    while (*p >= '0' && *p <= '9') {
        places = (places * 10) + (*p - '0');
        ++p;
    }
    return places;
}

float step_from_format(const char* format, int fallback_decimals) {
    const int decimals = std::clamp(decimal_places_from_format(format, fallback_decimals), 0, 6);
    return std::max(0.000001f, std::pow(10.0f, -static_cast<float>(decimals)));
}

float drag_speed_from_range(float v_min, float v_max, float min_step) {
    const float span = std::max(0.0f, v_max - v_min);
    return std::max(min_step, span / 500.0f);
}

float numbox_width_for_digits(int digits) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float text_w = ImGui::CalcTextSize(std::string(std::max(1, digits), '0').c_str()).x;
    return text_w + st.FramePadding.x * 2.0f + st.ItemInnerSpacing.x;
}

}  // namespace

bool input_int_with_scroll(State* state, const char* label, int* v, int v_min, int v_max) {
    ImGui::SetNextItemWidth(numbox_width_for_digits(10));
    bool changed = ImGui::DragInt(label, v, 1.0f, v_min, v_max, "%d", ImGuiSliderFlags_Vertical);
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.001f) {
            *v += (wheel > 0.0f) ? 1 : -1;
            changed = true;
        }
    }
    if (changed) {
        *v = std::clamp(*v, v_min, v_max);
    }
    mark_commit(state, changed);
    return changed;
}

bool slider_float_with_text_input(State* state,
                                  const char* label,
                                  float* v,
                                  float v_min,
                                  float v_max,
                                  const char* format,
                                  ImGuiSliderFlags flags) {
    (void)flags;
    if (format == nullptr) {
        format = "%.4f";
    }

    const float step = step_from_format(format, 3);
    const float drag_speed = drag_speed_from_range(v_min, v_max, step);
    ImGui::SetNextItemWidth(numbox_width_for_digits(10));
    bool changed = ImGui::DragFloat(label, v, drag_speed, v_min, v_max, format, ImGuiSliderFlags_Vertical);
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.001f) {
            *v += wheel * step;
            changed = true;
        }
    }
    if (changed) {
        *v = clampf(*v, v_min, v_max);
    }
    mark_commit(state, changed);
    return changed;
}

bool slider_rate_with_text_input(State* state,
                                 const char* label,
                                 float* internal_rate,
                                 float min_rate,
                                 float max_rate,
                                 bool attack_mode,
                                 const char* format,
                                 ImGuiSliderFlags flags) {
    (void)flags;
    if (format == nullptr) {
        format = "%.4f";
    }

    const float step = step_from_format(format, 4);
    const float drag_speed = drag_speed_from_range(min_rate, max_rate, step);
    ImGui::SetNextItemWidth(numbox_width_for_digits(10));
    bool changed = ImGui::DragFloat(label, internal_rate, drag_speed, min_rate, max_rate, format, ImGuiSliderFlags_Vertical);
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.001f) {
            *internal_rate += wheel * step;
            changed = true;
        }
    }
    if (changed) {
        *internal_rate = clampf(*internal_rate, min_rate, max_rate);
        if (attack_mode && *internal_rate <= min_rate) {
            *internal_rate = min_rate;
        }
    }
    mark_commit(state, changed);
    return changed;
}

bool colored_slider_float(State* state,
                          const char* label,
                          float* v,
                          float v_min,
                          float v_max,
                          ImVec4 color,
                          int overlay_to_select,
                          const char* format) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_float_with_text_input(state, "##slider", v, v_min, v_max, format);
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool colored_slider_rate(State* state,
                         const char* label,
                         float* internal_rate,
                         float min_rate,
                         float max_rate,
                         ImVec4 color,
                         int overlay_to_select) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_rate_with_text_input(state, "##slider", internal_rate, min_rate, max_rate, false, "%.4f");
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool colored_slider_attack_rate(State* state,
                                const char* label,
                                float* rate,
                                float min_rate,
                                float max_rate,
                                ImVec4 color,
                                int overlay_to_select) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_rate_with_text_input(state, "##slider", rate, min_rate, max_rate, true, "%.4f");
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool colored_slider_float_labeled(State* state,
                                  const char* label,
                                  float* v,
                                  float v_min,
                                  float v_max,
                                  ImVec4 color,
                                  int overlay_to_select) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_float_with_text_input(state, label, v, v_min, v_max, "%.4f");
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    return changed;
}

bool colored_slider_rate_labeled(State* state,
                                 const char* label,
                                 float* internal_rate,
                                 float min_rate,
                                 float max_rate,
                                 ImVec4 color,
                                 int overlay_to_select) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_rate_with_text_input(state, label, internal_rate, min_rate, max_rate, false, "%.4f");
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    return changed;
}

bool colored_slider_attack_rate_labeled(State* state,
                                        const char* label,
                                        float* internal_rate,
                                        float min_rate,
                                        float max_rate,
                                        ImVec4 color,
                                        int overlay_to_select) {
    const ImVec4 bg = ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 1.0f);
    const ImVec4 hov = ImVec4(std::min(1.0f, bg.x + 0.12f), std::min(1.0f, bg.y + 0.12f), std::min(1.0f, bg.z + 0.12f), 1.0f);
    const ImVec4 act = ImVec4(std::min(1.0f, bg.x + 0.18f), std::min(1.0f, bg.y + 0.18f), std::min(1.0f, bg.z + 0.18f), 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, act);
    bool changed = slider_rate_with_text_input(state, label, internal_rate, min_rate, max_rate, true, "%.4f");
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopStyleColor(3);
    return changed;
}

bool shape_combo(const char* label, EnvelopeShape* shape) {
    int idx = static_cast<int>(*shape);
    ImGui::SetNextItemWidth(220.0f);
    if (!ImGui::Combo(label, &idx, "Exponential\0Linear\0Logarithmic\0")) {
        return false;
    }
    *shape = static_cast<EnvelopeShape>(idx);
    return true;
}

float compact_shape_combo_width() {
    const float button_w = 34.0f;
    const float gap = 4.0f;
    return (button_w * 3.0f) + (gap * 2.0f);
}

float shape_row_left_label_anchor_width() {
    float w = 0.0f;
    w = std::max(w, ImGui::CalcTextSize("Pitch Decay").x);
    w = std::max(w, ImGui::CalcTextSize("Tone Decay").x);
    w = std::max(w, ImGui::CalcTextSize("Amp Decay").x);
    w = std::max(w, ImGui::CalcTextSize("Attack Rate").x);
    return w;
}

bool colored_shape_combo(State* state,
                         const char* label,
                         EnvelopeShape* shape,
                         ImVec4 color,
                         int overlay_to_select) {
    ImGui::PushID(label);
    const float gap = 4.0f;
    const ImVec2 button_size(34.0f, ImGui::GetFrameHeight());

    auto curve_value = [](float t, EnvelopeShape s) {
        const float x = clampf(t, 0.0f, 1.0f);
        if (s == EnvelopeShape::Linear) {
            return x;
        }
        if (s == EnvelopeShape::Logarithmic) {
            return std::log(1.0f + (9.0f * x)) / std::log(10.0f);
        }
        return 1.0f - std::exp(-5.0f * x);
    };

    auto curve_button = [&](const char* id, EnvelopeShape option) {
        const bool selected = (*shape == option);
        const ImVec4 base = selected
            ? ImVec4(std::min(1.0f, color.x * 0.65f), std::min(1.0f, color.y * 0.65f), std::min(1.0f, color.z * 0.65f), 1.0f)
            : ImVec4(color.x * 0.22f, color.y * 0.22f, color.z * 0.22f, 1.0f);
        const ImVec4 hov = ImVec4(std::min(1.0f, base.x + 0.10f), std::min(1.0f, base.y + 0.10f), std::min(1.0f, base.z + 0.10f), 1.0f);
        const ImVec4 act = ImVec4(std::min(1.0f, base.x + 0.16f), std::min(1.0f, base.y + 0.16f), std::min(1.0f, base.z + 0.16f), 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
        const bool pressed = ImGui::Button(id, button_size);
        ImGui::PopStyleColor(3);

        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad_x = 5.0f;
        const float pad_y = 4.0f;
        const float left = r.Min.x + pad_x;
        const float right = r.Max.x - pad_x;
        const float top = r.Min.y + pad_y;
        const float bottom = r.Max.y - pad_y;
        const ImU32 line_col = ImGui::GetColorU32(selected ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f) : ImVec4(0.82f, 0.82f, 0.82f, 0.95f));
        const int segs = 16;
        ImVec2 prev(left, bottom - (curve_value(0.0f, option) * (bottom - top)));
        for (int i = 1; i <= segs; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(segs);
            const float x = left + ((right - left) * u);
            const float y = bottom - (curve_value(u, option) * (bottom - top));
            const ImVec2 cur(x, y);
            dl->AddLine(prev, cur, line_col, selected ? 2.0f : 1.6f);
            prev = cur;
        }

        return pressed;
    };

    bool changed = false;
    bool hovered_or_active = false;

    if (curve_button("##shape_exp", EnvelopeShape::Exponential)) {
        *shape = EnvelopeShape::Exponential;
        changed = true;
    }
    hovered_or_active |= ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImGui::SameLine(0.0f, gap);

    if (curve_button("##shape_lin", EnvelopeShape::Linear)) {
        *shape = EnvelopeShape::Linear;
        changed = true;
    }
    hovered_or_active |= ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImGui::SameLine(0.0f, gap);

    if (curve_button("##shape_log", EnvelopeShape::Logarithmic)) {
        *shape = EnvelopeShape::Logarithmic;
        changed = true;
    }
    hovered_or_active |= ImGui::IsItemHovered() || ImGui::IsItemActive();

    if (hovered_or_active) {
        select_overlay(state, overlay_to_select);
    }
    ImGui::PopID();
    return changed;
}

bool colored_rate_with_shape_row(State* state,
                                 const char* rate_label,
                                 float* rate,
                                 float min_rate,
                                 float max_rate,
                                 const char* shape_label,
                                 EnvelopeShape* shape,
                                 ImVec4 color,
                                 int overlay_to_select) {
    const float row_x = ImGui::GetCursorPosX();
    const float row_w = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float slider_w = ImGui::CalcItemWidth();
    (void)rate_label;
    bool changed = colored_slider_rate(state, rate_label, rate, min_rate, max_rate, color, overlay_to_select);
    const float combo_w = compact_shape_combo_width();
    const float max_left_label_w = shape_row_left_label_anchor_width();
    const float label_end_x = row_x + slider_w + st.ItemInnerSpacing.x + max_left_label_w;
    const float right_edge_x = row_x + row_w;
    const float centered_x = label_end_x + ((right_edge_x - label_end_x - combo_w) * 0.5f);
    const float min_x = label_end_x + 6.0f;
    const float max_x = right_edge_x - combo_w;
    const float combo_x = clampf(centered_x, min_x, max_x);
    ImGui::SameLine(combo_x);
    changed |= colored_shape_combo(state, shape_label, shape, color, overlay_to_select);
    return changed;
}

bool colored_attack_with_shape_row(State* state,
                                   const char* attack_label,
                                   float* attack_rate,
                                   float min_rate,
                                   float max_rate,
                                   const char* shape_label,
                                   EnvelopeShape* shape,
                                   ImVec4 color,
                                   int overlay_to_select) {
    const float row_x = ImGui::GetCursorPosX();
    const float row_w = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float slider_w = ImGui::CalcItemWidth();
    (void)attack_label;
    bool changed = colored_slider_attack_rate(state, attack_label, attack_rate, min_rate, max_rate, color, overlay_to_select);
    const float combo_w = compact_shape_combo_width();
    const float max_left_label_w = shape_row_left_label_anchor_width();
    const float label_end_x = row_x + slider_w + st.ItemInnerSpacing.x + max_left_label_w;
    const float right_edge_x = row_x + row_w;
    const float centered_x = label_end_x + ((right_edge_x - label_end_x - combo_w) * 0.5f);
    const float min_x = label_end_x + 6.0f;
    const float max_x = right_edge_x - combo_w;
    const float combo_x = clampf(centered_x, min_x, max_x);
    ImGui::SameLine(combo_x);
    changed |= colored_shape_combo(state, shape_label, shape, color, overlay_to_select);
    return changed;
}

void envelope_group_gap() {
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

bool render_output_controls(State* state,
                            float* output_gain_db,
                            float* limiter_ceiling,
                            int* output_shaper_mode,
                            float* output_saturation) {
    bool changed = false;
    changed |= slider_float_with_text_input(state, "Output Gain dB", output_gain_db, -100.0f, 100.0f, "%.2f");
    changed |= slider_float_with_text_input(state, "Limiter Ceiling", limiter_ceiling, 0.1f, 1.0f, "%.3f");

    ImGui::TextUnformatted("Output Process");
    int radio_mode = *output_shaper_mode;
    if (ImGui::RadioButton("Off", radio_mode == 0)) {
        radio_mode = 0;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Sat", radio_mode == 2)) {
        radio_mode = 2;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Clip", radio_mode == 1)) {
        radio_mode = 1;
        changed = true;
    }
    *output_shaper_mode = radio_mode;

    if (*output_shaper_mode == 2) {
        changed |= slider_float_with_text_input(state, "Sat Drive", output_saturation, 0.0f, 1.0f, "%.3f");
    }
    return changed;
}

}  // namespace drumrom::main_ui_overlay_controls
