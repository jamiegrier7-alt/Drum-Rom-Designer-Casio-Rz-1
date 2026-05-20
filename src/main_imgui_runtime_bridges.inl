// Runtime bridge helpers that synchronize history, snapshots, and deferred UI/app state.
void apply_pending_snapshot_if_needed() {
    if (!g_has_pending_snapshot) {
        return;
    }
    g_history_index = g_pending_history_index;
    apply_snapshot(g_pending_snapshot);
    g_has_pending_snapshot = false;
}

bool selected_slot_is_synth_source() {
    return g_slot_cfg[g_selected_slot].source == SourceKind::Synth;
}

void play_selected_slot_preview() {
    (void)play_slot_preview(g_selected_slot);
}

void randomize_selected_slot_from_shortcut() {
    randomize_slot(g_slot_cfg[g_selected_slot]);
    g_wave_preview_dirty = true;
    g_auto_upload_commit_requested = true;
    g_auto_play_commit_requested = true;
    set_status("Randomized slot");
}

void randomize_selected_reverb_from_shortcut() {
    randomize_reverb(g_slot_cfg[g_selected_slot]);
    g_wave_preview_dirty = true;
    g_auto_upload_commit_requested = true;
    g_auto_play_commit_requested = true;
    set_status("Randomized reverb");
}

void copy_selected_slot_to_clipboard() {
    if (g_selected_slot >= g_slot_cfg.size()) {
        return;
    }
    g_slot_clipboard = g_slot_cfg[g_selected_slot];
    g_slot_clipboard_source = g_selected_slot;
    set_status(std::string("Copied slot ") + kSlots[g_selected_slot].label);
}

void paste_slot_from_clipboard() {
    if (!g_slot_clipboard.has_value() || g_selected_slot >= g_slot_cfg.size()) {
        return;
    }
    g_slot_cfg[g_selected_slot] = g_slot_clipboard.value();
    g_sample_path_slot = static_cast<std::size_t>(-1);
    g_wave_preview_dirty = true;
    g_params_dirty = true;
    g_auto_upload_commit_requested = true;
    g_auto_play_commit_requested = true;
    const bool have_source_slot = (g_slot_clipboard_source < kSlots.size());
    const std::string source_label = have_source_slot ? kSlots[g_slot_clipboard_source].label : "(unknown)";
    set_status(std::string("Pasted slot ") + source_label + " -> " + kSlots[g_selected_slot].label);
}

void set_selected_slot_drum_kind_from_index(int drum_kind_index) {
    const DrumKind next = drum_kind_from_index(drum_kind_index);
    g_slot_cfg[g_selected_slot].drum = next;
    g_params_dirty = true;
    g_wave_preview_dirty = true;
    g_auto_upload_commit_requested = true;
    set_status(std::string("Drum type: ") + drum_kind_name(next));
    
    // Directly trigger preview when switching drum types via keyboard shortcut
    (void)play_slot_preview(g_selected_slot);
}

void toggle_selected_slot_source_kind() {
    if (g_selected_slot >= g_slot_cfg.size()) {
        return;
    }
    SourceKind next = g_slot_cfg[g_selected_slot].source;
    switch (next) {
        case SourceKind::Synth: next = SourceKind::Sample; break;
        case SourceKind::Sample: next = SourceKind::Loop; break;
        case SourceKind::Loop: next = SourceKind::Synth; break;
    }
    g_slot_cfg[g_selected_slot].source = next;
    g_params_dirty = true;
    g_wave_preview_dirty = true;
    g_auto_upload_commit_requested = true;
    g_auto_play_commit_requested = true;
    
    const char* source_name = "";
    switch (next) {
        case SourceKind::Synth: source_name = "Synth"; break;
        case SourceKind::Sample: source_name = "Sample"; break;
        case SourceKind::Loop: source_name = "Loop"; break;
    }
    set_status(std::string("Source: ") + source_name);
}

void update_ui_scale_for_window() {
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(g_window, &window_w, &window_h);
    const float w_scale = static_cast<float>(window_w) / static_cast<float>(kBaseWindowWidth);
    const float h_scale = static_cast<float>(window_h) / static_cast<float>(kBaseWindowHeight);
    g_ui_scale = std::min(w_scale, h_scale);
    g_ui_scale = std::max(0.5f, std::min(2.0f, g_ui_scale));
    ImGui::GetIO().FontGlobalScale = g_ui_scale;
}

