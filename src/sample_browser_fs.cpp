// Filesystem browser logic for listing, sorting, and filtering sample files.
#include "drumrom/sample_browser_fs.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <vector>

namespace drumrom::sample_browser_fs {

namespace {

bool is_supported_sample_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".raw";
}

}  // namespace

bool path_within_root(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(candidate.lexically_normal(), root.lexically_normal(), ec);
    if (ec) {
        return false;
    }
    if (rel.empty() || rel == ".") {
        return true;
    }
    const std::string rel_str = rel.generic_string();
    return rel_str != ".." && rel_str.rfind("../", 0) != 0;
}

std::string folder_label(const std::string& folder, const std::filesystem::path& root) {
    std::error_code ec;
    const std::filesystem::path path(folder);
    auto rel = std::filesystem::relative(path, root, ec);
    if (ec || rel.empty() || rel == ".") {
        const std::string root_name = root.filename().empty() ? root.string() : root.filename().string();
        return root_name.empty() ? "samples" : root_name;
    }
    return rel.string();
}

std::string entry_label(
    const std::string& entry_path,
    const std::filesystem::path& current_folder,
    const std::filesystem::path& root) {
    const std::filesystem::path entry(entry_path);
    if (entry == current_folder.parent_path() && current_folder != root) {
        return "..";
    }
    std::error_code ec;
    if (std::filesystem::is_directory(entry, ec)) {
        return entry.filename().string() + "/";
    }
    return entry.filename().string();
}

void refresh_files_for_folder(
    const std::filesystem::path& root,
    std::string* selected_folder_path,
    std::vector<std::string>* current_folder_files,
    int* selected_sample_file) {
    if (current_folder_files == nullptr || selected_folder_path == nullptr) {
        return;
    }

    current_folder_files->clear();
    if (selected_sample_file != nullptr) {
        *selected_sample_file = -1;
    }

    if (!std::filesystem::exists(root)) {
        return;
    }

    std::filesystem::path folder = selected_folder_path->empty()
        ? root
        : std::filesystem::path(*selected_folder_path).lexically_normal();
    if (!path_within_root(folder, root) || !std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        folder = root;
    }
    *selected_folder_path = folder.string();

    if (folder != root) {
        current_folder_files->push_back(folder.parent_path().string());
    }

    std::error_code ec;
    std::vector<std::string> directories;
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory()) {
            directories.push_back(entry.path().string());
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!is_supported_sample_file(entry.path())) {
            continue;
        }
        files.push_back(entry.path().string());
    }

    std::sort(directories.begin(), directories.end());
    std::sort(files.begin(), files.end());
    current_folder_files->insert(current_folder_files->end(), directories.begin(), directories.end());
    current_folder_files->insert(current_folder_files->end(), files.begin(), files.end());
}

void refresh_folders(
    const std::filesystem::path& root,
    std::vector<std::string>* sample_folders,
    int* selected_folder,
    std::string* selected_folder_path,
    std::vector<std::string>* current_folder_files,
    int* selected_sample_file) {
    if (sample_folders == nullptr || selected_folder == nullptr || selected_folder_path == nullptr) {
        return;
    }

    sample_folders->clear();
    *selected_folder = -1;
    selected_folder_path->clear();

    if (!std::filesystem::exists(root)) {
        if (current_folder_files != nullptr) {
            current_folder_files->clear();
        }
        if (selected_sample_file != nullptr) {
            *selected_sample_file = -1;
        }
        return;
    }

    sample_folders->push_back(root.string());
    *selected_folder = 0;
    *selected_folder_path = root.string();

    refresh_files_for_folder(root, selected_folder_path, current_folder_files, selected_sample_file);
}

}  // namespace drumrom::sample_browser_fs
