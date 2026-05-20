// Filesystem browser logic for listing, sorting, and filtering preset/kit files.
#include "drumrom/preset_browser_fs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace drumrom::preset_browser_fs {

namespace {

bool is_supported_preset_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".drum" || ext == ".kit" || ext == ".slotpreset" || ext == ".allslotpreset";
}

}  // namespace

std::string current_root(bool show_kits_mode) {
    return show_kits_mode ? "kits" : "presets";
}

void refresh_files_for_root(
    bool show_kits_mode,
    std::vector<std::string>* current_preset_files,
    int* selected_preset_file) {
    if (current_preset_files == nullptr || selected_preset_file == nullptr) {
        return;
    }

    current_preset_files->clear();
    *selected_preset_file = -1;

    const std::string root = current_root(show_kits_mode);
    if (!std::filesystem::exists(root)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!is_supported_preset_file(entry.path())) {
            continue;
        }
        current_preset_files->push_back(entry.path().string());
    }

    std::sort(current_preset_files->begin(), current_preset_files->end());
}

void refresh_folders(
    std::vector<std::string>* preset_folders,
    int* selected_preset_folder,
    std::string* selected_preset_folder_path,
    bool show_kits_mode,
    std::vector<std::string>* current_preset_files,
    int* selected_preset_file) {
    if (preset_folders == nullptr || selected_preset_folder == nullptr || selected_preset_folder_path == nullptr) {
        return;
    }

    preset_folders->clear();
    *selected_preset_folder = -1;
    selected_preset_folder_path->clear();

    std::filesystem::create_directories("presets");
    std::filesystem::create_directories("kits");

    preset_folders->push_back("presets");
    preset_folders->push_back("kits");

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("presets", ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory()) {
            preset_folders->push_back(entry.path().string());
        }
    }

    ec.clear();
    for (const auto& entry : std::filesystem::recursive_directory_iterator("kits", ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory()) {
            preset_folders->push_back(entry.path().string());
        }
    }

    std::sort(preset_folders->begin(), preset_folders->end());
    preset_folders->erase(std::unique(preset_folders->begin(), preset_folders->end()), preset_folders->end());

    refresh_files_for_root(show_kits_mode, current_preset_files, selected_preset_file);
}

std::string name_to_path(const std::string& name, bool is_kit) {
    std::string result = is_kit ? "kits/" : "presets/";
    result += name;
    const std::string ext = is_kit ? ".kit" : ".drum";

    if (result.size() >= 4) {
        const std::string suffix = result.substr(result.size() - 4);
        if (suffix == ".kit" || suffix == ".drum") {
            return result;
        }
        if (result.size() >= 11 && result.substr(result.size() - 11) == ".allslotpreset") {
            return result;
        }
        if (result.size() >= 11 && result.substr(result.size() - 11) == ".slotpreset") {
            return result;
        }
    }

    result += ext;
    return result;
}

std::string path_to_name(const std::string& path) {
    const std::filesystem::path p(path);
    std::string name = p.filename().string();
    const std::vector<std::string> extensions = {".kit", ".drum", ".allslotpreset", ".slotpreset"};
    for (const auto& ext : extensions) {
        if (name.size() >= ext.size() && name.substr(name.size() - ext.size()) == ext) {
            name.erase(name.length() - ext.size());
            return name;
        }
    }
    return name;
}

}  // namespace drumrom::preset_browser_fs
