#pragma once

#include <cstddef>
#include <string>

namespace drumrom::settings_io {

bool save_settings_file(
    const std::string& path,
    int midi_in_port_index,
    int midi_out_port_index,
    const char* samples_folder,
    int monitor_width,
    int monitor_height,
    const char* onerom_serial_rom_a,
    const char* onerom_serial_rom_b,
    int onerom_single_device_role,
    bool loop_split_reset_slots,
    int loop_split_target_pads,
    bool loop_split_autofit);

void load_settings_file(
    const std::string& path,
    int* midi_in_port_index,
    int* midi_out_port_index,
    char* samples_folder,
    std::size_t samples_folder_capacity,
    int* monitor_width,
    int* monitor_height,
    char* onerom_serial_rom_a,
    std::size_t onerom_serial_rom_a_capacity,
    char* onerom_serial_rom_b,
    std::size_t onerom_serial_rom_b_capacity,
    int* onerom_single_device_role,
    bool* loop_split_reset_slots,
    int* loop_split_target_pads,
    bool* loop_split_autofit);

}  // namespace drumrom::settings_io
