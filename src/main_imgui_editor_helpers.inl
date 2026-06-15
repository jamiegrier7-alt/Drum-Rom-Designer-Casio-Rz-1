// Editor helper implementations for controls, actions, and per-slot editing UI behavior.
bool listbox_nav_with_focus(const std::string& listbox_id, int item_count, int* selected_index, bool* activate_selection) {
    if (activate_selection != nullptr) {
        *activate_selection = false;
    }
    if (selected_index == nullptr || item_count <= 0) {
        return false;
    }
    if (g_focused_listbox_id != listbox_id || !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        return false;
    }

    int idx = *selected_index;
    if (idx < 0) {
        idx = 0;
    }

    bool moved = false;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        idx = std::max(0, idx - 1);
        moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        idx = std::min(item_count - 1, idx + 1);
        moved = true;
    }

    if (moved) {
        *selected_index = idx;
    }
    if (activate_selection != nullptr) {
        *activate_selection = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    }
    return moved;
}

void render_action_pane(SlotConfig& cfg, bool* changed) {
    ImGui::BeginChild("ActionPane", ImVec2(0, 0), true);

    const ImGuiStyle& action_style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(action_style.FramePadding.x, std::max(1.0f, action_style.FramePadding.y * 0.82f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(action_style.ItemSpacing.x, std::max(1.0f, action_style.ItemSpacing.y * 0.82f)));

    const float action_button_small_w = 74.0f * g_ui_scale;
    const float action_button_w = 150.0f * g_ui_scale;
    const float preset_list_h = 150.0f * g_ui_scale;

    ImGui::SeparatorText("Actions");
    ImGui::Checkbox("Auto USB slot upload", &g_auto_upload_enabled);
    ImGui::Checkbox("Auto Play", &g_auto_play_enabled);


    
    // --- Undo/Redo Group ---
    ImGui::BeginGroup();
    if (!can_undo()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo", ImVec2(action_button_small_w, 0))) perform_undo();
    if (!can_undo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!can_redo()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo", ImVec2(action_button_small_w, 0))) perform_redo();
    if (!can_redo()) ImGui::EndDisabled();
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);

    // --- Slot Actions Group ---
    ImGui::BeginGroup();
    if (ImGui::Button("Randomize Slot", ImVec2(action_button_w, 0))) {
        randomize_slot(cfg);
        g_wave_preview_dirty = true;
        if (changed != nullptr) *changed = true;
    }
    if (ImGui::Button("Randomize Reverb (Y)", ImVec2(action_button_w, 0))) {
        randomize_reverb(cfg);
        g_wave_preview_dirty = true;
        if (changed != nullptr) *changed = true;
    }
    if (ImGui::Button("Copy Slot", ImVec2(action_button_small_w, 0))) copy_selected_slot_to_clipboard();
    ImGui::SameLine();
    if (!g_slot_clipboard.has_value()) ImGui::BeginDisabled();
    if (ImGui::Button("Paste Slot", ImVec2(action_button_small_w, 0))) {
        paste_slot_from_clipboard();
        cfg = g_slot_cfg[g_selected_slot];
        if (changed != nullptr) *changed = true;
    }
    if (!g_slot_clipboard.has_value()) ImGui::EndDisabled();
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);

    // --- Render/Preview Group ---
    ImGui::BeginGroup();
    if (ImGui::Button("Play Preview", ImVec2(action_button_w, 0))) (void)play_slot_preview(g_selected_slot);
    if (ImGui::Button("Render Slot", ImVec2(action_button_w, 0))) {
        (void)render_one_slot(g_selected_slot);
        g_wave_preview_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Render All Slots", ImVec2(action_button_w, 0))) {
        (void)render_all_slots();
        g_wave_preview_dirty = true;
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // --- Upload Actions Group ---
    ImGui::BeginGroup();
    if (ImGui::Button("Upload Slot Now", ImVec2(action_button_w, 0))) {
        if (render_one_slot(g_selected_slot)) {
            (void)upload_slot_to_device(g_selected_slot);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Upload All ROM", ImVec2(action_button_w, 0))) (void)upload_all_rom_slots_to_device();
    ImGui::SameLine();
    if (ImGui::Button("Upload ROM B", ImVec2(action_button_w, 0))) (void)upload_rom_b_slots_to_device();
    if (ImGui::Button("Upload MIDI", ImVec2(action_button_w, 0))) (void)upload_midi_sample_data();
    ImGui::EndGroup();

    ImGui::Spacing();

    // --- ROM/Other Actions Group ---
    ImGui::BeginGroup();
    if (ImGui::Button("Build ROM", ImVec2(action_button_w, 0))) (void)build_rom_image();
    ImGui::SameLine();
    if (ImGui::Button("Program Full ROM", ImVec2(action_button_w, 0))) {
        if (render_one_slot(g_selected_slot)) (void)program_slot_to_device_full_rom(g_selected_slot);
    }
    if (ImGui::Button("Get ROM A+B", ImVec2(action_button_w, 0))) {
        if (load_rom_ab_samples_into_editor()) {
            if (changed != nullptr) *changed = true;
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    ImGui::SeparatorText("Presets / Kits");
    if (ImGui::Button("Refresh Preset Folders")) {
        refresh_preset_folders();
    }

    const float toggle_button_w = action_button_w * 0.5f - 2.0f;
    if (ImGui::Button("Preset", ImVec2(toggle_button_w, 0.0f))) {
        const bool was_sysex_mode = g_show_sysex_file_browser;
        g_show_sysex_file_browser = false;
        if (g_show_kits_mode) {
            g_show_kits_mode = false;
            refresh_preset_files_for_folder();
        } else if (was_sysex_mode) {
            refresh_preset_files_for_folder();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Kit", ImVec2(toggle_button_w, 0.0f))) {
        const bool was_sysex_mode = g_show_sysex_file_browser;
        g_show_sysex_file_browser = false;
        if (!g_show_kits_mode) {
            g_show_kits_mode = true;
            refresh_preset_files_for_folder();
        } else if (was_sysex_mode) {
            refresh_preset_files_for_folder();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Sample", ImVec2(toggle_button_w, 0.0f))) {
        g_sysex_folder_target = SysexFolderTarget::Sample;
        g_show_sysex_file_browser = true;
        refresh_sysex_files_for_target(g_sysex_folder_target);
        set_status("SysEx folder target: sample");
    }
    ImGui::SameLine();
    if (ImGui::Button("Rhythm", ImVec2(toggle_button_w, 0.0f))) {
        g_sysex_folder_target = SysexFolderTarget::Rhythm;
        g_show_sysex_file_browser = true;
        refresh_sysex_files_for_target(g_sysex_folder_target);
        set_status("SysEx folder target: rhythm");
    }

    if (ImGui::BeginListBox("Preset Files", ImVec2(0.0f, preset_list_h))) {
        bool activate_preset_selection = false;
        const bool preset_nav_moved = listbox_nav_with_focus("PresetFiles", static_cast<int>(g_current_preset_files.size()), &g_selected_preset_file, &activate_preset_selection);

        const auto apply_preset_selection = [&](int idx) {
            if (idx < 0 || idx >= static_cast<int>(g_current_preset_files.size())) {
                return;
            }
            g_selected_preset_file = idx;
            const std::filesystem::path p(g_current_preset_files[static_cast<std::size_t>(idx)]);
            const std::string file_name = p.filename().string();
            const std::string preset_name = g_show_sysex_file_browser ? p.stem().string() : preset_path_to_name(file_name);
            std::snprintf(g_preset_path_buf, sizeof(g_preset_path_buf), "%s", preset_name.c_str());
        };

        for (std::size_t i = 0; i < g_current_preset_files.size(); ++i) {
            const std::filesystem::path p(g_current_preset_files[i]);
            const std::string file_name = p.filename().string();
            const bool selected = (g_selected_preset_file == static_cast<int>(i));
            if (ImGui::Selectable(file_name.c_str(), selected)) {
                g_focused_listbox_id = "PresetFiles";
                apply_preset_selection(static_cast<int>(i));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
                if (preset_nav_moved) {
                    ImGui::SetScrollHereY(0.5f);
                }
            }
        }
        if (preset_nav_moved) {
            apply_preset_selection(g_selected_preset_file);
        }
        if (activate_preset_selection) {
            apply_preset_selection(g_selected_preset_file);
        }
        ImGui::EndListBox();
    }

    ImGui::InputText("Name", g_preset_path_buf, sizeof(g_preset_path_buf));

    const bool sysex_target_is_sample = (g_sysex_folder_target == SysexFolderTarget::Sample);
    if (ImGui::Button("Save", ImVec2(action_button_w, 0))) {
        if (g_show_sysex_file_browser) {
            if (sysex_target_is_sample) {
                (void)save_sample_rz1_sysex_capture();
            } else {
                (void)save_rhythm_rz1_sysex_capture();
            }
        } else if (g_show_kits_mode) {
            const std::string full_path = preset_name_to_path(g_preset_path_buf, true);
            if (save_all_slot_presets(full_path, g_slot_cfg)) {
                set_status("Kit saved: " + std::string(g_preset_path_buf));
                refresh_preset_folders();
            } else {
                set_status("Failed to save kit");
            }
        } else {
            const std::string full_path = preset_name_to_path(g_preset_path_buf, false);
            if (save_slot_preset(full_path, cfg)) {
                set_status("Drum preset saved: " + std::string(g_preset_path_buf));
                refresh_preset_folders();
            } else {
                set_status("Failed to save drum preset");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(action_button_w, 0))) {
        if (g_show_sysex_file_browser) {
            if (sysex_target_is_sample) {
                (void)load_sample_rz1_sysex_capture();
            } else {
                (void)load_rhythm_rz1_sysex_capture();
            }
        } else if (g_show_kits_mode) {
            std::string full_path = preset_name_to_path(g_preset_path_buf, true);
            if (!std::filesystem::exists(full_path)) {
                full_path = "presets/" + std::string(g_preset_path_buf) + ".allslotpreset";
            }
            if (load_all_slot_presets(full_path, g_slot_cfg)) {
                set_status("Kit loaded: " + std::string(g_preset_path_buf));
                g_sample_path_slot = static_cast<std::size_t>(-1);
                g_wave_preview_dirty = true;
                g_params_dirty = true;
                initialize_history_if_needed();
                maybe_commit_history(true);
            } else {
                set_status("Failed to load kit");
            }
        } else {
            std::string full_path = preset_name_to_path(g_preset_path_buf, false);
            if (!std::filesystem::exists(full_path)) {
                full_path = "presets/" + std::string(g_preset_path_buf) + ".slotpreset";
            }
            if (load_slot_preset(full_path, cfg)) {
                set_status("Drum preset loaded: " + std::string(g_preset_path_buf));
                g_sample_path_slot = static_cast<std::size_t>(-1);
                g_wave_preview_dirty = true;
            } else {
                set_status("Failed to load drum preset");
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("MIDI SysEx");
    ImGui::SetNextItemWidth(130.0f * g_ui_scale);
    ImGui::SliderInt("Ch", &g_rz1_sysex_channel, 1, 16);
    ImGui::SetNextItemWidth(170.0f * g_ui_scale);
    ImGui::SliderInt("Dump Delay (ms)", &g_rz1_sysex_dump_delay_ms, 0, 10000);
    ImGui::SetNextItemWidth(170.0f * g_ui_scale);
    ImGui::SliderInt("Handshake Byte Delay (ms)", &g_rz1_sysex_handshake_byte_delay_ms, 0, 100);

    if (!g_midi_out_enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "MIDI out not connected (set in Settings)");
    }

    const float midi_button_w = action_button_w;
    if (ImGui::Button("Get RZ1 Sample", ImVec2(midi_button_w, 0))) {
        (void)send_rz1_sysex_command(0x10u, "10 MIDI SEND sample RAM");
    }
    ImGui::SameLine();
    if (ImGui::Button("Get RZ1 Rhythm", ImVec2(midi_button_w, 0))) {
        (void)send_rz1_sysex_command(0x14u, "14 MIDI SEND rhythm RAM");
    }
    if (ImGui::Button("Send 70 31 (Manual)", ImVec2(midi_button_w, 0))) {
        (void)send_manual_rz1_handshake_31();
    }

    ImGui::Text("Capture: %zu msgs, %zu bytes", g_rz1_sysex_message_count, g_rz1_sysex_capture.size());
    if (ImGui::Button("Clear Capture", ImVec2(midi_button_w, 0))) {
        g_rz1_sysex_capture.clear();
        g_rz1_sysex_message_count = 0;
        g_rz1_sysex_overflow = false;
        g_rz1_sysex_status = "Capture buffer cleared";
        g_sysex_dump_state = drumrom::main_ui_midi::SysexDumpState::Idle;
    }
    const char* ds_name = drumrom::main_ui_midi::sysex_dump_state_name(g_sysex_dump_state);
    const bool ds_active = (g_sysex_dump_state == drumrom::main_ui_midi::SysexDumpState::WaitingForAck ||
                            g_sysex_dump_state == drumrom::main_ui_midi::SysexDumpState::Receiving);
    const ImVec4 ds_color = ds_active
        ? ImVec4(1.0f, 1.0f, 0.3f, 1.0f)
        : (g_sysex_dump_state == drumrom::main_ui_midi::SysexDumpState::Done
            ? kColorGreen
            : (g_sysex_dump_state == drumrom::main_ui_midi::SysexDumpState::Error
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : ImVec4(0.6f, 0.6f, 0.6f, 1.0f)));
    ImGui::TextColored(ds_color, "State: %s", ds_name);
    if (ImGui::Button("Send Sample->RZ1", ImVec2(midi_button_w, 0))) {
        (void)send_rz1_sysex_dump_to_rz1_sample();
    }
    ImGui::SameLine();
    if (ImGui::Button("Send Rhythm->RZ1", ImVec2(midi_button_w, 0))) {
        (void)send_rz1_sysex_dump_to_rz1_rhythm();
    }
    if (ImGui::Button("Build Pad Sample .syx", ImVec2(midi_button_w, 0))) {
        (void)build_sample_pad_sysex(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Build+Send Pad .syx", ImVec2(midi_button_w, 0))) {
        (void)build_sample_pad_sysex(true);
    }
    if (!g_rz1_sysex_status.empty()) {
        ImGui::TextColored(kColorGreen, "Status: %s", g_rz1_sysex_status.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("MIDI Monitor");
    ImGui::Checkbox("Enable Monitor", &g_midi_debug_monitor_enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear Monitor", ImVec2(midi_button_w, 0))) {
        g_midi_debug_monitor_lines.clear();
    }
    std::string monitor_text;
    monitor_text.reserve(g_midi_debug_monitor_lines.size() * 40u);
    for (const std::string& line : g_midi_debug_monitor_lines) {
        monitor_text += line;
        monitor_text.push_back('\n');
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy Monitor", ImVec2(midi_button_w, 0))) {
        ImGui::SetClipboardText(monitor_text.c_str());
    }
    ImGui::Text("Messages: %zu", g_midi_debug_monitor_lines.size());

    const float monitor_height = 140.0f * g_ui_scale;
    static std::vector<char> monitor_text_buffer;
    monitor_text_buffer.assign(monitor_text.begin(), monitor_text.end());
    if (monitor_text_buffer.empty() || monitor_text_buffer.back() != '\0') {
        monitor_text_buffer.push_back('\0');
    }
    ImGui::InputTextMultiline(
        "##RawMidiMonitorText",
        monitor_text_buffer.data(),
        monitor_text_buffer.size(),
        ImVec2(0, monitor_height),
        ImGuiInputTextFlags_ReadOnly);

    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

void render_editor_left_pane(SlotConfig& cfg, bool& changed, float scaled_waveform_height, float source_panel_height) {
    ImGui::BeginChild("EditorPane", ImVec2(0, 0), true);

    int waveform_mode = (g_wave_preview_mode == WavePreviewMode::RomOverview) ? 1 : 0;
    ImGui::TextUnformatted("Waveform View");
    ImGui::SameLine();
    if (ImGui::RadioButton("Slot/Loop", waveform_mode == 0)) {
        waveform_mode = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("ROM Overview", waveform_mode == 1)) {
        waveform_mode = 1;
    }
    const WavePreviewMode requested_mode = (waveform_mode == 1) ? WavePreviewMode::RomOverview : WavePreviewMode::Slot;
    if (requested_mode != g_wave_preview_mode) {
        g_wave_preview_mode = requested_mode;
        g_wave_preview_dirty = true;
    }
    ImGui::TextUnformatted("Slot/Loop view and ROM overview are two views of the same slot data.");

    refresh_wave_preview_if_needed();
    ImGui::BeginChild("WaveformPane", ImVec2(0, scaled_waveform_height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw_waveform_overlay_editor(cfg, &changed);
    ImGui::EndChild();

    ImGui::BeginChild("SourcePanel", ImVec2(0, source_panel_height), true,
              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const SourceKind source_before = cfg.source;
    const float selector_row_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(selector_row_y);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Source");
    ImGui::SameLine();
    {
        if (source_kind_icon_selector("##SourceIcons", &cfg.source)) {
            if (cfg.source == SourceKind::Sample || cfg.source == SourceKind::Loop) {
                const SourceKind global_non_synth_source = cfg.source;
                bool any_non_synth_changed = false;
                for (std::size_t slot_idx = 0; slot_idx < g_slot_cfg.size(); ++slot_idx) {
                    if (g_slot_cfg[slot_idx].source == SourceKind::Synth) {
                        continue;
                    }
                    if (g_slot_cfg[slot_idx].source != global_non_synth_source) {
                        g_slot_cfg[slot_idx].source = global_non_synth_source;
                        any_non_synth_changed = true;
                    }
                }

                if (global_non_synth_source == SourceKind::Sample && source_before != SourceKind::Sample) {
                    if (ensure_default_sample_for_slot(g_selected_slot, cfg)) {
                        g_wave_preview_dirty = true;
                    }
                }

                if (any_non_synth_changed) {
                    g_wave_preview_dirty = true;
                }

                set_status(std::string("Non-synth source mode: ") +
                           ((global_non_synth_source == SourceKind::Loop) ? "Loop" : "Sample"));
            } else if (cfg.source == SourceKind::Synth && source_before != SourceKind::Synth) {
                set_status(std::string("Slot ") + kSlots[g_selected_slot].label + " set to Synth source");
            }
            changed = true;
        }
    }

    if (cfg.source == SourceKind::Synth) {
        ImGui::SameLine(0.0f, 12.0f * g_ui_scale);
        ImGui::SetCursorPosY(selector_row_y);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Drum");
        ImGui::SameLine();
        if (drum_kind_icon_selector("##DrumIcons", &cfg.drum)) {
            changed = true;
        }
    }
    ImGui::EndChild();

    const ImGuiStyle& pane_style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pane_style.FramePadding.x, std::max(1.0f, pane_style.FramePadding.y * 0.82f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pane_style.ItemSpacing.x, std::max(1.0f, pane_style.ItemSpacing.y * 0.82f)));

    if (ImGui::BeginTable("ParamPaneColumns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Primary Params", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Secondary Params", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextColumn();
        ImGui::BeginChild("PrimaryParamsPane", ImVec2(0, 0), false);
        ImGui::PushItemWidth(220.0f * g_ui_scale);
        changed |= render_output_controls(&cfg.output_gain_db, &cfg.limiter_ceiling, &cfg.output_shaper_mode, &cfg.output_saturation);

        if (cfg.source == SourceKind::Synth) {
            switch (cfg.drum) {
                case DrumKind::Elements: {
                    // ── Mutable Elements modal resonator controls ──────────────
                    using drumrom::synth::ElementsModel;
                    ImGui::SeparatorText("Physical Model");
                    static const char* kModelNames[] = {
                        "Membrane", "Plate", "Bar", "Bell", "String", "Tube"
                    };
                    int model_int = static_cast<int>(cfg.elements_params.model);
                    if (ImGui::BeginCombo("Model##elements_model", kModelNames[model_int])) {
                        for (int mi = 0; mi < 6; ++mi) {
                            const bool sel = (model_int == mi);
                            if (ImGui::Selectable(kModelNames[mi], sel)) {
                                cfg.elements_params.model = static_cast<ElementsModel>(mi);
                                changed = true;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SeparatorText("Resonator");
                    changed |= slider_float_with_text_input("Frequency Hz", &cfg.elements_params.frequency_hz, 10.0f, 4000.0f, "%.1f");
                    changed |= slider_float_with_text_input("Brightness", &cfg.elements_params.brightness, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Damping", &cfg.elements_params.damping, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Strike Position", &cfg.elements_params.position, 0.0f, 1.0f, "%.3f");
                    ImGui::SeparatorText("Exciter");
                    changed |= slider_float_with_text_input("Level", &cfg.elements_params.exciter_level, 0.0f, 2.0f, "%.3f");
                    changed |= slider_float_with_text_input("Noise", &cfg.elements_params.exciter_noise, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Duration s", &cfg.elements_params.exciter_dur_s, 0.001f, 0.1f, "%.4f");
                    ImGui::SeparatorText("Output Envelope");
                    changed |= colored_slider_float("Decay Rate", &cfg.elements_params.env_decay_rate, 0.0f, 80.0f, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_slider_float("Attack Rate", &cfg.elements_params.env_attack_rate, 0.0f, 80.0f, kEnvelopeColorAmp, OverlayId::Amp);
                    break;
                }
                case DrumKind::ElementsExact: {
                    using drumrom::synth::ElementsExactCvTarget;
                    using drumrom::synth::ElementsExactResonatorModel;
                    auto* p = &cfg.elements_exact_params;

                    static const char* kResModels[] = {
                        "Modal", "Sympathetic", "Inharmonic String"
                    };
                    static const char* kCvTargets[] = {
                        "None",
                        "Note",
                        "Modulation",
                        "Strength",
                        "Blow CV",
                        "Strike CV",
                        "Bow Level",
                        "Blow Level",
                        "Strike Level",
                        "Geometry",
                        "Brightness",
                        "Damping",
                        "Position",
                        "Space",
                    };

                    ImGui::SeparatorText("Mutable Elements Exact");
                    int resonator_model = static_cast<int>(p->resonator_model);
                    if (ImGui::BeginCombo("Resonator Model", kResModels[std::clamp(resonator_model, 0, 2)])) {
                        for (int i = 0; i < 3; ++i) {
                            const bool sel = (resonator_model == i);
                            if (ImGui::Selectable(kResModels[i], sel)) {
                                p->resonator_model = static_cast<ElementsExactResonatorModel>(i);
                                changed = true;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    changed |= slider_float_with_text_input("Note", &p->note, 0.0f, 127.0f, "%.2f");
                    changed |= slider_float_with_text_input("Modulation", &p->modulation, -48.0f, 48.0f, "%.2f");
                    changed |= slider_float_with_text_input("Strength", &p->strength, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Blow CV", &p->blow_cv, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Strike CV", &p->strike_cv, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Easter Egg", &p->easter_egg, 0.0f, 1.0f, "%.3f");

                    ImGui::SeparatorText("Exciter");
                    changed |= slider_float_with_text_input("Envelope Shape", &p->exciter_envelope_shape, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Bow Level", &p->exciter_bow_level, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Bow Timbre", &p->exciter_bow_timbre, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Blow Level", &p->exciter_blow_level, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Blow Meta", &p->exciter_blow_meta, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Blow Timbre", &p->exciter_blow_timbre, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Strike Level", &p->exciter_strike_level, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Strike Meta", &p->exciter_strike_meta, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Strike Timbre", &p->exciter_strike_timbre, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Signature", &p->exciter_signature, 0.0f, 1.0f, "%.3f");

                    ImGui::SeparatorText("Resonator");
                    changed |= slider_float_with_text_input("Geometry", &p->resonator_geometry, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Brightness", &p->resonator_brightness, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Damping", &p->resonator_damping, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Position", &p->resonator_position, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Mod Freq", &p->resonator_modulation_frequency, 0.0f, 0.01f, "%.6f");
                    changed |= slider_float_with_text_input("Mod Offset", &p->resonator_modulation_offset, 0.0f, 1.0f, "%.3f");

                    ImGui::SeparatorText("Space");
                    changed |= slider_float_with_text_input("Reverb Diffusion", &p->reverb_diffusion, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Reverb LP", &p->reverb_lp, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Space", &p->space, 0.0f, 1.0f, "%.3f");

                    auto render_cv_env = [&](const char* title, auto* env) {
                        ImGui::SeparatorText(title);
                        int target = static_cast<int>(env->target);
                        const char* preview = kCvTargets[std::clamp(target, 0, static_cast<int>(std::size(kCvTargets)) - 1)];
                        std::string combo_id = std::string("Target##") + title;
                        if (ImGui::BeginCombo(combo_id.c_str(), preview)) {
                            for (int i = 0; i < static_cast<int>(std::size(kCvTargets)); ++i) {
                                const bool sel = (target == i);
                                if (ImGui::Selectable(kCvTargets[i], sel)) {
                                    env->target = static_cast<ElementsExactCvTarget>(i);
                                    changed = true;
                                }
                                if (sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        changed |= slider_float_with_text_input((std::string("Amount##") + title).c_str(), &env->amount, -1.0f, 1.0f, "%.3f");
                        changed |= slider_float_with_text_input((std::string("Attack s##") + title).c_str(), &env->attack_s, 0.0005f, 1.0f, "%.4f");
                        changed |= slider_float_with_text_input((std::string("Decay s##") + title).c_str(), &env->decay_s, 0.001f, 2.0f, "%.4f");
                        changed |= slider_float_with_text_input((std::string("Sustain##") + title).c_str(), &env->sustain, 0.0f, 1.0f, "%.3f");
                        changed |= slider_float_with_text_input((std::string("Release s##") + title).c_str(), &env->release_s, 0.001f, 2.0f, "%.4f");
                    };

                    render_cv_env("CV Env 1", &p->cv_env1);
                    render_cv_env("CV Env 2", &p->cv_env2);
                    break;
                }
                case DrumKind::Kick:
                    changed |= colored_slider_float("Pitch Start", &cfg.params.kick.pitch_start_hz, 10.0f, 4000.0f, kEnvelopeColorPitch, OverlayId::Pitch, "%.0f");
                    changed |= colored_slider_float("Pitch End", &cfg.params.kick.pitch_end_hz, 10.0f, 4000.0f, kEnvelopeColorPitch, OverlayId::Pitch, "%.0f");
                    changed |= colored_rate_with_shape_row("Pitch Decay", &cfg.params.kick.pitch_decay_rate, 0.0f, 80.0f, "Pitch Shape", &cfg.params.kick.pitch_env_shape, kEnvelopeColorPitch, OverlayId::Pitch);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Amp Decay", &cfg.params.kick.env_decay_rate, 0.0f, 80.0f, "Amp Decay Shape", &cfg.params.kick.amp_decay_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_attack_with_shape_row("Attack Rate", &cfg.params.kick.attack_rate, 0.0f, 80.0f, "Attack Shape", &cfg.params.kick.amp_attack_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Tone Decay", &cfg.params.kick.tone_decay_rate, 0.0f, 80.0f, "Tone Shape", &cfg.params.kick.tone_env_shape, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    break;
                case DrumKind::Snare:
                    changed |= colored_slider_float("Tone Freq Start", &cfg.params.snare.tone_freq_hz, 20.0f, 10000.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= colored_slider_float("Tone Freq End", &cfg.params.snare.tone_freq_end_hz, 20.0f, 10000.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= colored_rate_with_shape_row("Pitch Decay", &cfg.params.snare.pitch_decay_rate, 0.0f, 80.0f, "Pitch Shape", &cfg.params.snare.pitch_env_shape, kEnvelopeColorPitch, OverlayId::Pitch);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Tone Decay", &cfg.params.snare.tone_decay_rate, 0.0f, 80.0f, "Tone Shape", &cfg.params.snare.tone_env_shape, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= colored_slider_rate("Noise Decay", &cfg.params.snare.noise_decay_rate, 0.0f, 80.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= slider_float_with_text_input("Tone Mix", &cfg.params.snare.tone_mix, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("Noise Mix", &cfg.params.snare.noise_mix, 0.0f, 1.0f, "%.3f");
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Amp Decay", &cfg.params.snare.amp_decay_rate, 0.0f, 80.0f, "Amp Decay Shape", &cfg.params.snare.amp_decay_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_attack_with_shape_row("Attack Rate", &cfg.params.snare.attack_rate, 0.0f, 80.0f, "Attack Shape", &cfg.params.snare.amp_attack_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    break;
                case DrumKind::Hihat:
                    changed |= colored_slider_float("Tone Freq Start", &cfg.params.hihat.tone_freq_hz, 30.0f, 1200.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= colored_slider_float("Tone Freq End", &cfg.params.hihat.tone_freq_end_hz, 30.0f, 1200.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    for (std::size_t i = 0; i < cfg.params.hihat.square_ratios.size(); ++i) {
                        char label[32];
                        std::snprintf(label, sizeof(label), "Square Ratio %u", static_cast<unsigned>(i + 1));
                        changed |= slider_float_with_text_input(label, &cfg.params.hihat.square_ratios[i], 0.2f, 4.0f, "%.3f");
                    }
                    changed |= colored_rate_with_shape_row("Pitch Decay", &cfg.params.hihat.pitch_decay_rate, 0.0f, 80.0f, "Pitch Shape", &cfg.params.hihat.pitch_env_shape, kEnvelopeColorPitch, OverlayId::Pitch);
                    changed |= slider_float_with_text_input("Tone Mix", &cfg.params.hihat.tone_mix, 0.0f, 1.0f, "%.3f");
                    changed |= slider_float_with_text_input("HP Cutoff Hz", &cfg.params.hihat.hp_cutoff_hz, 100.0f, 12000.0f, "%.2f");
                    changed |= slider_float_with_text_input("HP Resonance (Q)", &cfg.params.hihat.hp_resonance, 0.1f, 3.0f, "%.3f");
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Tone Decay", &cfg.params.hihat.tone_decay_rate, 0.0f, 80.0f, "Tone Shape", &cfg.params.hihat.tone_env_shape, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Amp Decay", &cfg.params.hihat.decay_rate, 0.0f, 120.0f, "Amp Decay Shape", &cfg.params.hihat.amp_decay_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_attack_with_shape_row("Attack Rate", &cfg.params.hihat.attack_rate, 0.0f, 80.0f, "Attack Shape", &cfg.params.hihat.amp_attack_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    break;
                case DrumKind::Tom:
                    changed |= colored_slider_float("Pitch Start", &cfg.params.tom.pitch_start_hz, 20.0f, 4000.0f, kEnvelopeColorPitch, OverlayId::Pitch, "%.0f");
                    changed |= colored_slider_float("Pitch End", &cfg.params.tom.pitch_end_hz, 20.0f, 4000.0f, kEnvelopeColorPitch, OverlayId::Pitch, "%.0f");
                    changed |= colored_rate_with_shape_row("Pitch Decay", &cfg.params.tom.pitch_decay_rate, 0.0f, 80.0f, "Pitch Shape", &cfg.params.tom.pitch_env_shape, kEnvelopeColorPitch, OverlayId::Pitch);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Amp Decay", &cfg.params.tom.env_decay_rate, 0.0f, 80.0f, "Amp Decay Shape", &cfg.params.tom.amp_decay_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_attack_with_shape_row("Attack Rate", &cfg.params.tom.attack_rate, 0.0f, 80.0f, "Attack Shape", &cfg.params.tom.amp_attack_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Tone Decay", &cfg.params.tom.tone_decay_rate, 0.0f, 80.0f, "Tone Shape", &cfg.params.tom.tone_env_shape, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    break;
                case DrumKind::Clap:
                    changed |= colored_slider_float("Tone Freq Start", &cfg.params.clap.tone_freq_hz, 30.0f, 4000.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= colored_slider_float("Tone Freq End", &cfg.params.clap.tone_freq_end_hz, 30.0f, 4000.0f, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    changed |= slider_float_with_text_input("Click Pitch (Rate)", &cfg.params.clap.click_rate, 0.25f, 8.0f, "%.3f");
                    changed |= colored_rate_with_shape_row("Pitch Decay", &cfg.params.clap.pitch_decay_rate, 0.0f, 80.0f, "Pitch Shape", &cfg.params.clap.pitch_env_shape, kEnvelopeColorPitch, OverlayId::Pitch);
                    changed |= slider_float_with_text_input("Tone Mix", &cfg.params.clap.tone_mix, 0.0f, 1.0f, "%.3f");
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Tone Decay", &cfg.params.clap.tone_decay_rate, 0.0f, 80.0f, "Tone Shape", &cfg.params.clap.tone_env_shape, kEnvelopeColorTone, OverlayId::ToneOrFilter);
                    envelope_group_gap();
                    changed |= colored_rate_with_shape_row("Amp Decay", &cfg.params.clap.env_decay_rate, 0.0f, 80.0f, "Amp Decay Shape", &cfg.params.clap.amp_decay_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    changed |= colored_attack_with_shape_row("Attack Rate", &cfg.params.clap.attack_rate, 0.0f, 80.0f, "Attack Shape", &cfg.params.clap.amp_attack_shape, kEnvelopeColorAmp, OverlayId::Amp);
                    break;
            }
        } else {
            using drumrom::sample_schema::SampleParamId;
            using drumrom::sample_schema::sample_param_spec;
            changed |= slider_float_with_text_input(
                sample_param_spec(SampleParamId::SourceRateHz).desktop_label,
                &cfg.sample.source_rate_hz,
                sample_param_spec(SampleParamId::SourceRateHz).min_v,
                sample_param_spec(SampleParamId::SourceRateHz).max_v,
                sample_param_spec(SampleParamId::SourceRateHz).format);
            changed |= input_int_with_scroll(
                sample_param_spec(SampleParamId::StartPct).desktop_label,
                &cfg.sample.start_pct,
                static_cast<int>(sample_param_spec(SampleParamId::StartPct).min_v),
                static_cast<int>(sample_param_spec(SampleParamId::StartPct).max_v));
            changed |= input_int_with_scroll(
                sample_param_spec(SampleParamId::EndPct).desktop_label,
                &cfg.sample.end_pct,
                static_cast<int>(sample_param_spec(SampleParamId::EndPct).min_v),
                static_cast<int>(sample_param_spec(SampleParamId::EndPct).max_v));
            changed |= input_int_with_scroll(
                sample_param_spec(SampleParamId::LoopStartPct).desktop_label,
                &cfg.sample.loop_start_pct,
                static_cast<int>(sample_param_spec(SampleParamId::LoopStartPct).min_v),
                static_cast<int>(sample_param_spec(SampleParamId::LoopStartPct).max_v));
            changed |= input_int_with_scroll(
                sample_param_spec(SampleParamId::LoopEndPct).desktop_label,
                &cfg.sample.loop_end_pct,
                static_cast<int>(sample_param_spec(SampleParamId::LoopEndPct).min_v),
                static_cast<int>(sample_param_spec(SampleParamId::LoopEndPct).max_v));
            changed |= slider_float_with_text_input(
                sample_param_spec(SampleParamId::LoopIncrementPct).desktop_label,
                &cfg.sample.loop_increment_pct,
                sample_param_spec(SampleParamId::LoopIncrementPct).min_v,
                sample_param_spec(SampleParamId::LoopIncrementPct).max_v,
                sample_param_spec(SampleParamId::LoopIncrementPct).format);
            changed |= slider_float_with_text_input(
                sample_param_spec(SampleParamId::TuneSemitones).desktop_label,
                &cfg.sample.tune_semitones,
                sample_param_spec(SampleParamId::TuneSemitones).min_v,
                sample_param_spec(SampleParamId::TuneSemitones).max_v,
                sample_param_spec(SampleParamId::TuneSemitones).format);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::FilterCutoffStartHz).desktop_label,
                &cfg.sample.filter_cutoff_hz,
                sample_param_spec(SampleParamId::FilterCutoffStartHz).min_v,
                sample_param_spec(SampleParamId::FilterCutoffStartHz).max_v,
                kEnvelopeColorTone,
                OverlayId::ToneOrFilter);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::FilterCutoffEndHz).desktop_label,
                &cfg.sample.filter_cutoff_end_hz,
                sample_param_spec(SampleParamId::FilterCutoffEndHz).min_v,
                sample_param_spec(SampleParamId::FilterCutoffEndHz).max_v,
                kEnvelopeColorTone,
                OverlayId::ToneOrFilter);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::FilterEnvDecayS).desktop_label,
                &cfg.sample.filter_env_decay_s,
                sample_param_spec(SampleParamId::FilterEnvDecayS).min_v,
                sample_param_spec(SampleParamId::FilterEnvDecayS).max_v,
                kEnvelopeColorTone,
                OverlayId::ToneOrFilter);
            changed |= slider_float_with_text_input(
                sample_param_spec(SampleParamId::FilterResonance).desktop_label,
                &cfg.sample.filter_resonance,
                sample_param_spec(SampleParamId::FilterResonance).min_v,
                sample_param_spec(SampleParamId::FilterResonance).max_v,
                sample_param_spec(SampleParamId::FilterResonance).format);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::AmpAttackS).desktop_label,
                &cfg.sample.amp_attack_s,
                sample_param_spec(SampleParamId::AmpAttackS).min_v,
                sample_param_spec(SampleParamId::AmpAttackS).max_v,
                kEnvelopeColorAmp,
                OverlayId::Amp);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::AmpDecayS).desktop_label,
                &cfg.sample.amp_decay_s,
                sample_param_spec(SampleParamId::AmpDecayS).min_v,
                sample_param_spec(SampleParamId::AmpDecayS).max_v,
                kEnvelopeColorAmp,
                OverlayId::Amp);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::AmpSustain).desktop_label,
                &cfg.sample.amp_sustain,
                sample_param_spec(SampleParamId::AmpSustain).min_v,
                sample_param_spec(SampleParamId::AmpSustain).max_v,
                kEnvelopeColorAmp,
                OverlayId::Amp);
            changed |= colored_slider_float(
                sample_param_spec(SampleParamId::AmpReleaseS).desktop_label,
                &cfg.sample.amp_release_s,
                sample_param_spec(SampleParamId::AmpReleaseS).min_v,
                sample_param_spec(SampleParamId::AmpReleaseS).max_v,
                kEnvelopeColorAmp,
                OverlayId::Amp);

            ImGui::SeparatorText("Amp Envelope");
            int env_mode_int = static_cast<int>(cfg.sample.amp_envelope_mode);
            if (ImGui::RadioButton("Off##amp_off", &env_mode_int, static_cast<int>(AmpEnvelopeMode::Off))) {
                cfg.sample.amp_envelope_mode = AmpEnvelopeMode::Off;
                changed = true;
                g_wave_preview_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Pre-Fit##amp_prefit", &env_mode_int, static_cast<int>(AmpEnvelopeMode::PreFit))) {
                cfg.sample.amp_envelope_mode = AmpEnvelopeMode::PreFit;
                changed = true;
                g_wave_preview_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Output##amp_output", &env_mode_int, static_cast<int>(AmpEnvelopeMode::Output))) {
                cfg.sample.amp_envelope_mode = AmpEnvelopeMode::Output;
                changed = true;
                g_wave_preview_dirty = true;
            }
            ImGui::TextWrapped("Off: No envelope. Pre-Fit: Envelope before resizing (compressed effect). Output: Envelope after resizing (correct timing).");

            const bool reset_pressed = ImGui::Button("Reset Sample Defaults") ||
                (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_O, false));
            if (reset_pressed) {
                cfg.sample.start_pct           = 0;
                cfg.sample.end_pct             = 100;
                cfg.sample.loop_start_pct      = 0;
                cfg.sample.loop_end_pct        = 100;
                cfg.sample.loop_increment_pct  = 0.0f;
                cfg.sample.tune_semitones      = 0.0f;
                cfg.sample.filter_cutoff_hz    = 12000.0f;
                cfg.sample.filter_cutoff_end_hz = 12000.0f;
                cfg.sample.filter_env_decay_s  = 0.0f;
                cfg.sample.filter_resonance    = 0.0f;
                cfg.sample.amp_attack_s        = 0.0f;
                cfg.sample.amp_decay_s         = 0.0f;
                cfg.sample.amp_sustain         = 1.0f;
                cfg.sample.amp_release_s       = 0.2f;
                cfg.sample.amp_envelope_mode   = AmpEnvelopeMode::Output;
                cfg.output_gain_db             = 0.0f;
                cfg.limiter_ceiling            = 1.0f;
                cfg.output_shaper_mode         = 0;
                cfg.output_saturation          = 0.65f;
                changed = true;
                g_wave_preview_dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Shortcut: O");
            }
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("SecondaryParamsPane", ImVec2(0, 0), false);
        if (cfg.source == SourceKind::Synth) {
            switch (cfg.drum) {
                case DrumKind::Kick: changed |= render_fm_section(cfg.params.kick.fm); break;
                case DrumKind::Snare: changed |= render_fm_section(cfg.params.snare.fm); break;
                case DrumKind::Hihat: changed |= render_fm_section(cfg.params.hihat.fm); break;
                case DrumKind::Tom: changed |= render_fm_section(cfg.params.tom.fm); break;
                case DrumKind::Clap: changed |= render_fm_section(cfg.params.clap.fm); break;
                case DrumKind::Elements:
                    ImGui::SeparatorText("Elements");
                    ImGui::TextWrapped(
                        "Modal resonator synthesis inspired by Mutable Instruments Elements.\n\n"
                        "A bank of 8 tuned resonators (frequencies set by the Model) is driven by a "
                        "short strike exciter.\n\n"
                        "Model: shape of the resonator partials\n"
                        "Frequency: fundamental resonant pitch\n"
                        "Brightness: spectral tilt (dark to bright)\n"
                        "Damping: resonator decay (long to short)\n"
                        "Strike Position: excitation point (centre to edge)\n"
                        "Exciter Noise: impulse vs. noise excitation\n"
                    );
                    break;
                case DrumKind::ElementsExact:
                    ImGui::SeparatorText("Elements Exact");
                    ImGui::TextWrapped(
                        "This module uses Mutable Instruments Elements DSP code directly.\n\n"
                        "Controls map to the original patch/performance parameters.\n"
                        "CV Env 1/2 are extra envelopes that can be routed via dropdown to emulate CV destination modulation."
                    );
                    break;
            }
        } else {
            if (cfg.source == SourceKind::Loop) {
                if (ImGui::Button("Refresh Loops")) {
                    refresh_loop_files();
                }
                ImGui::SameLine();
                if (ImGui::Button("Jump To Loops")) {
                    const std::filesystem::path sample_root = sample_root_path();
                    std::filesystem::path target = sample_root / "loops";
                    if (!std::filesystem::exists(target) || !std::filesystem::is_directory(target)) {
                        target = sample_root;
                    }
                    g_selected_loop_folder_path = target.lexically_normal().string();
                    refresh_loop_files();
                }
                ImGui::SameLine();
                if (ImGui::Button("Split Loop Across Slots")) {
                    changed |= split_loop_across_slots(cfg.sample.path);
                }

                const std::filesystem::path sample_root = sample_root_path();
                std::filesystem::path current_loop_folder = g_selected_loop_folder_path.empty()
                    ? sample_root
                    : std::filesystem::path(g_selected_loop_folder_path).lexically_normal();
                if (!sample_path_within_root(current_loop_folder, sample_root)) {
                    current_loop_folder = sample_root;
                }
                ImGui::Text("Current Loop Folder: %s", folder_label(current_loop_folder.string()).c_str());

                if (ImGui::InputText("Loop Path", g_sample_path_buf, sizeof(g_sample_path_buf))) {
                    cfg.sample.path = g_sample_path_buf;
                    changed = true;
                    g_wave_preview_dirty = true;
                }
                refresh_sample_path_status();
                if (!cfg.sample.path.empty()) {
                    if (g_sample_path_status_exists) {
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "file found");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "file not found");
                    }
                }
                if (!g_last_sample_error.empty()) {
                    ImGui::TextWrapped("Error: %s", g_last_sample_error.c_str());
                }
                if (ImGui::BeginListBox("##LoopFiles", ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
                    bool activate_loop_selection = false;
                    const bool loop_nav_moved = listbox_nav_with_focus("LoopFiles", static_cast<int>(g_loop_files.size()), &g_selected_loop_file, &activate_loop_selection);

                    const auto navigate_loop_folder = [&](const std::filesystem::path& target_folder) {
                        if (!sample_path_within_root(target_folder, sample_root) ||
                            !std::filesystem::exists(target_folder) ||
                            !std::filesystem::is_directory(target_folder)) {
                            return;
                        }
                        g_selected_loop_folder_path = target_folder.lexically_normal().string();
                        refresh_loop_files();
                    };

                    const auto apply_loop_selection = [&](int idx) {
                        if (idx < 0 || idx >= static_cast<int>(g_loop_files.size())) {
                            return;
                        }
                        g_selected_loop_file = idx;
                        cfg.sample.path = g_loop_files[static_cast<std::size_t>(idx)];
                        std::snprintf(g_sample_path_buf, sizeof(g_sample_path_buf), "%s", cfg.sample.path.c_str());
                        changed = true;
                        g_wave_preview_dirty = true;
                    };

                    const auto activate_loop_browser_entry = [&](int idx, bool open_directory) {
                        if (idx < 0 || idx >= static_cast<int>(g_loop_files.size())) {
                            return;
                        }
                        g_selected_loop_file = idx;
                        const std::filesystem::path entry(g_loop_files[static_cast<std::size_t>(idx)]);
                        std::error_code ec;
                        if (std::filesystem::is_directory(entry, ec)) {
                            if (open_directory) {
                                navigate_loop_folder(entry);
                            }
                            return;
                        }
                        apply_loop_selection(idx);
                    };

                    for (std::size_t i = 0; i < g_loop_files.size(); ++i) {
                        const std::filesystem::path p(g_loop_files[i]);
                        const std::string file_name = sample_browser_entry_label(g_loop_files[i], current_loop_folder, sample_root);
                        const bool selected = (g_selected_loop_file == static_cast<int>(i));
                        std::error_code ec;
                        const bool is_directory = std::filesystem::is_directory(p, ec);
                        if (ImGui::Selectable(file_name.c_str(), selected, is_directory ? ImGuiSelectableFlags_AllowDoubleClick : 0)) {
                            g_focused_listbox_id = "LoopFiles";
                            if (is_directory) {
                                g_selected_loop_file = static_cast<int>(i);
                                if (ImGui::IsMouseDoubleClicked(0)) {
                                    activate_loop_browser_entry(static_cast<int>(i), true);
                                }
                            } else {
                                apply_loop_selection(static_cast<int>(i));
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                            if (loop_nav_moved) {
                                ImGui::SetScrollHereY(0.5f);
                            }
                        }
                    }
                    if (loop_nav_moved) {
                        activate_loop_browser_entry(g_selected_loop_file, false);
                    }
                    if (activate_loop_selection) {
                        activate_loop_browser_entry(g_selected_loop_file, true);
                    }
                    ImGui::EndListBox();
                }
            } else {
                if (ImGui::Button("Refresh Sample Folders")) {
                    refresh_sample_folders();
                }
                ImGui::SameLine();
                ImGui::Checkbox("Audition on Select", &g_sample_browser_audition_enabled);
                const std::filesystem::path sample_root = sample_root_path();
                std::filesystem::path current_sample_folder = g_selected_folder_path.empty()
                    ? sample_root
                    : std::filesystem::path(g_selected_folder_path).lexically_normal();
                if (!sample_path_within_root(current_sample_folder, sample_root)) {
                    current_sample_folder = sample_root;
                }
                ImGui::Text("Current Folder: %s", folder_label(current_sample_folder.string()).c_str());
                if (ImGui::InputText("Sample Path", g_sample_path_buf, sizeof(g_sample_path_buf))) {
                    cfg.sample.path = g_sample_path_buf;
                    changed = true;
                    g_wave_preview_dirty = true;
                }
                refresh_sample_path_status();
                if (!cfg.sample.path.empty()) {
                    if (g_sample_path_status_exists) {
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "file found");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "file not found");
                    }
                }
                if (!g_last_sample_error.empty()) {
                    ImGui::TextWrapped("Error: %s", g_last_sample_error.c_str());
                }
                if (ImGui::BeginListBox("##SampleFiles", ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
                    bool activate_sample_selection = false;
                    const bool sample_nav_moved = listbox_nav_with_focus("SampleFiles", static_cast<int>(g_current_folder_files.size()), &g_selected_sample_file, &activate_sample_selection);

                    const auto navigate_sample_folder = [&](const std::filesystem::path& target_folder) {
                        if (!sample_path_within_root(target_folder, sample_root) || !std::filesystem::exists(target_folder) || !std::filesystem::is_directory(target_folder)) {
                            return;
                        }
                        g_selected_folder_path = target_folder.lexically_normal().string();
                        refresh_sample_files_for_folder();
                    };

                    const auto apply_sample_selection = [&](int idx) {
                        if (idx < 0 || idx >= static_cast<int>(g_current_folder_files.size())) {
                            return;
                        }
                        g_selected_sample_file = idx;
                        cfg.sample.path = g_current_folder_files[static_cast<std::size_t>(idx)];
                        std::snprintf(g_sample_path_buf, sizeof(g_sample_path_buf), "%s", cfg.sample.path.c_str());
                        changed = true;
                        g_wave_preview_dirty = true;
                        if (g_sample_browser_audition_enabled) {
                            (void)play_original_sample_preview(cfg.sample);
                        }
                    };

                    const auto activate_sample_browser_entry = [&](int idx, bool open_directory) {
                        if (idx < 0 || idx >= static_cast<int>(g_current_folder_files.size())) {
                            return;
                        }
                        g_selected_sample_file = idx;
                        const std::filesystem::path entry(g_current_folder_files[static_cast<std::size_t>(idx)]);
                        std::error_code ec;
                        if (std::filesystem::is_directory(entry, ec)) {
                            if (open_directory) {
                                navigate_sample_folder(entry);
                            }
                            return;
                        }
                        apply_sample_selection(idx);
                    };

                    for (std::size_t i = 0; i < g_current_folder_files.size(); ++i) {
                        const std::filesystem::path p(g_current_folder_files[i]);
                        const std::string file_name = sample_browser_entry_label(g_current_folder_files[i], current_sample_folder, sample_root);
                        const bool selected = (g_selected_sample_file == static_cast<int>(i));
                        std::error_code ec;
                        const bool is_directory = std::filesystem::is_directory(p, ec);
                        if (ImGui::Selectable(file_name.c_str(), selected, is_directory ? ImGuiSelectableFlags_AllowDoubleClick : 0)) {
                            g_focused_listbox_id = "SampleFiles";
                            if (is_directory) {
                                g_selected_sample_file = static_cast<int>(i);
                                if (ImGui::IsMouseDoubleClicked(0)) {
                                    activate_sample_browser_entry(static_cast<int>(i), true);
                                }
                            } else {
                                apply_sample_selection(static_cast<int>(i));
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                            if (sample_nav_moved) {
                                ImGui::SetScrollHereY(0.5f);
                            }
                        }
                    }
                    if (sample_nav_moved) {
                        activate_sample_browser_entry(g_selected_sample_file, false);
                    }
                    if (activate_sample_selection) {
                        activate_sample_browser_entry(g_selected_sample_file, true);
                    }
                    ImGui::EndListBox();
                }
            }
        }

        // Reverb section (applies to all synth and sample voices)
        ImGui::Separator();
        ImGui::SeparatorText("Reverb");
        
        ImGui::SetNextItemWidth(-80.0f);
        bool reverb_enabled = cfg.params.reverb.enabled > 0.5f;
        if (ImGui::Checkbox("Enable Reverb", &reverb_enabled)) {
            cfg.params.reverb.enabled = reverb_enabled ? 1.0f : 0.0f;
            changed = true;
            g_wave_preview_dirty = true;
        }
        
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Decay Time (ms)##reverb", &cfg.params.reverb.decay_time_ms, 10.0f, 2000.0f, "%.0f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }
        
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Damping##reverb", &cfg.params.reverb.damping, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Early Level##reverb", &cfg.params.reverb.early_level, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Early Spread##reverb", &cfg.params.reverb.early_spread, 0.5f, 2.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Diffusion##reverb", &cfg.params.reverb.diffusion, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Tone##reverb", &cfg.params.reverb.tone, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Late Mix##reverb", &cfg.params.reverb.late_mix, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Size##reverb", &cfg.params.reverb.size, 0.5f, 1.5f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Decay Shape##reverb", &cfg.params.reverb.decay_shape, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }
        
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Wet Level##reverb", &cfg.params.reverb.wet_level, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }
        
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Dry Level##reverb", &cfg.params.reverb.dry_level, 0.0f, 1.0f, "%.2f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }
        
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::SliderFloat("Pre-Delay (ms)##reverb", &cfg.params.reverb.pre_delay_ms, 0.0f, 50.0f, "%.1f")) {
            changed = true;
            g_wave_preview_dirty = true;
        }

        ImGui::EndChild();
        ImGui::EndTable();
    }

    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

void render_action_pane_bridge(void* slot_config, bool* changed) {
    if (slot_config == nullptr) {
        return;
    }
    render_action_pane(*static_cast<SlotConfig*>(slot_config), changed);
}

void render_editor_left_pane_bridge(void* slot_config, bool* changed, float scaled_waveform_height, float source_panel_height) {
    if (slot_config == nullptr || changed == nullptr) {
        return;
    }
    render_editor_left_pane(*static_cast<SlotConfig*>(slot_config), *changed, scaled_waveform_height, source_panel_height);
}
