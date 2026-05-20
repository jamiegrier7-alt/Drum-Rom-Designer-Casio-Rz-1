#pragma once

#include <cstddef>
#include <string>

namespace drumrom::main_ui_runtime_startup {

struct State {
    std::size_t slot_count = 0;
    int sample_rate = 0;
    void (*set_slot_sample_rate)(std::size_t index, int sample_rate) = nullptr;

    void (*refresh_sample_folders)() = nullptr;
    void (*refresh_loop_files)() = nullptr;
    void (*refresh_preset_folders)() = nullptr;
    void (*load_layout_config)() = nullptr;
    void (*load_settings)() = nullptr;
    bool (*load_default_rz1_kit_into_editor)() = nullptr;
    void (*set_status)(const std::string& message) = nullptr;

    void (*history_clear)() = nullptr;
    std::size_t* history_index = nullptr;
    bool* history_initialized = nullptr;
    bool* history_commit_pending = nullptr;
    void (*initialize_history_if_needed)() = nullptr;

    void (*refresh_midi_in_ports)() = nullptr;
    void (*refresh_midi_out_ports)() = nullptr;
    int settings_midi_in_port_index = -1;
    int settings_midi_out_port_index = -1;
    int (*available_midi_in_port_count)() = nullptr;
    int (*available_midi_out_port_count)() = nullptr;
    void (*open_midi_in_port)(int port_index) = nullptr;
    void (*open_midi_out_port)(int port_index) = nullptr;
};

void perform_startup(State* state);

}  // namespace drumrom::main_ui_runtime_startup
