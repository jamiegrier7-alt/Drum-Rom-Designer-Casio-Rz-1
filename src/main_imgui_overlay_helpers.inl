// Overlay and waveform helper routines used by the editor and preview displays.
void refresh_wave_preview_if_needed() {
    if (g_wave_preview_mode == WavePreviewMode::RomOverview) {
        const bool mode_changed = (g_wave_preview_mode_cached != g_wave_preview_mode);
        const bool have_valid_overview =
            !g_wave_preview.empty() &&
            g_rom_overview_waveform_length > 1 &&
            g_rom_overview_boundaries.size() >= 2 &&
            g_rom_overview_slot_indices.size() + 1 == g_rom_overview_boundaries.size();
        const bool need_rebuild = mode_changed || g_wave_preview_dirty || !have_valid_overview;

        if (need_rebuild) {
            std::vector<float> mono;
            g_rom_overview_boundaries.clear();
            g_rom_overview_slot_indices.clear();
            std::size_t total_samples = 0;
            g_rom_overview_boundaries.push_back(0);

            std::mt19937 rng(0x524F4D55u);
            for (std::size_t slot_idx = 0; slot_idx < kSlots.size(); ++slot_idx) {
                if (!is_rom_slot_index(slot_idx)) {
                    continue;
                }

                auto rendered = render_slot(slot_idx, rng);
                const std::size_t slot_capacity = get_slot_capacity(slot_idx);
                if (rendered.size() != slot_capacity) {
                    rendered.assign(slot_capacity, 0);
                }

                for (std::int8_t sample : rendered) {
                    mono.push_back(std::clamp(static_cast<float>(sample) / 127.0f, -1.0f, 1.0f));
                }

                total_samples += rendered.size();
                g_rom_overview_slot_indices.push_back(slot_idx);
                g_rom_overview_boundaries.push_back(total_samples);
            }

            if (!mono.empty() && total_samples > 0) {
                const int points = 640;
                g_wave_preview.assign(points, 0.0f);
                for (int i = 0; i < points; ++i) {
                    const std::size_t idx = static_cast<std::size_t>(
                        (static_cast<double>(i) / (points - 1)) * (mono.size() - 1));
                    g_wave_preview[static_cast<std::size_t>(i)] = std::clamp(mono[idx], -1.0f, 1.0f);
                }
                g_wave_preview_sample_length = mono.size();
                g_rom_overview_waveform_length = total_samples;
            } else {
                g_wave_preview.clear();
                g_wave_preview_sample_length = 0;
                g_rom_overview_waveform_length = 0;
                g_rom_overview_boundaries.clear();
                g_rom_overview_slot_indices.clear();
                g_rom_overview_selected_region = -1;
            }
        }

        if (!g_rom_overview_slot_indices.empty()) {
            int mapped_region = -1;
            for (std::size_t region_idx = 0; region_idx < g_rom_overview_slot_indices.size(); ++region_idx) {
                if (g_rom_overview_slot_indices[region_idx] == g_selected_slot) {
                    mapped_region = static_cast<int>(region_idx);
                    break;
                }
            }
            if (mapped_region >= 0) {
                g_rom_overview_selected_region = mapped_region;
            } else {
                g_rom_overview_selected_region = 0;
            }
        } else {
            g_rom_overview_selected_region = -1;
        }

        g_loop_split_boundaries.clear();
        g_loop_split_waveform_length = 0;
        g_loop_split_slot_indices.clear();
        g_loop_split_selected_region = -1;
        g_loop_split_drag_boundary = -1;
        g_loop_split_cached_path.clear();

        g_wave_preview_slot = g_selected_slot;
        g_wave_preview_mode_cached = g_wave_preview_mode;
        g_wave_preview_dirty = false;
        return;
    }

    g_rom_overview_boundaries.clear();
    g_rom_overview_slot_indices.clear();
    g_rom_overview_waveform_length = 0;
    g_rom_overview_selected_region = -1;

    if (g_selected_slot < g_slot_cfg.size() &&
        g_slot_cfg[g_selected_slot].source == SourceKind::Loop &&
        !g_slot_cfg[g_selected_slot].sample.path.empty()) {
        const std::string& loop_path = g_slot_cfg[g_selected_slot].sample.path;
        const bool split_mode_changed =
            (g_loop_split_cached_target_pads != g_settings.loop_split_target_pads) ||
            (g_loop_split_cached_autofit != g_settings.loop_split_autofit);
        const bool path_changed = (g_loop_split_cached_path != loop_path);
        const bool have_valid_analysis =
            !g_wave_preview.empty() &&
            g_loop_split_waveform_length > 1 &&
            g_loop_split_boundaries.size() >= 2;
        const bool need_reanalyze = path_changed || split_mode_changed || !have_valid_analysis;

        if (need_reanalyze) {
            std::string error_message;
            const std::size_t split_count = (g_settings.loop_split_target_pads == 16) ? 16u : 12u;
            auto analysis = analyze_loop_for_split(loop_path, split_count, &error_message);
            if (analysis.has_value() && !analysis->mono.empty()) {
                const int points = 640;
                g_wave_preview.assign(points, 0.0f);
                for (int i = 0; i < points; ++i) {
                    const std::size_t idx = static_cast<std::size_t>((static_cast<double>(i) / (points - 1)) * (analysis->mono.size() - 1));
                    g_wave_preview[static_cast<std::size_t>(i)] = std::clamp(analysis->mono[idx], -1.0f, 1.0f);
                }
                g_wave_preview_sample_length = analysis->mono.size();
                g_loop_split_boundaries = analysis->boundaries;
                g_loop_split_waveform_length = analysis->mono.size();
                g_loop_split_slot_indices = analysis->assigned_slots;
                g_loop_split_cached_path = loop_path;
                g_loop_split_cached_target_pads = g_settings.loop_split_target_pads;
                g_loop_split_cached_autofit = g_settings.loop_split_autofit;
            } else {
                g_wave_preview.clear();
                g_wave_preview_sample_length = 0;
                g_loop_split_boundaries.clear();
                g_loop_split_waveform_length = 0;
                g_loop_split_slot_indices.clear();
                g_loop_split_selected_region = -1;
                g_loop_split_drag_boundary = -1;
                g_loop_split_cached_path.clear();
            }
        }

        if (!g_loop_split_slot_indices.empty()) {
            int mapped_region = -1;
            for (std::size_t region_idx = 0; region_idx < g_loop_split_slot_indices.size(); ++region_idx) {
                if (g_loop_split_slot_indices[region_idx] == g_selected_slot) {
                    mapped_region = static_cast<int>(region_idx);
                    break;
                }
            }
            if (mapped_region >= 0) {
                g_loop_split_selected_region = mapped_region;
            } else if (g_loop_split_selected_region < 0 ||
                       g_loop_split_selected_region + 1 >= static_cast<int>(g_loop_split_boundaries.size())) {
                g_loop_split_selected_region = 0;
            }
        } else if (g_loop_split_boundaries.size() >= 2) {
            if (g_loop_split_selected_region < 0 ||
                g_loop_split_selected_region + 1 >= static_cast<int>(g_loop_split_boundaries.size())) {
                g_loop_split_selected_region = 0;
            }
        }

        g_wave_preview_slot = g_selected_slot;
        g_wave_preview_mode_cached = g_wave_preview_mode;
        g_wave_preview_dirty = false;
        return;
    }

    g_loop_split_boundaries.clear();
    g_loop_split_waveform_length = 0;
    g_loop_split_slot_indices.clear();
    g_loop_split_selected_region = -1;
    g_loop_split_drag_boundary = -1;
    g_loop_split_cached_path.clear();

    drumrom::main_ui_overlay_waveform::PreviewState state{};
    state.wave_preview_dirty = &g_wave_preview_dirty;
    state.wave_preview_slot = &g_wave_preview_slot;
    state.selected_slot = &g_selected_slot;
    state.wave_preview = &g_wave_preview;
    state.wave_preview_sample_length = &g_wave_preview_sample_length;
    drumrom::main_ui_overlay_waveform::refresh_wave_preview_if_needed(
        &state,
        static_cast<drumrom::main_ui_overlay_waveform::RenderSlotFn>(&render_slot),
        +[](std::size_t slot_index) {
            return slot_index < g_slot_cfg.size() && is_sample_based_source(g_slot_cfg[slot_index].source);
        },
        +[](std::size_t slot_index) {
            if (slot_index >= g_slot_cfg.size()) {
                return static_cast<std::size_t>(0);
            }
            auto processed = process_sample_for_render(g_slot_cfg[slot_index].sample);
            return processed.size();
        });
    g_wave_preview_mode_cached = g_wave_preview_mode;
}

