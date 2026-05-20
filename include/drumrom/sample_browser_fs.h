#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace drumrom::sample_browser_fs {

bool path_within_root(const std::filesystem::path& candidate, const std::filesystem::path& root);

std::string folder_label(const std::string& folder, const std::filesystem::path& root);

std::string entry_label(
    const std::string& entry_path,
    const std::filesystem::path& current_folder,
    const std::filesystem::path& root);

void refresh_files_for_folder(
    const std::filesystem::path& root,
    std::string* selected_folder_path,
    std::vector<std::string>* current_folder_files,
    int* selected_sample_file);

void refresh_folders(
    const std::filesystem::path& root,
    std::vector<std::string>* sample_folders,
    int* selected_folder,
    std::string* selected_folder_path,
    std::vector<std::string>* current_folder_files,
    int* selected_sample_file);

}  // namespace drumrom::sample_browser_fs
