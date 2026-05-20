// Waveform generation and drawing utilities for sample/synth preview overlays.
#include "drumrom/main_ui_overlay_waveform.h"

#include <algorithm>
#include <cmath>

namespace {

float clampf(float value, float min_value, float max_value) {
    return std::clamp(value, min_value, max_value);
}

}  // namespace

namespace drumrom::main_ui_overlay_waveform {

void refresh_wave_preview_if_needed(PreviewState* state,
                                    RenderSlotFn render_slot_fn,
                                    IsSampleBasedSlotFn is_sample_based_slot_fn,
                                    ProcessedSampleLengthFn processed_sample_length_fn) {
    if (state == nullptr || state->wave_preview_dirty == nullptr || state->wave_preview_slot == nullptr ||
        state->selected_slot == nullptr || state->wave_preview == nullptr || state->wave_preview_sample_length == nullptr ||
        render_slot_fn == nullptr || is_sample_based_slot_fn == nullptr || processed_sample_length_fn == nullptr) {
        return;
    }

    if (!*state->wave_preview_dirty && *state->wave_preview_slot == *state->selected_slot) {
        return;
    }

    static std::mt19937 rng{0xBEEF1234u};
    const auto rendered = render_slot_fn(*state->selected_slot, rng);
    const int points = 640;
    state->wave_preview->assign(points, 0.0f);
    if (!rendered.empty()) {
        const std::size_t total = rendered.size();
        for (int i = 0; i < points; ++i) {
            const std::size_t idx = static_cast<std::size_t>((static_cast<double>(i) / (points - 1)) * (total - 1));
            (*state->wave_preview)[i] = std::clamp(static_cast<float>(rendered[idx]) / 127.0f, -1.0f, 1.0f);
        }
    }

    if (is_sample_based_slot_fn(*state->selected_slot)) {
        *state->wave_preview_sample_length = processed_sample_length_fn(*state->selected_slot);
    } else {
        *state->wave_preview_sample_length = 0;
    }

    *state->wave_preview_slot = *state->selected_slot;
    *state->wave_preview_dirty = false;
}

bool render_fm_section(drumrom::synth::FmToneParams* fm,
                       const FmSectionStyle& style,
                       const FmSectionActions& actions) {
    if (fm == nullptr || actions.colored_slider_float == nullptr || actions.colored_rate_with_shape_row == nullptr) {
        return false;
    }

    bool changed = false;
    ImGui::SeparatorText("FM");
    ImGui::PushItemWidth(220.0f * style.ui_scale);
    changed |= actions.colored_slider_float("FM Mod Freq Start", &fm->mod_freq_hz, 1.0f, 4000.0f, style.color_fm_pitch, style.overlay_fm_mod_pitch, "%.2f");
    changed |= actions.colored_slider_float("FM Mod Freq End", &fm->mod_freq_end_hz, 1.0f, 4000.0f, style.color_fm_pitch, style.overlay_fm_mod_pitch, "%.2f");
    changed |= actions.colored_rate_with_shape_row("FM Mod Pitch Decay", &fm->mod_pitch_decay_rate, 0.0f, 80.0f, "FM Mod Pitch Shape", &fm->mod_pitch_env_shape, style.color_fm_pitch, style.overlay_fm_mod_pitch);
    if (actions.envelope_group_gap != nullptr) {
        actions.envelope_group_gap();
    }
    changed |= actions.colored_slider_float("FM Index Start", &fm->mod_index, 0.0f, 12.0f, style.color_fm_index, style.overlay_fm_index, "%.2f");
    changed |= actions.colored_slider_float("FM Index End", &fm->mod_index_end, 0.0f, 12.0f, style.color_fm_index, style.overlay_fm_index, "%.2f");
    changed |= actions.colored_rate_with_shape_row("FM Index Decay", &fm->mod_index_decay_rate, 0.0f, 80.0f, "FM Index Shape", &fm->mod_index_env_shape, style.color_fm_index, style.overlay_fm_index);
    if (actions.envelope_group_gap != nullptr) {
        actions.envelope_group_gap();
    }
    changed |= actions.colored_slider_float("AM Freq Start", &fm->amp_osc_hz, 0.0f, 200.0f, style.color_am_pitch, style.overlay_am_pitch, "%.2f");
    changed |= actions.colored_slider_float("AM Freq End", &fm->amp_osc_end_hz, 0.0f, 200.0f, style.color_am_pitch, style.overlay_am_pitch, "%.2f");
    changed |= actions.colored_rate_with_shape_row("AM Pitch Decay", &fm->amp_osc_pitch_decay_rate, 0.0f, 80.0f, "AM Pitch Shape", &fm->amp_osc_pitch_env_shape, style.color_am_pitch, style.overlay_am_pitch);
    if (actions.envelope_group_gap != nullptr) {
        actions.envelope_group_gap();
    }
    changed |= actions.colored_slider_float("AM Depth Start", &fm->amp_osc_depth, 0.0f, 1.0f, style.color_am_depth, style.overlay_am_depth, "%.2f");
    changed |= actions.colored_slider_float("AM Depth End", &fm->amp_osc_depth_end, 0.0f, 1.0f, style.color_am_depth, style.overlay_am_depth, "%.2f");
    changed |= actions.colored_rate_with_shape_row("AM Depth Decay", &fm->amp_osc_depth_decay_rate, 0.0f, 80.0f, "AM Depth Shape", &fm->amp_osc_depth_env_shape, style.color_am_depth, style.overlay_am_depth);
    ImGui::PopItemWidth();
    return changed;
}

float cutoff_to_norm(float cutoff_hz) {
    const float mn = 40.0f;
    const float mx = 12000.0f;
    const float c = clampf(cutoff_hz, mn, mx);
    const float ln_mn = std::log(mn);
    const float ln_mx = std::log(mx);
    const float t = (std::log(c) - ln_mn) / (ln_mx - ln_mn);
    return clampf(t, 0.0f, 1.0f);
}

float norm_to_cutoff(float t) {
    const float mn = 40.0f;
    const float mx = 12000.0f;
    const float ln_mn = std::log(mn);
    const float ln_mx = std::log(mx);
    const float v = std::exp(ln_mn + (clampf(t, 0.0f, 1.0f) * (ln_mx - ln_mn)));
    return clampf(v, mn, mx);
}

void render_overlay_selector(OverlaySelectorState* state) {
    if (state == nullptr || state->selected_overlay == nullptr || state->overlay_drag_point == nullptr) {
        return;
    }

    struct OverlayOption {
        int id;
        const char* label;
        ImVec4 color;
    };

    std::vector<OverlayOption> options;
    options.push_back({state->ids.amp, "Amp", ImVec4(0.92f, 0.41f, 0.35f, 1.0f)});
    options.push_back({state->ids.pitch, "Pitch", ImVec4(0.35f, 0.57f, 1.0f, 1.0f)});
    if (state->sample_based_source) {
        options.push_back({state->ids.tone_or_filter, "Filter", ImVec4(0.27f, 0.80f, 0.47f, 1.0f)});
    } else {
        options.push_back({state->ids.tone_or_filter, "Tone", ImVec4(0.27f, 0.80f, 0.47f, 1.0f)});
        options.push_back({state->ids.fm_mod_pitch, "FM Pitch", ImVec4(0.95f, 0.80f, 0.30f, 1.0f)});
        options.push_back({state->ids.fm_index, "FM Index", ImVec4(0.82f, 0.42f, 0.90f, 1.0f)});
        options.push_back({state->ids.am_pitch, "AM Pitch", ImVec4(0.30f, 0.85f, 0.90f, 1.0f)});
        options.push_back({state->ids.am_depth, "AM Depth", ImVec4(0.95f, 0.60f, 0.25f, 1.0f)});
    }

    bool selected_valid = false;
    for (const auto& o : options) {
        if (o.id == *state->selected_overlay) {
            selected_valid = true;
            break;
        }
    }
    if (!selected_valid && !options.empty()) {
        *state->selected_overlay = options[0].id;
    }

    ImGui::TextUnformatted("Envelope Edit");
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const bool active = (options[i].id == *state->selected_overlay);
        const ImVec4 c = options[i].color;
        const ImVec4 bg = active ? c : ImVec4(c.x * 0.55f, c.y * 0.55f, c.z * 0.55f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(std::min(1.0f, bg.x + 0.15f), std::min(1.0f, bg.y + 0.15f), std::min(1.0f, bg.z + 0.15f), 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(std::min(1.0f, bg.x + 0.08f), std::min(1.0f, bg.y + 0.08f), std::min(1.0f, bg.z + 0.08f), 1.0f));
        if (ImGui::Button(options[i].label)) {
            *state->selected_overlay = options[i].id;
            *state->overlay_drag_point = -1;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Dummy(ImVec2(0, 6.0f * state->ui_scale));
}

std::vector<ImVec2> build_shaped_segment_points(const ImVec2& p0,
                                                const ImVec2& p1,
                                                drumrom::synth::EnvelopeShape shape,
                                                bool is_attack_segment,
                                                int steps) {
    const float dx = p1.x - p0.x;
    if (std::abs(dx) < 0.0001f || steps <= 0) {
        return {p0, p1};
    }

    auto attack_curve = [](float t, drumrom::synth::EnvelopeShape curve_shape) {
        const float x = clampf(t, 0.0f, 1.0f);
        if (curve_shape == drumrom::synth::EnvelopeShape::Linear) {
            return x;
        }
        if (curve_shape == drumrom::synth::EnvelopeShape::Logarithmic) {
            return std::log(1.0f + (9.0f * x)) / std::log(10.0f);
        }
        return 1.0f - std::exp(-5.0f * x);
    };

    auto decay_curve = [](float t, drumrom::synth::EnvelopeShape curve_shape) {
        const float x = clampf(t, 0.0f, 1.0f);
        if (curve_shape == drumrom::synth::EnvelopeShape::Linear) {
            return std::max(0.0f, 1.0f - x);
        }
        if (curve_shape == drumrom::synth::EnvelopeShape::Logarithmic) {
            return 1.0f / (1.0f + (2.0f * x));
        }
        return std::exp(-x);
    };

    auto shaped_progress = [&](float t) {
        const float x = clampf(t, 0.0f, 1.0f);
        if (is_attack_segment) {
            const float y = attack_curve(x, shape);
            const float y_end = std::max(0.0001f, attack_curve(1.0f, shape));
            return clampf(y / y_end, 0.0f, 1.0f);
        }
        const float y = 1.0f - decay_curve(x, shape);
        const float y_end = std::max(0.0001f, 1.0f - decay_curve(1.0f, shape));
        return clampf(y / y_end, 0.0f, 1.0f);
    };

    std::vector<ImVec2> points;
    points.reserve(static_cast<std::size_t>(steps) + 1u);
    points.push_back(p0);
    for (int i = 1; i <= steps; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(steps);
        const float x = p0.x + (dx * u);
        const float p = (x - p0.x) / dx;
        const float c = shaped_progress(p);
        const float y = p0.y + ((p1.y - p0.y) * c);
        points.emplace_back(x, y);
    }
    return points;
}

int pick_handle_at_point(const std::vector<ImVec2>& screen_handles,
                         const ImVec2& mouse,
                         float hit_radius) {
    const float hit_radius_sq = hit_radius * hit_radius;
    int hit = -1;
    float best = 1.0e9f;
    for (int i = 0; i < static_cast<int>(screen_handles.size()); ++i) {
        const float dx = mouse.x - screen_handles[static_cast<std::size_t>(i)].x;
        const float dy = mouse.y - screen_handles[static_cast<std::size_t>(i)].y;
        const float d2 = (dx * dx) + (dy * dy);
        if (d2 < hit_radius_sq && d2 < best) {
            best = d2;
            hit = i;
        }
    }
    return hit;
}

void draw_handles(ImDrawList* draw_list,
                  const std::vector<ImVec2>& screen_handles,
                  float fill_radius,
                  float outline_radius,
                  ImU32 fill_color,
                  ImU32 outline_color,
                  float outline_thickness) {
    if (draw_list == nullptr) {
        return;
    }
    for (const ImVec2& h : screen_handles) {
        draw_list->AddCircleFilled(h, fill_radius, fill_color);
        draw_list->AddCircle(h, outline_radius, outline_color, 0, outline_thickness);
    }
}

bool apply_sample_drag_update(int selected_overlay,
                              int drag_point,
                              float nx,
                              float ny,
                              const OverlaySelectorIds& ids,
                              SampleDragParams* params) {
    if (params == nullptr) {
        return false;
    }

    if (selected_overlay == ids.amp) {
        if (params->amp_attack_s == nullptr || params->amp_decay_s == nullptr ||
            params->amp_sustain == nullptr || params->amp_release_s == nullptr) {
            return false;
        }

        const float amp_total = std::max(0.05f, *params->amp_attack_s + *params->amp_decay_s + *params->amp_release_s + 0.25f);
        if (drag_point == 0) {
            *params->amp_attack_s = clampf(nx * amp_total, 0.0f, 0.3f);
            return true;
        }
        if (drag_point == 1) {
            const float min_x = clampf(*params->amp_attack_s / amp_total, 0.0f, 0.98f);
            const float x = std::max(nx, min_x);
            *params->amp_decay_s = clampf((x * amp_total) - *params->amp_attack_s, 0.0f, 0.6f);
            *params->amp_sustain = clampf(1.0f - ny, 0.0f, 1.0f);
            return true;
        }
        if (drag_point == 2) {
            *params->amp_release_s = clampf((1.0f - nx) * amp_total, 0.0f, 0.6f);
            *params->amp_sustain = clampf(1.0f - ny, 0.0f, 1.0f);
            return true;
        }
        return false;
    }

    if (selected_overlay == ids.tone_or_filter) {
        if (params->filter_cutoff_hz == nullptr || params->filter_cutoff_end_hz == nullptr || params->filter_env_decay_s == nullptr) {
            return false;
        }
        if (drag_point == 0) {
            *params->filter_cutoff_hz = norm_to_cutoff(1.0f - ny);
            return true;
        }
        if (drag_point == 1) {
            *params->filter_cutoff_end_hz = norm_to_cutoff(1.0f - ny);
            *params->filter_env_decay_s = clampf((0.2f * nx) / std::max(0.01f, (1.0f - nx)), 0.01f, 2.0f);
            return true;
        }
        return false;
    }

    if (selected_overlay == ids.pitch) {
        if (params->tune_semitones == nullptr) {
            return false;
        }
        *params->tune_semitones = clampf(((1.0f - ny) * 48.0f) - 24.0f, -24.0f, 24.0f);
        return true;
    }

    return false;
}

bool apply_synth_drag_update(int selected_overlay,
                             int drag_point,
                             float nx,
                             float ny,
                             const OverlaySelectorIds& ids,
                             const SynthDragMathFns& math,
                             SynthDragParams* params) {
    if (params == nullptr) {
        return false;
    }

    if (selected_overlay == ids.amp) {
        if (params->synth_amp_attack != nullptr && drag_point == 0 &&
            math.x_to_rate_attack != nullptr) {
            *params->synth_amp_attack = math.x_to_rate_attack(nx, 0.0f, 80.0f);
        }
        if (params->synth_amp_decay != nullptr && drag_point == 1 &&
            params->synth_amp_attack != nullptr && math.rate_to_x_attack != nullptr &&
            math.x_to_rate != nullptr) {
            const float a = *params->synth_amp_attack;
            const float ax = clampf(math.rate_to_x_attack(a, 0.0f, 80.0f), 0.0f, 0.95f);
            const float x = std::max(nx, ax + 0.01f);
            *params->synth_amp_decay = math.x_to_rate((x - ax), 0.0f, 120.0f);
        }
        return true;
    }

    if (selected_overlay == ids.pitch) {
        if (drag_point == 0 && params->synth_pitch_start != nullptr && math.norm_to_freq_range != nullptr) {
            *params->synth_pitch_start = math.norm_to_freq_range(1.0f - ny, params->synth_pitch_min, params->synth_pitch_max);
        } else if (drag_point == 1) {
            if (params->synth_pitch_decay != nullptr && math.x_to_rate != nullptr) {
                *params->synth_pitch_decay = math.x_to_rate(nx, 0.0f, 80.0f);
            }
            if (params->synth_pitch_end != nullptr && math.norm_to_freq_range != nullptr) {
                *params->synth_pitch_end = math.norm_to_freq_range(1.0f - ny, params->synth_pitch_min, params->synth_pitch_max);
            }
        }
        return true;
    }

    if (selected_overlay == ids.tone_or_filter) {
        if (params->has_tone_freq_filter) {
            if (drag_point == 0 && params->synth_filter_start != nullptr && math.norm_to_freq_range != nullptr) {
                *params->synth_filter_start = math.norm_to_freq_range(1.0f - ny, params->synth_filter_min, params->synth_filter_max);
            } else if (drag_point == 1) {
                if (params->synth_filter_decay != nullptr && math.x_to_rate != nullptr) {
                    *params->synth_filter_decay = math.x_to_rate(nx, 0.0f, 80.0f);
                }
                if (params->synth_filter_end != nullptr && math.norm_to_freq_range != nullptr) {
                    *params->synth_filter_end = math.norm_to_freq_range(1.0f - ny, params->synth_filter_min, params->synth_filter_max);
                }
            }
        } else if (params->synth_filter_decay != nullptr && math.x_to_rate != nullptr) {
            *params->synth_filter_decay = math.x_to_rate(nx, 0.0f, 80.0f);
        }
        return true;
    }

    if (params->fm != nullptr && selected_overlay == ids.fm_mod_pitch) {
        if (drag_point == 0 && math.norm_to_freq_range != nullptr) {
            params->fm->mod_freq_hz = math.norm_to_freq_range(1.0f - ny, 1.0f, 4000.0f);
        } else if (drag_point == 1) {
            if (math.x_to_rate != nullptr) {
                params->fm->mod_pitch_decay_rate = math.x_to_rate(nx, 0.0f, 80.0f);
            }
            if (math.norm_to_freq_range != nullptr) {
                params->fm->mod_freq_end_hz = math.norm_to_freq_range(1.0f - ny, 1.0f, 4000.0f);
            }
        }
        return true;
    }

    if (params->fm != nullptr && selected_overlay == ids.fm_index) {
        if (drag_point == 0) {
            params->fm->mod_index = clampf((1.0f - ny) * 12.0f, 0.0f, 12.0f);
        } else if (drag_point == 1) {
            if (math.x_to_rate != nullptr) {
                params->fm->mod_index_decay_rate = math.x_to_rate(nx, 0.0f, 80.0f);
            }
            params->fm->mod_index_end = clampf((1.0f - ny) * 12.0f, 0.0f, 12.0f);
        }
        return true;
    }

    if (params->fm != nullptr && selected_overlay == ids.am_pitch) {
        if (drag_point == 0) {
            params->fm->amp_osc_hz = clampf((1.0f - ny) * 200.0f, 0.0f, 200.0f);
        } else if (drag_point == 1) {
            if (math.x_to_rate != nullptr) {
                params->fm->amp_osc_pitch_decay_rate = math.x_to_rate(nx, 0.0f, 80.0f);
            }
            params->fm->amp_osc_end_hz = clampf((1.0f - ny) * 200.0f, 0.0f, 200.0f);
        }
        return true;
    }

    if (params->fm != nullptr && selected_overlay == ids.am_depth) {
        if (drag_point == 0) {
            params->fm->amp_osc_depth = clampf(1.0f - ny, 0.0f, 1.0f);
        } else if (drag_point == 1) {
            if (math.x_to_rate != nullptr) {
                params->fm->amp_osc_depth_decay_rate = math.x_to_rate(nx, 0.0f, 80.0f);
            }
            params->fm->amp_osc_depth_end = clampf(1.0f - ny, 0.0f, 1.0f);
        }
        return true;
    }

    return false;
}

bool build_sample_overlay_points(const SampleOverlayPointParams& params,
                                 OverlayCurvePoints* out_points) {
    if (out_points == nullptr) {
        return false;
    }

    const float amp_total = std::max(0.05f, params.amp_attack_s + params.amp_decay_s + params.amp_release_s + 0.25f);
    const float amp_x1 = clampf(params.amp_attack_s / amp_total, 0.0f, 0.95f);
    const float amp_x2 = clampf((params.amp_attack_s + params.amp_decay_s) / amp_total, amp_x1, 0.98f);
    const float amp_x3 = clampf(1.0f - (params.amp_release_s / amp_total), amp_x2, 0.995f);
    const float amp_s = clampf(params.amp_sustain, 0.0f, 1.0f);
    out_points->amp = {
        ImVec2(0.0f, 1.0f),
        ImVec2(amp_x1, 0.0f),
        ImVec2(amp_x2, 1.0f - amp_s),
        ImVec2(amp_x3, 1.0f - amp_s),
        ImVec2(1.0f, 1.0f),
    };

    const float f_x = clampf(params.filter_env_decay_s / (params.filter_env_decay_s + 0.2f), 0.0f, 0.995f);
    const float f_y0 = 1.0f - cutoff_to_norm(params.filter_cutoff_hz);
    const float f_y1 = 1.0f - cutoff_to_norm(params.filter_cutoff_end_hz);
    out_points->filter = {
        ImVec2(0.0f, f_y0),
        ImVec2(f_x, f_y1),
        ImVec2(1.0f, f_y1),
    };

    const float p_tune = clampf((params.tune_semitones + 24.0f) / 48.0f, 0.0f, 1.0f);
    const float p_y = 1.0f - p_tune;
    out_points->pitch = {
        ImVec2(0.0f, p_y),
        ImVec2(0.5f, p_y),
        ImVec2(1.0f, p_y),
    };

    out_points->fm_mod_pitch.clear();
    out_points->fm_index.clear();
    out_points->am_pitch.clear();
    out_points->am_depth.clear();
    return true;
}

bool build_synth_overlay_points(const SynthOverlayPointParams& params,
                                const OverlayPointMathFns& math,
                                OverlayCurvePoints* out_points) {
    if (out_points == nullptr || math.rate_to_x == nullptr ||
        math.rate_to_x_attack == nullptr || math.freq_to_norm_range == nullptr) {
        return false;
    }

    const float a = params.synth_amp_attack ? *params.synth_amp_attack : 0.0f;
    const float d = params.synth_amp_decay ? *params.synth_amp_decay : 0.0f;
    const float ax = clampf(math.rate_to_x_attack(a, 0.0f, 80.0f), 0.0f, 0.95f);
    const float dx = clampf(ax + math.rate_to_x(d, 0.0f, 120.0f), ax + 0.01f, 1.0f);
    out_points->amp = {
        ImVec2(0.0f, 1.0f),
        ImVec2(ax, 0.0f),
        ImVec2(dx, 1.0f),
        ImVec2(1.0f, 1.0f),
    };

    const float pdec = params.synth_pitch_decay ? *params.synth_pitch_decay : 0.0f;
    const float px = math.rate_to_x(pdec, 0.0f, 80.0f);
    const float py0 = 1.0f - math.freq_to_norm_range(params.synth_pitch_start ? *params.synth_pitch_start : params.synth_pitch_min,
                                                     params.synth_pitch_min,
                                                     params.synth_pitch_max);
    const float py1 = 1.0f - math.freq_to_norm_range(params.synth_pitch_end ? *params.synth_pitch_end : params.synth_pitch_min,
                                                     params.synth_pitch_min,
                                                     params.synth_pitch_max);
    out_points->pitch = {
        ImVec2(0.0f, py0),
        ImVec2(px, py1),
        ImVec2(1.0f, py1),
    };

    const float fx = math.rate_to_x(params.synth_filter_decay ? *params.synth_filter_decay : 0.0f, 0.0f, 80.0f);
    if (params.has_tone_freq_filter && params.synth_filter_start && params.synth_filter_end) {
        const float fy0 = 1.0f - math.freq_to_norm_range(*params.synth_filter_start, params.synth_filter_min, params.synth_filter_max);
        const float fy1 = 1.0f - math.freq_to_norm_range(*params.synth_filter_end, params.synth_filter_min, params.synth_filter_max);
        out_points->filter = {
            ImVec2(0.0f, fy0),
            ImVec2(fx, fy1),
            ImVec2(1.0f, fy1),
        };
    } else {
        out_points->filter = {
            ImVec2(0.0f, 0.55f),
            ImVec2(fx, 0.45f),
            ImVec2(1.0f, 0.45f),
        };
    }

    out_points->fm_mod_pitch.clear();
    out_points->fm_index.clear();
    out_points->am_pitch.clear();
    out_points->am_depth.clear();

    if (params.fm != nullptr) {
        const float fmp_x = math.rate_to_x(params.fm->mod_pitch_decay_rate, 0.0f, 80.0f);
        const float fmp_y0 = 1.0f - math.freq_to_norm_range(params.fm->mod_freq_hz, 1.0f, 4000.0f);
        const float fmp_y1 = 1.0f - math.freq_to_norm_range(params.fm->mod_freq_end_hz, 1.0f, 4000.0f);
        out_points->fm_mod_pitch = {ImVec2(0.0f, fmp_y0), ImVec2(fmp_x, fmp_y1), ImVec2(1.0f, fmp_y1)};

        const float fmi_x = math.rate_to_x(params.fm->mod_index_decay_rate, 0.0f, 80.0f);
        const float fmi_y0 = 1.0f - clampf(params.fm->mod_index / 12.0f, 0.0f, 1.0f);
        const float fmi_y1 = 1.0f - clampf(params.fm->mod_index_end / 12.0f, 0.0f, 1.0f);
        out_points->fm_index = {ImVec2(0.0f, fmi_y0), ImVec2(fmi_x, fmi_y1), ImVec2(1.0f, fmi_y1)};

        const float amp_x = math.rate_to_x(params.fm->amp_osc_pitch_decay_rate, 0.0f, 80.0f);
        const float amp_y0 = 1.0f - clampf(params.fm->amp_osc_hz / 200.0f, 0.0f, 1.0f);
        const float amp_y1 = 1.0f - clampf(params.fm->amp_osc_end_hz / 200.0f, 0.0f, 1.0f);
        out_points->am_pitch = {ImVec2(0.0f, amp_y0), ImVec2(amp_x, amp_y1), ImVec2(1.0f, amp_y1)};

        const float amd_x = math.rate_to_x(params.fm->amp_osc_depth_decay_rate, 0.0f, 80.0f);
        const float amd_y0 = 1.0f - clampf(params.fm->amp_osc_depth, 0.0f, 1.0f);
        const float amd_y1 = 1.0f - clampf(params.fm->amp_osc_depth_end, 0.0f, 1.0f);
        out_points->am_depth = {ImVec2(0.0f, amd_y0), ImVec2(amd_x, amd_y1), ImVec2(1.0f, amd_y1)};
    }

    return true;
}

std::vector<ImVec2> build_overlay_handles(int selected_overlay,
                                          bool sample_based_source,
                                          bool has_tone_freq_filter,
                                          const OverlaySelectorIds& ids,
                                          const OverlayCurvePoints& points) {
    if (selected_overlay == ids.amp) {
        if (sample_based_source && points.amp.size() >= 4) {
            return std::vector<ImVec2>{points.amp[1], points.amp[2], points.amp[3]};
        }
        if (!sample_based_source && points.amp.size() >= 3) {
            return std::vector<ImVec2>{points.amp[1], points.amp[2]};
        }
    } else if (selected_overlay == ids.tone_or_filter) {
        if (sample_based_source && points.filter.size() >= 2) {
            return std::vector<ImVec2>{points.filter[0], points.filter[1]};
        }
        if (!sample_based_source && has_tone_freq_filter && points.filter.size() >= 2) {
            return std::vector<ImVec2>{points.filter[0], points.filter[1]};
        }
        if (!sample_based_source && !has_tone_freq_filter && points.filter.size() >= 2) {
            return std::vector<ImVec2>{points.filter[1]};
        }
    } else if (selected_overlay == ids.pitch) {
        if (sample_based_source && points.pitch.size() >= 2) {
            return std::vector<ImVec2>{points.pitch[1]};
        }
        if (!sample_based_source && points.pitch.size() >= 2) {
            return std::vector<ImVec2>{points.pitch[0], points.pitch[1]};
        }
    } else if (selected_overlay == ids.fm_mod_pitch) {
        if (points.fm_mod_pitch.size() >= 2) {
            return std::vector<ImVec2>{points.fm_mod_pitch[0], points.fm_mod_pitch[1]};
        }
    } else if (selected_overlay == ids.fm_index) {
        if (points.fm_index.size() >= 2) {
            return std::vector<ImVec2>{points.fm_index[0], points.fm_index[1]};
        }
    } else if (selected_overlay == ids.am_pitch) {
        if (points.am_pitch.size() >= 2) {
            return std::vector<ImVec2>{points.am_pitch[0], points.am_pitch[1]};
        }
    } else if (selected_overlay == ids.am_depth) {
        if (points.am_depth.size() >= 2) {
            return std::vector<ImVec2>{points.am_depth[0], points.am_depth[1]};
        }
    }

    return {};
}

}  // namespace drumrom::main_ui_overlay_waveform