bool render_fm_section(drumrom::synth::FmToneParams& fm) {
    const drumrom::main_ui_overlay_waveform::FmSectionStyle style{
        g_ui_scale,
        kEnvelopeColorFmPitch,
        kEnvelopeColorFmIndex,
        kEnvelopeColorAmPitch,
        kEnvelopeColorAmDepth,
        static_cast<int>(OverlayId::FmModPitch),
        static_cast<int>(OverlayId::FmIndex),
        static_cast<int>(OverlayId::AmPitch),
        static_cast<int>(OverlayId::AmDepth),
    };

    const drumrom::main_ui_overlay_waveform::FmSectionActions actions{
        [](const char* label,
           float* v,
           float v_min,
           float v_max,
           ImVec4 color,
           int overlay_to_select,
           const char* format) {
            return colored_slider_float(label, v, v_min, v_max, color, static_cast<OverlayId>(overlay_to_select), format);
        },
        [](const char* rate_label,
           float* rate,
           float min_rate,
           float max_rate,
           const char* shape_label,
           drumrom::synth::EnvelopeShape* shape,
           ImVec4 color,
           int overlay_to_select) {
            return colored_rate_with_shape_row(rate_label,
                                               rate,
                                               min_rate,
                                               max_rate,
                                               shape_label,
                                               shape,
                                               color,
                                               static_cast<OverlayId>(overlay_to_select));
        },
        &envelope_group_gap,
    };

    return drumrom::main_ui_overlay_waveform::render_fm_section(&fm, style, actions);
}

float cutoff_to_norm(float cutoff_hz) {
    return drumrom::main_ui_overlay_waveform::cutoff_to_norm(cutoff_hz);
}

float norm_to_cutoff(float t) {
    return drumrom::main_ui_overlay_waveform::norm_to_cutoff(t);
}