drumrom::main_ui_runtime_bootstrap::State make_runtime_bootstrap_state(SDL_Renderer** renderer_out) {
    drumrom::main_ui_runtime_bootstrap::State state{};
    state.load_settings = &load_settings;
    state.monitor_width = &g_settings.monitor_width;
    state.monitor_height = &g_settings.monitor_height;
    state.forced_monitor_width = 1920;
    state.forced_monitor_height = 1080;
    state.window_title = "RZ-1 Drum ROM Editor";
    state.window_out = &g_window;
    state.renderer_out = renderer_out;
    state.sdl_renderer_out = &g_sdl_renderer;
    return state;
}

drumrom::main_ui_runtime_startup::State make_runtime_startup_state() {
    drumrom::main_ui_runtime_startup::State state{};
    state.slot_count = g_slot_cfg.size();
    state.sample_rate = kSampleRate;
    state.set_slot_sample_rate = +[](std::size_t index, int sample_rate) {
        if (index < g_slot_cfg.size()) {
            g_slot_cfg[index].params.sample_rate = sample_rate;
        }
    };

    state.refresh_sample_folders = &refresh_sample_folders;
    state.refresh_loop_files = &refresh_loop_files;
    state.refresh_preset_folders = &refresh_preset_folders;
    state.load_layout_config = &load_layout_config;
    state.load_settings = &load_settings;
    state.load_default_rz1_kit_into_editor = &load_default_rz1_kit_into_editor;
    state.set_status = &set_status;

    state.history_clear = +[]() { g_history.clear(); };
    state.history_index = &g_history_index;
    state.history_initialized = &g_history_initialized;
    state.history_commit_pending = &g_history_commit_pending;
    state.initialize_history_if_needed = &initialize_history_if_needed;

    state.refresh_midi_in_ports = &refresh_midi_in_ports;
    state.refresh_midi_out_ports = &refresh_midi_out_ports;
    state.settings_midi_in_port_index = g_settings.midi_in_port_index;
    state.settings_midi_out_port_index = g_settings.midi_out_port_index;
    state.available_midi_in_port_count = +[]() { return static_cast<int>(g_available_midi_in_ports.size()); };
    state.available_midi_out_port_count = +[]() { return static_cast<int>(g_available_midi_out_ports.size()); };
    state.open_midi_in_port = &open_midi_in_port;
    state.open_midi_out_port = &open_midi_out_port;
    return state;
}

drumrom::main_ui_runtime_events::State make_runtime_event_state(bool* running) {
    drumrom::main_ui_runtime_events::State state{};
    state.running = running;
    state.window = g_window;
    state.window_resize_pending = &g_window_resize_pending;
    state.pending_window_width = &g_pending_window_width;
    state.pending_window_height = &g_pending_window_height;
    return state;
}

drumrom::main_ui_runtime_events::Actions make_runtime_event_actions() {
    return drumrom::main_ui_runtime_events::Actions{
        &selected_slot_is_synth_source,
        &select_slot,
        &perform_undo,
        &perform_redo,
        &copy_selected_slot_to_clipboard,
        &paste_slot_from_clipboard,
        &play_selected_slot_preview,
        &randomize_selected_slot_from_shortcut,
        &randomize_selected_reverb_from_shortcut,
        &set_selected_slot_drum_kind_from_index,
        &toggle_selected_slot_source_kind,
    };
}

drumrom::main_ui_runtime_frame::Actions make_runtime_frame_actions() {
    return drumrom::main_ui_runtime_frame::Actions{
        &apply_pending_snapshot_if_needed,
        &update_ui_scale_for_window,
        &poll_midi_input,
        &render_ui,
    };
}

drumrom::main_ui_runtime_cleanup::State make_runtime_cleanup_state(SDL_Renderer* renderer) {
    drumrom::main_ui_runtime_cleanup::State state{};
    state.drum_type_textures = &g_drum_type_textures;
    state.drum_icons_texture = &g_drum_icons_texture;
    state.sdl_renderer = &g_sdl_renderer;
    state.preview_audio_device = &g_preview_audio_device;
    state.renderer = renderer;
    state.window = g_window;
    state.close_midi_in_port = &close_midi_in_port;
    state.close_midi_out_port = &close_midi_out_port;
    state.clear_midi_owners = +[]() {
        g_midi_in = nullptr;
        g_midi_out = nullptr;
    };
    return state;
}
