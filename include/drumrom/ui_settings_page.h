#pragma once

#include "imgui.h"

#include <cstddef>
#include <string>
#include <vector>

namespace drumrom::ui_settings_page {

struct Model {
    std::vector<std::string>* available_midi_in_ports;
    std::vector<std::string>* available_midi_out_ports;

    int* selected_midi_in_port;
    int* selected_midi_out_port;

    int* settings_midi_in_port_index;
    int* settings_midi_out_port_index;

    std::string* midi_in_status;
    std::string* midi_out_status;

    bool* midi_in_enabled;
    bool* midi_out_enabled;

    std::vector<std::string>* available_onerom_serials;
    int* selected_onerom_rom_a;
    int* selected_onerom_rom_b;
    int* onerom_single_device_role;
    char* onerom_serial_rom_a;
    std::size_t onerom_serial_rom_a_capacity;
    char* onerom_serial_rom_b;
    std::size_t onerom_serial_rom_b_capacity;
    std::string* onerom_usb_status;

    char* samples_folder;
    std::size_t samples_folder_capacity;

    int* monitor_width;
    int* monitor_height;

    bool* window_resize_pending;
    int* pending_window_width;
    int* pending_window_height;
    bool* loop_split_reset_slots;
    int* loop_split_target_pads;
    bool* loop_split_autofit;

    float ui_scale;
    ImVec4 status_color;
};

struct Actions {
    void (*refresh_midi_in_ports)();
    void (*open_midi_in_port)(int port_index);
    void (*close_midi_in_port)();

    void (*refresh_midi_out_ports)();
    void (*open_midi_out_port)(int port_index);
    void (*close_midi_out_port)();

    void (*refresh_onerom_usb_devices)();

    void (*save_settings)();
    bool (*restore_default_rz1_kit_file)();
    bool (*load_default_rz1_kit_into_editor)();

    void (*set_status)(const std::string& msg);
    void (*initialize_history_if_needed)();
    void (*maybe_commit_history)(bool committed);
};

void render(Model* model, const Actions& actions);

}  // namespace drumrom::ui_settings_page