void draw_waveform_overlay_editor(SlotConfig& cfg, bool* changed) {
    // In loop edit mode, render read-only envelopes only (no editing UI)
    const bool in_loop_edit_mode = (cfg.source == SourceKind::Loop);
    
    if (!in_loop_edit_mode) {
        drumrom::main_ui_overlay_waveform::OverlaySelectorState overlay_selector_state{};
        overlay_selector_state.sample_based_source = is_sample_based_source(cfg.source);
        overlay_selector_state.selected_overlay = reinterpret_cast<int*>(&g_overlay_selected);
        overlay_selector_state.overlay_drag_point = &g_overlay_drag_point;
        overlay_selector_state.ui_scale = g_ui_scale;
        overlay_selector_state.ids.amp = static_cast<int>(OverlayId::Amp);
        overlay_selector_state.ids.pitch = static_cast<int>(OverlayId::Pitch);
        overlay_selector_state.ids.tone_or_filter = static_cast<int>(OverlayId::ToneOrFilter);
        overlay_selector_state.ids.fm_mod_pitch = static_cast<int>(OverlayId::FmModPitch);
        overlay_selector_state.ids.fm_index = static_cast<int>(OverlayId::FmIndex);
        overlay_selector_state.ids.am_pitch = static_cast<int>(OverlayId::AmPitch);
        overlay_selector_state.ids.am_depth = static_cast<int>(OverlayId::AmDepth);
        drumrom::main_ui_overlay_waveform::render_overlay_selector(&overlay_selector_state);
    }

    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##WaveOverlayCanvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const ImVec2 pmin = ImGui::GetItemRectMin();
    const ImVec2 pmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pmin, pmax, IM_COL32(15, 18, 28, 255));
    dl->AddRect(pmin, pmax, IM_COL32(45, 55, 70, 255));

    const auto to_screen = [&](float x, float y) {
        const float sx = pmin.x + (x * (pmax.x - pmin.x));
        const float sy = pmin.y + (y * (pmax.y - pmin.y));
        return ImVec2(sx, sy);
    };
    const bool show_rom_overview = (g_wave_preview_mode == WavePreviewMode::RomOverview);

    // Draw waveform
    if (!g_wave_preview.empty()) {
        const float mid_y = pmin.y + ((pmax.y - pmin.y) * 0.5f);
        dl->AddLine(ImVec2(pmin.x, mid_y), ImVec2(pmax.x, mid_y), IM_COL32(50, 65, 85, 255), 1.0f);
        for (std::size_t i = 1; i < g_wave_preview.size(); ++i) {
            const float x0 = static_cast<float>(i - 1) / static_cast<float>(g_wave_preview.size() - 1);
            const float x1 = static_cast<float>(i) / static_cast<float>(g_wave_preview.size() - 1);
            const float y0 = 0.5f - (0.45f * g_wave_preview[i - 1]);
            const float y1 = 0.5f - (0.45f * g_wave_preview[i]);
            dl->AddLine(to_screen(x0, y0), to_screen(x1, y1), IM_COL32(80, 180, 190, 180), 1.0f);
        }

        if (show_rom_overview &&
            g_rom_overview_waveform_length > 1 &&
            g_rom_overview_boundaries.size() >= 2 &&
            g_rom_overview_slot_indices.size() + 1 == g_rom_overview_boundaries.size()) {
            const float denom = static_cast<float>(g_rom_overview_waveform_length - 1);
            if (g_rom_overview_selected_region >= 0 &&
                g_rom_overview_selected_region + 1 < static_cast<int>(g_rom_overview_boundaries.size())) {
                const std::size_t region_idx = static_cast<std::size_t>(g_rom_overview_selected_region);
                const std::size_t start_idx = std::min(g_rom_overview_boundaries[region_idx], g_rom_overview_waveform_length - 1);
                const std::size_t end_exclusive = g_rom_overview_boundaries[region_idx + 1];
                const std::size_t end_idx = std::min(
                    end_exclusive > 0 ? end_exclusive - 1 : start_idx,
                    g_rom_overview_waveform_length - 1);
                const float start_x = pmin.x + ((static_cast<float>(start_idx) / denom) * (pmax.x - pmin.x));
                const float end_x = pmin.x + ((static_cast<float>(end_idx) / denom) * (pmax.x - pmin.x));
                dl->AddRectFilled(ImVec2(start_x, pmin.y), ImVec2(end_x, pmax.y), IM_COL32(80, 110, 150, 45));
                dl->AddRect(ImVec2(start_x, pmin.y), ImVec2(end_x, pmax.y), IM_COL32(125, 185, 235, 225), 0.0f, 0, 2.0f);
            }

            for (std::size_t i = 1; i + 1 < g_rom_overview_boundaries.size(); ++i) {
                const float x = pmin.x + ((static_cast<float>(g_rom_overview_boundaries[i]) / denom) * (pmax.x - pmin.x));
                dl->AddLine(ImVec2(x, pmin.y), ImVec2(x, pmax.y), IM_COL32(120, 180, 230, 185), 1.0f);
            }

            dl->AddText(ImVec2(pmin.x + 8.0f, pmin.y + 8.0f), IM_COL32(190, 220, 255, 230), "ROM overview: click a region to select slot");
        } else if (cfg.source == SourceKind::Loop &&
            g_loop_split_waveform_length > 1 &&
            g_loop_split_boundaries.size() >= 2) {
            const float denom = static_cast<float>(g_loop_split_waveform_length - 1);
            std::size_t start_idx = static_cast<std::size_t>(std::clamp(cfg.sample.start_pct, 0, 99)) * (g_loop_split_waveform_length - 1) / 100u;
            std::size_t end_idx = static_cast<std::size_t>(std::clamp(cfg.sample.end_pct, 1, 100)) * (g_loop_split_waveform_length - 1) / 100u;
            if (g_loop_split_selected_region >= 0 &&
                g_loop_split_selected_region + 1 < static_cast<int>(g_loop_split_boundaries.size())) {
                const std::size_t region_idx = static_cast<std::size_t>(g_loop_split_selected_region);
                start_idx = std::min(g_loop_split_boundaries[region_idx], g_loop_split_waveform_length - 1);
                end_idx = std::min(g_loop_split_boundaries[region_idx + 1], g_loop_split_waveform_length - 1);
            } else if (!g_loop_split_slot_indices.empty()) {
                for (std::size_t region_idx = 0; region_idx < g_loop_split_slot_indices.size(); ++region_idx) {
                    if (g_loop_split_slot_indices[region_idx] == g_selected_slot) {
                        start_idx = std::min(g_loop_split_boundaries[region_idx], g_loop_split_waveform_length - 1);
                        end_idx = std::min(g_loop_split_boundaries[region_idx + 1], g_loop_split_waveform_length - 1);
                        break;
                    }
                }
            }
            const float start_x = pmin.x + ((static_cast<float>(start_idx) / denom) * (pmax.x - pmin.x));
            const float end_x = pmin.x + ((static_cast<float>(end_idx) / denom) * (pmax.x - pmin.x));
            dl->AddRectFilled(ImVec2(start_x, pmin.y), ImVec2(end_x, pmax.y), IM_COL32(70, 120, 70, 35));
            dl->AddRect(ImVec2(start_x, pmin.y), ImVec2(end_x, pmax.y), IM_COL32(90, 200, 120, 200), 0.0f, 0, 2.0f);

            for (std::size_t i = 1; i + 1 < g_loop_split_boundaries.size(); ++i) {
                const float x = pmin.x + ((static_cast<float>(g_loop_split_boundaries[i]) / denom) * (pmax.x - pmin.x));
                dl->AddLine(ImVec2(x, pmin.y), ImVec2(x, pmax.y), IM_COL32(255, 210, 90, 180), 1.0f);
            }

            const float handle_y = pmax.y - 10.0f;
            for (std::size_t i = 1; i + 1 < g_loop_split_boundaries.size(); ++i) {
                const float x = pmin.x + ((static_cast<float>(g_loop_split_boundaries[i]) / denom) * (pmax.x - pmin.x));
                const bool active = (static_cast<int>(i) == g_loop_split_drag_boundary);
                dl->AddCircleFilled(
                    ImVec2(x, handle_y),
                    active ? 7.0f : 6.0f,
                    active ? IM_COL32(255, 240, 130, 240) : IM_COL32(255, 210, 90, 220));
                dl->AddCircle(ImVec2(x, handle_y), active ? 7.0f : 6.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);
            }

            dl->AddText(
                ImVec2(pmin.x + 8.0f, pmin.y + 8.0f),
                IM_COL32(210, 235, 195, 230),
                "Loop regions: click region to select mapped slot");
            
            // Draw all slot envelopes in loop edit mode (read-only display)
            {
                const float denom = static_cast<float>(g_loop_split_waveform_length - 1);
                const int display_points = 640;
                
                // Envelope colors: red for amplitude, green for filter
                constexpr ImU32 kAmpEnvelopeColor = IM_COL32(255, 100, 100, 140);      // Red for amp
                constexpr ImU32 kFilterEnvelopeColor = IM_COL32(100, 200, 100, 140);   // Green for filter
                
                // Draw envelope for each slot in its ROM region
                for (std::size_t region_idx = 0; region_idx < g_loop_split_slot_indices.size(); ++region_idx) {
                    const std::size_t slot_idx = g_loop_split_slot_indices[region_idx];
                    if (slot_idx >= kSlots.size()) continue;
                    
                    const std::size_t region_start = g_loop_split_boundaries[region_idx];
                    const std::size_t region_end = g_loop_split_boundaries[region_idx + 1];
                    if (region_end <= region_start) continue;
                    
                    const SlotConfig& slot_cfg = g_slot_cfg[slot_idx];
                    if (!is_sample_based_source(slot_cfg.source)) continue;
                    
                    const std::size_t region_size = region_end - region_start;
                    const float region_start_norm = static_cast<float>(region_start) / denom;
                    const float region_end_norm = static_cast<float>(region_end) / denom;
                    
                    // Draw amplitude envelope in red
                    {
                        const auto amp_envelope = generate_adsr_envelope(
                            region_size,
                            slot_cfg.sample.amp_attack_s,
                            slot_cfg.sample.amp_decay_s,
                            slot_cfg.sample.amp_sustain,
                            slot_cfg.sample.amp_release_s
                        );
                        
                        if (!amp_envelope.empty()) {
                            for (int i = 1; i < display_points; ++i) {
                                const std::size_t region_pos0 = static_cast<std::size_t>(
                                    (static_cast<float>(i - 1) / (display_points - 1)) * static_cast<float>(region_size)
                                );
                                const std::size_t region_pos1 = static_cast<std::size_t>(
                                    (static_cast<float>(i) / (display_points - 1)) * static_cast<float>(region_size)
                                );
                                
                                const std::size_t env_idx0 = std::min(region_pos0, amp_envelope.size() - 1);
                                const std::size_t env_idx1 = std::min(region_pos1, amp_envelope.size() - 1);
                                
                                const float env_val0 = clampf(amp_envelope[env_idx0], 0.0f, 1.0f);
                                const float env_val1 = clampf(amp_envelope[env_idx1], 0.0f, 1.0f);
                                
                                const float x0 = region_start_norm + (static_cast<float>(i - 1) / (display_points - 1)) * (region_end_norm - region_start_norm);
                                const float x1 = region_start_norm + (static_cast<float>(i) / (display_points - 1)) * (region_end_norm - region_start_norm);
                                
                                const float y0 = 0.5f - (0.45f * (env_val0 * 2.0f - 1.0f));
                                const float y1 = 0.5f - (0.45f * (env_val1 * 2.0f - 1.0f));
                                
                                dl->AddLine(to_screen(x0, y0), to_screen(x1, y1), kAmpEnvelopeColor, 2.0f);
                            }
                        }
                    }
                    
                    // Draw filter envelope in green
                    {
                        // Filter envelope: starts at full (1.0), decays to 0 over filter_env_decay_s
                        const auto filter_envelope = generate_adsr_envelope(
                            region_size,
                            0.0f,   // No attack
                            slot_cfg.sample.filter_env_decay_s,
                            0.0f,   // No sustain (filtered out)
                            0.01f   // Quick release
                        );
                        
                        if (!filter_envelope.empty()) {
                            for (int i = 1; i < display_points; ++i) {
                                const std::size_t region_pos0 = static_cast<std::size_t>(
                                    (static_cast<float>(i - 1) / (display_points - 1)) * static_cast<float>(region_size)
                                );
                                const std::size_t region_pos1 = static_cast<std::size_t>(
                                    (static_cast<float>(i) / (display_points - 1)) * static_cast<float>(region_size)
                                );
                                
                                const std::size_t env_idx0 = std::min(region_pos0, filter_envelope.size() - 1);
                                const std::size_t env_idx1 = std::min(region_pos1, filter_envelope.size() - 1);
                                
                                const float env_val0 = clampf(filter_envelope[env_idx0], 0.0f, 1.0f);
                                const float env_val1 = clampf(filter_envelope[env_idx1], 0.0f, 1.0f);
                                
                                const float x0 = region_start_norm + (static_cast<float>(i - 1) / (display_points - 1)) * (region_end_norm - region_start_norm);
                                const float x1 = region_start_norm + (static_cast<float>(i) / (display_points - 1)) * (region_end_norm - region_start_norm);
                                
                                const float y0 = 0.5f - (0.45f * (env_val0 * 2.0f - 1.0f));
                                const float y1 = 0.5f - (0.45f * (env_val1 * 2.0f - 1.0f));
                                
                                dl->AddLine(to_screen(x0, y0), to_screen(x1, y1), kFilterEnvelopeColor, 2.0f);
                            }
                        }
                    }
                }
            }
        }
        
        // Draw amp envelope overlay (only for sample mode with envelope enabled)
        if (!show_rom_overview && is_sample_based_source(cfg.source) && cfg.sample.amp_envelope_mode != AmpEnvelopeMode::Off) {
            // Use actual sample length for Pre-Fit, slot size for Output
            const std::size_t envelope_length = cfg.sample.amp_envelope_mode == AmpEnvelopeMode::PreFit 
                ? std::max(static_cast<std::size_t>(1), g_wave_preview_sample_length)
                : get_slot_capacity(g_selected_slot);
            
            if (envelope_length > 0) {
                auto envelope = generate_adsr_envelope(envelope_length, cfg.sample.amp_attack_s, cfg.sample.amp_decay_s, cfg.sample.amp_sustain, cfg.sample.amp_release_s);
                
                if (!envelope.empty()) {
                    const ImU32 env_color = cfg.sample.amp_envelope_mode == AmpEnvelopeMode::PreFit 
                        ? IM_COL32(255, 150, 100, 120)  // Orange for Pre-Fit
                        : IM_COL32(150, 255, 100, 120);  // Green for Output
                    
                    const int display_points = 640;
                    
                    if (cfg.sample.amp_envelope_mode == AmpEnvelopeMode::PreFit) {
                        // Pre-Fit with looping: show envelope repeating across loop iterations
                        const int s_pct = std::clamp(cfg.sample.loop_start_pct, 0, 99);
                        const int e_pct = std::clamp(cfg.sample.loop_end_pct, s_pct + 1, 100);
                        const std::size_t loop_start = (g_wave_preview_sample_length * static_cast<std::size_t>(s_pct)) / 100;
                        const std::size_t loop_end = (g_wave_preview_sample_length * static_cast<std::size_t>(e_pct)) / 100;
                        const std::size_t loop_window = std::max<std::size_t>(1, loop_end > loop_start ? (loop_end - loop_start) : 1);
                        
                        // Draw envelope repeating across loop iterations to fill slot
                        const std::size_t slot_size = get_slot_capacity(g_selected_slot);
                        const std::size_t num_iterations = (slot_size + loop_window - 1) / loop_window;
                        
                        for (int i = 1; i < display_points; ++i) {
                            // Map display position to slot position
                            const float slot_pos = (static_cast<float>(i) / (display_points - 1)) * static_cast<float>(slot_size);
                            const float prev_slot_pos = (static_cast<float>(i - 1) / (display_points - 1)) * static_cast<float>(slot_size);
                            
                            // Which iteration are we in?
                            const std::size_t iter_idx = static_cast<std::size_t>(slot_pos / static_cast<float>(loop_window));
                            const std::size_t prev_iter_idx = static_cast<std::size_t>(prev_slot_pos / static_cast<float>(loop_window));
                            
                            // Position within the current loop iteration
                            const float pos_in_iter = std::fmod(slot_pos, static_cast<float>(loop_window));
                            const float prev_pos_in_iter = std::fmod(prev_slot_pos, static_cast<float>(loop_window));
                            
                            // Map to envelope index
                            const std::size_t env_idx = static_cast<std::size_t>(
                                (pos_in_iter / static_cast<float>(loop_window)) * static_cast<float>(envelope_length - 1)
                            );
                            const std::size_t prev_env_idx = static_cast<std::size_t>(
                                (prev_pos_in_iter / static_cast<float>(loop_window)) * static_cast<float>(envelope_length - 1)
                            );
                            
                            const float env0 = clampf(envelope[prev_env_idx], 0.0f, 1.0f);
                            const float env1 = clampf(envelope[env_idx], 0.0f, 1.0f);
                            
                            const float x0 = static_cast<float>(i - 1) / (display_points - 1);
                            const float x1 = static_cast<float>(i) / (display_points - 1);
                            const float y0 = 0.5f - (0.45f * (env0 * 2.0f - 1.0f));
                            const float y1 = 0.5f - (0.45f * (env1 * 2.0f - 1.0f));
                            
                            dl->AddLine(to_screen(x0, y0), to_screen(x1, y1), env_color, 2.0f);
                        }
                    } else {
                        // Output mode: envelope applied after fit, no looping effect
                        for (int i = 1; i < display_points; ++i) {
                            const std::size_t idx0 = static_cast<std::size_t>((static_cast<double>(i - 1) / (display_points - 1)) * (envelope_length - 1));
                            const std::size_t idx1 = static_cast<std::size_t>((static_cast<double>(i) / (display_points - 1)) * (envelope_length - 1));
                            
                            const float x0 = static_cast<float>(i - 1) / (display_points - 1);
                            const float x1 = static_cast<float>(i) / (display_points - 1);
                            const float env0 = clampf(envelope[idx0], 0.0f, 1.0f);
                            const float env1 = clampf(envelope[idx1], 0.0f, 1.0f);
                            
                            const float y0 = 0.5f - (0.45f * (env0 * 2.0f - 1.0f));
                            const float y1 = 0.5f - (0.45f * (env1 * 2.0f - 1.0f));
                            
                            dl->AddLine(to_screen(x0, y0), to_screen(x1, y1), env_color, 2.0f);
                        }
                    }
                    
                    // Add label to show which mode is active
                    ImVec2 label_pos = ImVec2(pmin.x + 5.0f, pmin.y + 5.0f);
                    const char* mode_label = cfg.sample.amp_envelope_mode == AmpEnvelopeMode::PreFit ? "Pre-Fit Env (looped)" : "Output Env";
                    dl->AddText(label_pos, env_color, mode_label);
                }
            }
        }
    }

    if (show_rom_overview) {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            g_rom_overview_waveform_length > 1 &&
            g_rom_overview_boundaries.size() >= 2 &&
            g_rom_overview_slot_indices.size() + 1 == g_rom_overview_boundaries.size()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float nx = clampf((mouse.x - pmin.x) / std::max(1.0f, (pmax.x - pmin.x)), 0.0f, 1.0f);
            const std::size_t max_index = std::max<std::size_t>(1, g_rom_overview_waveform_length - 1);
            const std::size_t sample_index = static_cast<std::size_t>(nx * static_cast<float>(max_index));
            for (std::size_t i = 0; i < g_rom_overview_slot_indices.size(); ++i) {
                const std::size_t start = g_rom_overview_boundaries[i];
                const std::size_t end_exclusive = g_rom_overview_boundaries[i + 1];
                if (sample_index < start || sample_index >= end_exclusive) {
                    continue;
                }

                g_rom_overview_selected_region = static_cast<int>(i);
                const std::size_t slot_idx = g_rom_overview_slot_indices[i];
                if (slot_idx < kSlots.size()) {
                    select_slot(slot_idx, false);
                }
                break;
            }
        }
        return;
    }

    auto freq_to_norm_range = [](float f, float mn, float mx) {
        const float ff = clampf(f, mn, mx);
        const float ln_mn = std::log(mn);
        const float ln_mx = std::log(mx);
        return clampf((std::log(ff) - ln_mn) / (ln_mx - ln_mn), 0.0f, 1.0f);
    };
    auto norm_to_freq_range = [](float t, float mn, float mx) {
        const float ln_mn = std::log(mn);
        const float ln_mx = std::log(mx);
        return clampf(std::exp(ln_mn + (clampf(t, 0.0f, 1.0f) * (ln_mx - ln_mn))), mn, mx);
    };
    auto rate_to_x = [](float internal_rate, float min_rate, float max_rate) {
        const float ui = ui_rate_from_internal(internal_rate, min_rate, max_rate);
        return clampf((ui - min_rate) / std::max(0.0001f, max_rate - min_rate), 0.0f, 1.0f);
    };
    auto x_to_rate = [](float x, float min_rate, float max_rate) {
        const float ui = min_rate + (clampf(x, 0.0f, 1.0f) * (max_rate - min_rate));
        return internal_rate_from_ui(ui, min_rate, max_rate);
    };
    auto rate_to_x_linear = [](float internal_rate, float min_rate, float max_rate) {
        return clampf((internal_rate - min_rate) / std::max(0.0001f, max_rate - min_rate), 0.0f, 1.0f);
    };
    auto x_to_rate_linear = [](float x, float min_rate, float max_rate) {
        return min_rate + (clampf(x, 0.0f, 1.0f) * (max_rate - min_rate));
    };
    auto rate_to_x_attack = [](float internal_rate, float min_rate, float max_rate) {
        const float ui = ui_attack_from_internal(internal_rate, min_rate, max_rate);
        return clampf((ui - min_rate) / std::max(0.0001f, max_rate - min_rate), 0.0f, 1.0f);
    };
    auto x_to_rate_attack = [](float x, float min_rate, float max_rate) {
        const float ui = min_rate + (clampf(x, 0.0f, 1.0f) * (max_rate - min_rate));
        return internal_attack_from_ui(ui, min_rate, max_rate);
    };

    drumrom::main_ui_overlay_waveform::OverlayCurvePoints curve_points{};
    std::vector<ImVec2>& amp_pts = curve_points.amp;
    std::vector<ImVec2>& filter_pts = curve_points.filter;
    std::vector<ImVec2>& pitch_pts = curve_points.pitch;
    std::vector<ImVec2>& fm_mod_pitch_pts = curve_points.fm_mod_pitch;
    std::vector<ImVec2>& fm_index_pts = curve_points.fm_index;
    std::vector<ImVec2>& am_pitch_pts = curve_points.am_pitch;
    std::vector<ImVec2>& am_depth_pts = curve_points.am_depth;
    std::vector<ImVec2> handles;

    bool has_tone_freq_filter = false;
    float* synth_amp_attack = nullptr;
    float* synth_amp_decay = nullptr;
    float* synth_pitch_decay = nullptr;
    float* synth_pitch_start = nullptr;
    float* synth_pitch_end = nullptr;
    float* synth_filter_decay = nullptr;
    float* synth_filter_start = nullptr;
    float* synth_filter_end = nullptr;
    float synth_pitch_min = 20.0f;
    float synth_pitch_max = 4000.0f;
    float synth_filter_min = 20.0f;
    float synth_filter_max = 12000.0f;
    EnvelopeShape synth_amp_attack_shape = EnvelopeShape::Linear;
    EnvelopeShape synth_amp_decay_shape = EnvelopeShape::Linear;
    EnvelopeShape synth_pitch_shape = EnvelopeShape::Linear;
    EnvelopeShape synth_filter_shape = EnvelopeShape::Linear;
    EnvelopeShape fm_mod_pitch_shape = EnvelopeShape::Linear;
    EnvelopeShape fm_index_shape = EnvelopeShape::Linear;
    EnvelopeShape am_pitch_shape = EnvelopeShape::Linear;
    EnvelopeShape am_depth_shape = EnvelopeShape::Linear;
    drumrom::synth::FmToneParams* fm = nullptr;

    if (is_sample_based_source(cfg.source)) {
        const drumrom::main_ui_overlay_waveform::SampleOverlayPointParams point_params{
            cfg.sample.amp_attack_s,
            cfg.sample.amp_decay_s,
            cfg.sample.amp_sustain,
            cfg.sample.amp_release_s,
            cfg.sample.filter_cutoff_hz,
            cfg.sample.filter_cutoff_end_hz,
            cfg.sample.filter_env_decay_s,
            cfg.sample.tune_semitones,
        };
        drumrom::main_ui_overlay_waveform::build_sample_overlay_points(point_params, &curve_points);
    } else {
        switch (cfg.drum) {
            case DrumKind::Kick:
                fm = &cfg.params.kick.fm;
                synth_amp_attack = &cfg.params.kick.attack_rate;
                synth_amp_decay = &cfg.params.kick.env_decay_rate;
                synth_pitch_decay = &cfg.params.kick.pitch_decay_rate;
                synth_pitch_start = &cfg.params.kick.pitch_start_hz;
                synth_pitch_end = &cfg.params.kick.pitch_end_hz;
                synth_filter_decay = &cfg.params.kick.tone_decay_rate;
                synth_amp_attack_shape = cfg.params.kick.amp_attack_shape;
                synth_amp_decay_shape = cfg.params.kick.amp_decay_shape;
                synth_pitch_shape = cfg.params.kick.pitch_env_shape;
                synth_filter_shape = cfg.params.kick.tone_env_shape;
                synth_filter_min = 100.0f;
                synth_filter_max = 6000.0f;
                break;
            case DrumKind::Snare:
                fm = &cfg.params.snare.fm;
                synth_amp_attack = &cfg.params.snare.attack_rate;
                synth_amp_decay = &cfg.params.snare.amp_decay_rate;
                synth_pitch_decay = &cfg.params.snare.pitch_decay_rate;
                synth_pitch_start = &cfg.params.snare.tone_freq_hz;
                synth_pitch_end = &cfg.params.snare.tone_freq_end_hz;
                synth_filter_decay = &cfg.params.snare.tone_decay_rate;
                synth_filter_start = &cfg.params.snare.tone_freq_hz;
                synth_filter_end = &cfg.params.snare.tone_freq_end_hz;
                synth_amp_attack_shape = cfg.params.snare.amp_attack_shape;
                synth_amp_decay_shape = cfg.params.snare.amp_decay_shape;
                synth_pitch_shape = cfg.params.snare.pitch_env_shape;
                synth_filter_shape = cfg.params.snare.tone_env_shape;
                has_tone_freq_filter = true;
                synth_pitch_min = 20.0f;
                synth_pitch_max = 10000.0f;
                synth_filter_min = 20.0f;
                synth_filter_max = 10000.0f;
                break;
            case DrumKind::Hihat:
                fm = &cfg.params.hihat.fm;
                synth_amp_attack = &cfg.params.hihat.attack_rate;
                synth_amp_decay = &cfg.params.hihat.decay_rate;
                synth_pitch_decay = &cfg.params.hihat.pitch_decay_rate;
                synth_pitch_start = &cfg.params.hihat.tone_freq_hz;
                synth_pitch_end = &cfg.params.hihat.tone_freq_end_hz;
                synth_filter_decay = &cfg.params.hihat.tone_decay_rate;
                synth_filter_start = &cfg.params.hihat.tone_freq_hz;
                synth_filter_end = &cfg.params.hihat.tone_freq_end_hz;
                synth_amp_attack_shape = cfg.params.hihat.amp_attack_shape;
                synth_amp_decay_shape = cfg.params.hihat.amp_decay_shape;
                synth_pitch_shape = cfg.params.hihat.pitch_env_shape;
                synth_filter_shape = cfg.params.hihat.tone_env_shape;
                has_tone_freq_filter = true;
                synth_pitch_min = 30.0f;
                synth_pitch_max = 1200.0f;
                synth_filter_min = 30.0f;
                synth_filter_max = 1200.0f;
                break;
            case DrumKind::Tom:
                fm = &cfg.params.tom.fm;
                synth_amp_attack = &cfg.params.tom.attack_rate;
                synth_amp_decay = &cfg.params.tom.env_decay_rate;
                synth_pitch_decay = &cfg.params.tom.pitch_decay_rate;
                synth_pitch_start = &cfg.params.tom.pitch_start_hz;
                synth_pitch_end = &cfg.params.tom.pitch_end_hz;
                synth_filter_decay = &cfg.params.tom.tone_decay_rate;
                synth_amp_attack_shape = cfg.params.tom.amp_attack_shape;
                synth_amp_decay_shape = cfg.params.tom.amp_decay_shape;
                synth_pitch_shape = cfg.params.tom.pitch_env_shape;
                synth_filter_shape = cfg.params.tom.tone_env_shape;
                synth_filter_min = 100.0f;
                synth_filter_max = 6000.0f;
                break;
            case DrumKind::Clap:
                fm = &cfg.params.clap.fm;
                synth_amp_attack = &cfg.params.clap.attack_rate;
                synth_amp_decay = &cfg.params.clap.env_decay_rate;
                synth_pitch_decay = &cfg.params.clap.pitch_decay_rate;
                synth_pitch_start = &cfg.params.clap.tone_freq_hz;
                synth_pitch_end = &cfg.params.clap.tone_freq_end_hz;
                synth_filter_decay = &cfg.params.clap.tone_decay_rate;
                synth_filter_start = &cfg.params.clap.tone_freq_hz;
                synth_filter_end = &cfg.params.clap.tone_freq_end_hz;
                synth_amp_attack_shape = cfg.params.clap.amp_attack_shape;
                synth_amp_decay_shape = cfg.params.clap.amp_decay_shape;
                synth_pitch_shape = cfg.params.clap.pitch_env_shape;
                synth_filter_shape = cfg.params.clap.tone_env_shape;
                has_tone_freq_filter = true;
                synth_pitch_min = 30.0f;
                synth_pitch_max = 4000.0f;
                synth_filter_min = 30.0f;
                synth_filter_max = 4000.0f;
                break;
            case DrumKind::Elements:
            case DrumKind::ElementsExact:
                // Elements variants do not expose the same ADSR/FM overlay model.
                // Keep pointers null so generic safe overlay points are used.
                break;
        }
        if (fm) {
            fm_mod_pitch_shape = fm->mod_pitch_env_shape;
            fm_index_shape = fm->mod_index_env_shape;
            am_pitch_shape = fm->amp_osc_pitch_env_shape;
            am_depth_shape = fm->amp_osc_depth_env_shape;
        }

        drumrom::main_ui_overlay_waveform::OverlayPointMathFns point_math{};
        point_math.rate_to_x = rate_to_x;
        point_math.rate_to_x_attack = rate_to_x_attack;
        point_math.freq_to_norm_range = freq_to_norm_range;

        drumrom::main_ui_overlay_waveform::SynthOverlayPointParams point_params{};
        point_params.has_tone_freq_filter = has_tone_freq_filter;
        point_params.synth_amp_attack = synth_amp_attack;
        point_params.synth_amp_decay = synth_amp_decay;
        point_params.synth_pitch_decay = synth_pitch_decay;
        point_params.synth_pitch_start = synth_pitch_start;
        point_params.synth_pitch_end = synth_pitch_end;
        point_params.synth_filter_decay = synth_filter_decay;
        point_params.synth_filter_start = synth_filter_start;
        point_params.synth_filter_end = synth_filter_end;
        point_params.synth_pitch_min = synth_pitch_min;
        point_params.synth_pitch_max = synth_pitch_max;
        point_params.synth_filter_min = synth_filter_min;
        point_params.synth_filter_max = synth_filter_max;
        point_params.fm = fm;
        drumrom::main_ui_overlay_waveform::build_synth_overlay_points(point_params, point_math, &curve_points);
    }

    auto draw_poly = [&](const std::vector<ImVec2>& pts, ImU32 col, float thick) {
        for (std::size_t i = 1; i < pts.size(); ++i) {
            dl->AddLine(to_screen(pts[i - 1].x, pts[i - 1].y), to_screen(pts[i].x, pts[i].y), col, thick);
        }
    };

    auto draw_shaped_segment = [&](const ImVec2& p0, const ImVec2& p1, EnvelopeShape shape, bool is_attack_segment, ImU32 col, float thick) {
        const auto points = drumrom::main_ui_overlay_waveform::build_shaped_segment_points(p0, p1, shape, is_attack_segment, 28);
        for (std::size_t i = 1; i < points.size(); ++i) {
            dl->AddLine(to_screen(points[i - 1].x, points[i - 1].y), to_screen(points[i].x, points[i].y), col, thick);
        }
    };

    auto draw_overlay_poly = [&](OverlayId id, const std::vector<ImVec2>& pts, ImU32 base_col) {
        const bool active = (id == g_overlay_selected);
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(base_col);
        col.w *= active ? 1.0f : 0.28f;
        const float thickness = active ? 3.0f : 1.5f;
        draw_poly(pts, ImGui::ColorConvertFloat4ToU32(col), thickness);
    };

    auto draw_overlay_segment = [&](OverlayId id, const ImVec2& p0, const ImVec2& p1, EnvelopeShape shape, bool is_attack_segment, ImU32 base_col) {
        const bool active = (id == g_overlay_selected);
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(base_col);
        col.w *= active ? 1.0f : 0.28f;
        const float thickness = active ? 3.0f : 1.5f;
        draw_shaped_segment(p0, p1, shape, is_attack_segment, ImGui::ColorConvertFloat4ToU32(col), thickness);
    };

    // Draw selected slot's envelope curves (only in non-loop modes)
    if (!in_loop_edit_mode) {
        if (is_sample_based_source(cfg.source)) {
            draw_overlay_poly(OverlayId::Amp, amp_pts, IM_COL32(235, 105, 90, 230));
            draw_overlay_poly(OverlayId::ToneOrFilter, filter_pts, IM_COL32(70, 205, 120, 230));
            draw_overlay_poly(OverlayId::Pitch, pitch_pts, IM_COL32(85, 145, 255, 230));
        } else {
            if (amp_pts.size() >= 4) {
                draw_overlay_segment(OverlayId::Amp, amp_pts[0], amp_pts[1], synth_amp_attack_shape, true, IM_COL32(235, 105, 90, 230));
                draw_overlay_segment(OverlayId::Amp, amp_pts[1], amp_pts[2], synth_amp_decay_shape, false, IM_COL32(235, 105, 90, 230));
                draw_overlay_poly(OverlayId::Amp, std::vector<ImVec2>{amp_pts[2], amp_pts[3]}, IM_COL32(235, 105, 90, 230));
            }
            if (pitch_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::Pitch, pitch_pts[0], pitch_pts[1], synth_pitch_shape, false, IM_COL32(85, 145, 255, 230));
                draw_overlay_poly(OverlayId::Pitch, std::vector<ImVec2>{pitch_pts[1], pitch_pts[2]}, IM_COL32(85, 145, 255, 230));
            }
            if (filter_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::ToneOrFilter, filter_pts[0], filter_pts[1], synth_filter_shape, false, IM_COL32(70, 205, 120, 230));
                draw_overlay_poly(OverlayId::ToneOrFilter, std::vector<ImVec2>{filter_pts[1], filter_pts[2]}, IM_COL32(70, 205, 120, 230));
            }
            if (fm_mod_pitch_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::FmModPitch, fm_mod_pitch_pts[0], fm_mod_pitch_pts[1], fm_mod_pitch_shape, false, IM_COL32(245, 205, 75, 225));
                draw_overlay_poly(OverlayId::FmModPitch, std::vector<ImVec2>{fm_mod_pitch_pts[1], fm_mod_pitch_pts[2]}, IM_COL32(245, 205, 75, 225));
            }
            if (fm_index_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::FmIndex, fm_index_pts[0], fm_index_pts[1], fm_index_shape, false, IM_COL32(210, 105, 225, 225));
                draw_overlay_poly(OverlayId::FmIndex, std::vector<ImVec2>{fm_index_pts[1], fm_index_pts[2]}, IM_COL32(210, 105, 225, 225));
            }
            if (am_pitch_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::AmPitch, am_pitch_pts[0], am_pitch_pts[1], am_pitch_shape, false, IM_COL32(80, 220, 230, 225));
                draw_overlay_poly(OverlayId::AmPitch, std::vector<ImVec2>{am_pitch_pts[1], am_pitch_pts[2]}, IM_COL32(80, 220, 230, 225));
            }
            if (am_depth_pts.size() >= 3) {
                draw_overlay_segment(OverlayId::AmDepth, am_depth_pts[0], am_depth_pts[1], am_depth_shape, false, IM_COL32(245, 155, 65, 225));
                draw_overlay_poly(OverlayId::AmDepth, std::vector<ImVec2>{am_depth_pts[1], am_depth_pts[2]}, IM_COL32(245, 155, 65, 225));
            }
        }
    }

    // Handle selection and dragging for selected envelope mode (only in non-loop modes).
    if (!in_loop_edit_mode) {
        drumrom::main_ui_overlay_waveform::OverlaySelectorIds overlay_ids{};
        overlay_ids.amp = static_cast<int>(OverlayId::Amp);
        overlay_ids.pitch = static_cast<int>(OverlayId::Pitch);
        overlay_ids.tone_or_filter = static_cast<int>(OverlayId::ToneOrFilter);
        overlay_ids.fm_mod_pitch = static_cast<int>(OverlayId::FmModPitch);
        overlay_ids.fm_index = static_cast<int>(OverlayId::FmIndex);
        overlay_ids.am_pitch = static_cast<int>(OverlayId::AmPitch);
        overlay_ids.am_depth = static_cast<int>(OverlayId::AmDepth);
        handles = drumrom::main_ui_overlay_waveform::build_overlay_handles(
            static_cast<int>(g_overlay_selected),
            is_sample_based_source(cfg.source),
            has_tone_freq_filter,
            overlay_ids,
            curve_points);
    }

    const float kHandleFillRadius = 9.0f;
    const float kHandleOutlineRadius = 11.0f;
    const float kHandleHitRadius = 20.0f;

    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    std::vector<ImVec2> screen_handles;
    screen_handles.reserve(handles.size());
    for (const ImVec2& h : handles) {
        screen_handles.push_back(to_screen(h.x, h.y));
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool consumed_loop_marker = false;
        if (cfg.source == SourceKind::Loop && g_loop_split_waveform_length > 1 && g_loop_split_boundaries.size() >= 3) {
            const float denom = static_cast<float>(g_loop_split_waveform_length - 1);
            const float handle_y = pmax.y - 10.0f;
            constexpr float kLoopHandleHitRadius = 10.0f;
            for (std::size_t i = 1; i + 1 < g_loop_split_boundaries.size(); ++i) {
                const float x = pmin.x + ((static_cast<float>(g_loop_split_boundaries[i]) / denom) * (pmax.x - pmin.x));
                const float dx = mouse.x - x;
                const float dy = mouse.y - handle_y;
                if ((dx * dx) + (dy * dy) <= (kLoopHandleHitRadius * kLoopHandleHitRadius)) {
                    g_loop_split_drag_boundary = static_cast<int>(i);
                    consumed_loop_marker = true;
                    break;
                }
            }
        }

        const int hit = consumed_loop_marker ? -1 : drumrom::main_ui_overlay_waveform::pick_handle_at_point(screen_handles, mouse, kHandleHitRadius);
        if (!consumed_loop_marker && !in_loop_edit_mode && hit >= 0) {
            g_overlay_drag_selected = g_overlay_selected;
            g_overlay_drag_point = hit;
        } else if (!consumed_loop_marker && cfg.source == SourceKind::Loop &&
                   g_loop_split_waveform_length > 1 &&
                   g_loop_split_boundaries.size() >= 2) {
            const float nx = clampf((mouse.x - pmin.x) / std::max(1.0f, (pmax.x - pmin.x)), 0.0f, 1.0f);
            const std::size_t sample_index = static_cast<std::size_t>(nx * static_cast<float>(g_loop_split_waveform_length - 1));
            for (std::size_t i = 0; i + 1 < g_loop_split_boundaries.size(); ++i) {
                if (sample_index < g_loop_split_boundaries[i] || sample_index > g_loop_split_boundaries[i + 1]) {
                    continue;
                }
                const std::size_t max_index = std::max<std::size_t>(1, g_loop_split_waveform_length - 1);
                const std::size_t b0 = std::min(g_loop_split_boundaries[i], max_index);
                const std::size_t b1 = std::min(g_loop_split_boundaries[i + 1], max_index);
                int start_pct = static_cast<int>((b0 * 100u) / max_index);
                int end_pct = static_cast<int>((b1 * 100u) / max_index);
                start_pct = std::clamp(start_pct, 0, 99);
                end_pct = std::clamp(end_pct, start_pct + 1, 100);

                g_loop_split_selected_region = static_cast<int>(i);


                // Assign this region to the currently selected slot
                if (i < g_loop_split_slot_indices.size()) {
                    g_loop_split_slot_indices[i] = g_selected_slot;
                }
                if (g_selected_slot < g_slot_cfg.size()) {
                    g_slot_cfg[g_selected_slot].sample.start_pct = start_pct;
                    g_slot_cfg[g_selected_slot].sample.end_pct = end_pct;
                }

                *changed = true;
                g_params_dirty = true;
                g_auto_upload_commit_requested = true;
                g_auto_play_commit_requested = true;
                break;
            }
        }
    }

    if (cfg.source == SourceKind::Loop &&
        g_loop_split_drag_boundary > 0 &&
        g_loop_split_drag_boundary + 1 < static_cast<int>(g_loop_split_boundaries.size()) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const std::size_t max_index = std::max<std::size_t>(1, g_loop_split_waveform_length - 1);
        const float nx = clampf((mouse.x - pmin.x) / std::max(1.0f, (pmax.x - pmin.x)), 0.0f, 1.0f);
        std::size_t idx = static_cast<std::size_t>(nx * static_cast<float>(max_index));
        const std::size_t left = g_loop_split_boundaries[static_cast<std::size_t>(g_loop_split_drag_boundary - 1)] + 1;
        const std::size_t right = g_loop_split_boundaries[static_cast<std::size_t>(g_loop_split_drag_boundary + 1)] - 1;
        if (right > left) {
            idx = std::clamp(idx, left, right);
            g_loop_split_boundaries[static_cast<std::size_t>(g_loop_split_drag_boundary)] = idx;
            if (g_loop_split_selected_region >= 0 && g_loop_split_selected_region + 1 < static_cast<int>(g_loop_split_boundaries.size())) {
                const std::size_t r0 = g_loop_split_boundaries[static_cast<std::size_t>(g_loop_split_selected_region)];
                const std::size_t r1 = g_loop_split_boundaries[static_cast<std::size_t>(g_loop_split_selected_region + 1)];
                int start_pct = std::clamp(static_cast<int>((r0 * 100u) / max_index), 0, 99);
                int end_pct = std::clamp(static_cast<int>((r1 * 100u) / max_index), start_pct + 1, 100);
                cfg.sample.start_pct = start_pct;
                cfg.sample.end_pct = end_pct;
                *changed = true;
                g_params_dirty = true;
            }
        }
    }

    if (g_loop_split_drag_boundary >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_auto_upload_commit_requested = true;
        g_auto_play_commit_requested = true;
        g_loop_split_drag_boundary = -1;
    }

    if (g_overlay_drag_point >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_auto_upload_commit_requested = true;
        g_auto_play_commit_requested = true;
        g_overlay_drag_point = -1;
    }

    // Only allow envelope editing in non-loop modes
    if (!in_loop_edit_mode && g_overlay_drag_point >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float nx = clampf((mouse.x - pmin.x) / std::max(1.0f, (pmax.x - pmin.x)), 0.0f, 1.0f);
        const float ny = clampf((mouse.y - pmin.y) / std::max(1.0f, (pmax.y - pmin.y)), 0.0f, 1.0f);
        if (is_sample_based_source(cfg.source)) {
            drumrom::main_ui_overlay_waveform::OverlaySelectorIds ids{};
            ids.amp = static_cast<int>(OverlayId::Amp);
            ids.pitch = static_cast<int>(OverlayId::Pitch);
            ids.tone_or_filter = static_cast<int>(OverlayId::ToneOrFilter);

            drumrom::main_ui_overlay_waveform::SampleDragParams params{};
            params.amp_attack_s = &cfg.sample.amp_attack_s;
            params.amp_decay_s = &cfg.sample.amp_decay_s;
            params.amp_sustain = &cfg.sample.amp_sustain;
            params.amp_release_s = &cfg.sample.amp_release_s;
            params.filter_cutoff_hz = &cfg.sample.filter_cutoff_hz;
            params.filter_cutoff_end_hz = &cfg.sample.filter_cutoff_end_hz;
            params.filter_env_decay_s = &cfg.sample.filter_env_decay_s;
            params.tune_semitones = &cfg.sample.tune_semitones;

            const bool sample_changed = drumrom::main_ui_overlay_waveform::apply_sample_drag_update(
                static_cast<int>(g_overlay_drag_selected),
                g_overlay_drag_point,
                nx,
                ny,
                ids,
                &params);
            *changed |= sample_changed;
        } else {
            drumrom::main_ui_overlay_waveform::OverlaySelectorIds ids{};
            ids.amp = static_cast<int>(OverlayId::Amp);
            ids.pitch = static_cast<int>(OverlayId::Pitch);
            ids.tone_or_filter = static_cast<int>(OverlayId::ToneOrFilter);
            ids.fm_mod_pitch = static_cast<int>(OverlayId::FmModPitch);
            ids.fm_index = static_cast<int>(OverlayId::FmIndex);
            ids.am_pitch = static_cast<int>(OverlayId::AmPitch);
            ids.am_depth = static_cast<int>(OverlayId::AmDepth);

            drumrom::main_ui_overlay_waveform::SynthDragMathFns math{};
            math.x_to_rate = x_to_rate;
            math.x_to_rate_attack = x_to_rate_attack;
            math.rate_to_x_attack = rate_to_x_attack;
            math.norm_to_freq_range = norm_to_freq_range;

            drumrom::main_ui_overlay_waveform::SynthDragParams params{};
            params.has_tone_freq_filter = has_tone_freq_filter;
            params.synth_amp_attack = synth_amp_attack;
            params.synth_amp_decay = synth_amp_decay;
            params.synth_pitch_decay = synth_pitch_decay;
            params.synth_pitch_start = synth_pitch_start;
            params.synth_pitch_end = synth_pitch_end;
            params.synth_filter_decay = synth_filter_decay;
            params.synth_filter_start = synth_filter_start;
            params.synth_filter_end = synth_filter_end;
            params.synth_pitch_min = synth_pitch_min;
            params.synth_pitch_max = synth_pitch_max;
            params.synth_filter_min = synth_filter_min;
            params.synth_filter_max = synth_filter_max;
            params.fm = fm;

            const bool synth_changed = drumrom::main_ui_overlay_waveform::apply_synth_drag_update(
                static_cast<int>(g_overlay_drag_selected),
                g_overlay_drag_point,
                nx,
                ny,
                ids,
                math,
                &params);
            *changed |= synth_changed;
        }
    }

    // Draw handles for selected envelope (only in non-loop modes).
    if (!in_loop_edit_mode) {
        drumrom::main_ui_overlay_waveform::draw_handles(
            dl,
            screen_handles,
            kHandleFillRadius,
            kHandleOutlineRadius,
            IM_COL32(245, 245, 245, 245),
            IM_COL32(20, 20, 20, 255),
            2.0f);
    }

}
