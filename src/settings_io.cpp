// Loads and saves persistent application settings from/to disk.
#include "drumrom/settings_io.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace drumrom::settings_io {

namespace {

bool copy_json_string_value(const std::string& line, char* out, std::size_t out_capacity) {
    if (out == nullptr || out_capacity == 0) {
        return false;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    const std::size_t first_quote = line.find('"', colon);
    if (first_quote == std::string::npos) {
        return false;
    }
    const std::size_t second_quote = line.find('"', first_quote + 1);
    if (second_quote == std::string::npos || second_quote <= first_quote + 1) {
        return false;
    }
    const std::string value = line.substr(first_quote + 1, second_quote - first_quote - 1);
    std::strncpy(out, value.c_str(), out_capacity - 1);
    out[out_capacity - 1] = '\0';
    return true;
}

}  // namespace

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
    bool loop_split_autofit) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"midi_in_port_index\": " << midi_in_port_index << ",\n";
    file << "  \"midi_out_port_index\": " << midi_out_port_index << ",\n";
    file << "  \"samples_folder\": \"" << (samples_folder != nullptr ? samples_folder : "samples") << "\",\n";
    file << "  \"monitor_width\": " << monitor_width << ",\n";
    file << "  \"monitor_height\": " << monitor_height << ",\n";
    file << "  \"onerom_serial_rom_a\": \"" << (onerom_serial_rom_a != nullptr ? onerom_serial_rom_a : "") << "\",\n";
    file << "  \"onerom_serial_rom_b\": \"" << (onerom_serial_rom_b != nullptr ? onerom_serial_rom_b : "") << "\",\n";
    file << "  \"onerom_single_device_role\": " << onerom_single_device_role << ",\n";
    file << "  \"loop_split_reset_slots\": " << (loop_split_reset_slots ? 1 : 0) << ",\n";
    file << "  \"loop_split_target_pads\": " << loop_split_target_pads << ",\n";
    file << "  \"loop_split_autofit\": " << (loop_split_autofit ? 1 : 0) << "\n";
    file << "}\n";
    return true;
}

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
    bool* loop_split_autofit) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (midi_in_port_index != nullptr && line.find("midi_in_port_index") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", midi_in_port_index);
        } else if (midi_out_port_index != nullptr && line.find("midi_out_port_index") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", midi_out_port_index);
        } else if (samples_folder != nullptr && samples_folder_capacity > 0 && line.find("samples_folder") != std::string::npos) {
            (void)copy_json_string_value(line, samples_folder, samples_folder_capacity);
        } else if (monitor_width != nullptr && line.find("monitor_width") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", monitor_width);
        } else if (monitor_height != nullptr && line.find("monitor_height") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", monitor_height);
        } else if (onerom_serial_rom_a != nullptr && onerom_serial_rom_a_capacity > 0 && line.find("onerom_serial_rom_a") != std::string::npos) {
            (void)copy_json_string_value(line, onerom_serial_rom_a, onerom_serial_rom_a_capacity);
        } else if (onerom_serial_rom_b != nullptr && onerom_serial_rom_b_capacity > 0 && line.find("onerom_serial_rom_b") != std::string::npos) {
            (void)copy_json_string_value(line, onerom_serial_rom_b, onerom_serial_rom_b_capacity);
        } else if (onerom_single_device_role != nullptr && line.find("onerom_single_device_role") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", onerom_single_device_role);
        } else if (loop_split_reset_slots != nullptr && line.find("loop_split_reset_slots") != std::string::npos) {
            int value = 1;
            std::sscanf(line.c_str(), "%*[^:]: %d", &value);
            *loop_split_reset_slots = (value != 0);
        } else if (loop_split_target_pads != nullptr && line.find("loop_split_target_pads") != std::string::npos) {
            std::sscanf(line.c_str(), "%*[^:]: %d", loop_split_target_pads);
        } else if (loop_split_autofit != nullptr && line.find("loop_split_autofit") != std::string::npos) {
            int value = 0;
            std::sscanf(line.c_str(), "%*[^:]: %d", &value);
            *loop_split_autofit = (value != 0);
        }
    }
}

}  // namespace drumrom::settings_io
