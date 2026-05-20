#pragma once

#include "imgui.h"

#include <string>

namespace drumrom::main_ui_orchestration {

struct NonEditorPageModel {
    int ui_page = 0;
    int editor_page = 0;
    int pin_matrix_page = 1;
    int settings_page = 2;
    double status_expire_time = 0.0;
    const std::string* status = nullptr;
    ImVec4 status_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float ui_scale = 1.0f;
};

struct NonEditorPageActions {
    void (*render_settings_page)() = nullptr;
    void (*render_pin_matrix_page)() = nullptr;
    void (*set_page)(int page_index) = nullptr;
};

bool render_non_editor_page_if_active(const NonEditorPageModel& model, const NonEditorPageActions& actions);

struct ChangeClampState {
    int* start_pct = nullptr;
    int* end_pct = nullptr;
    int* loop_start_pct = nullptr;
    int* loop_end_pct = nullptr;
    bool* params_dirty = nullptr;
    bool* wave_preview_dirty = nullptr;
    bool* history_commit_pending = nullptr;
};

void apply_editor_change_flags(bool changed, ChangeClampState* state);

struct CommitCycleState {
    bool* auto_upload_commit_requested = nullptr;
    bool* auto_play_commit_requested = nullptr;
    bool* history_commit_pending = nullptr;
};

struct CommitCycleActions {
    void (*maybe_commit_history)(bool committed) = nullptr;
    void (*maybe_auto_upload_current_slot)(bool committed) = nullptr;
    void (*maybe_auto_play_current_slot)(bool committed) = nullptr;
};

void finalize_editor_commit_cycle(CommitCycleState* state, const CommitCycleActions& actions);

}  // namespace drumrom::main_ui_orchestration
