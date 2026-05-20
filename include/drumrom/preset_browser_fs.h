#pragma once

#include <string>
#include <vector>

namespace drumrom::preset_browser_fs {

std::string current_root(bool show_kits_mode);

void refresh_files_for_root(
    bool show_kits_mode,
    std::vector<std::string>* current_preset_files,
    int* selected_preset_file);

void refresh_folders(
    std::vector<std::string>* preset_folders,
    int* selected_preset_folder,
    std::string* selected_preset_folder_path,
    bool show_kits_mode,
    std::vector<std::string>* current_preset_files,
    int* selected_preset_file);

std::string name_to_path(const std::string& name, bool is_kit);

std::string path_to_name(const std::string& path);

}  // namespace drumrom::preset_browser_fs
