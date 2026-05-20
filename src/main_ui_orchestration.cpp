// Coordinates slot triggering, preview playback, and shared UI orchestration state.
#include "drumrom/main_ui_orchestration.h"

#include <algorithm>

namespace drumrom::main_ui_orchestration {

namespace {

void render_status_line(const NonEditorPageModel& model) {
    if (model.status != nullptr && ImGui::GetTime() < model.status_expire_time) {
        ImGui::TextColored(model.status_color, "%s", model.status->c_str());
    }
}

}  // namespace (anonymous)

bool render_non_editor_page_if_active(const NonEditorPageModel& model, const NonEditorPageActions& actions) {
    const float separator_height = ImGui::GetFrameHeight() * 0.5f;
    const float status_height = ImGui::GetTextLineHeight();
    const float total_bottom_height = separator_height + status_height;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float content_height = avail.y - total_bottom_height;
    
    if (model.ui_page == model.settings_page) {
        ImGui::BeginChild("SettingsContent", ImVec2(0, content_height), false);
        if (actions.render_settings_page != nullptr) {
            actions.render_settings_page();
        }
        ImGui::EndChild();
        
        render_status_line(model);
        return true;
    }

    if (model.ui_page == model.pin_matrix_page) {
        ImGui::BeginChild("PinMatrixContent", ImVec2(0, content_height), false);
        if (actions.render_pin_matrix_page != nullptr) {
            actions.render_pin_matrix_page();
        }
        ImGui::EndChild();
        
        render_status_line(model);
        return true;
    }

    return false;
}

void apply_editor_change_flags(bool changed, ChangeClampState* state) {
    if (!changed || state == nullptr) {
        return;
    }

    if (state->start_pct != nullptr && state->end_pct != nullptr) {
        *state->start_pct = std::clamp(*state->start_pct, 0, 99);
        *state->end_pct = std::clamp(*state->end_pct, *state->start_pct + 1, 100);
    }
    if (state->loop_start_pct != nullptr && state->loop_end_pct != nullptr) {
        *state->loop_start_pct = std::clamp(*state->loop_start_pct, 0, 99);
        *state->loop_end_pct = std::clamp(*state->loop_end_pct, *state->loop_start_pct + 1, 100);
    }
    if (state->params_dirty != nullptr) {
        *state->params_dirty = true;
    }
    if (state->wave_preview_dirty != nullptr) {
        *state->wave_preview_dirty = true;
    }
    if (state->history_commit_pending != nullptr) {
        *state->history_commit_pending = true;
    }
}

void finalize_editor_commit_cycle(CommitCycleState* state, const CommitCycleActions& actions) {
    if (state == nullptr || state->auto_upload_commit_requested == nullptr || state->auto_play_commit_requested == nullptr ||
        state->history_commit_pending == nullptr) {
        return;
    }

    const bool history_commit = *state->auto_upload_commit_requested ||
        *state->auto_play_commit_requested ||
        (*state->history_commit_pending && !ImGui::IsAnyItemActive());

    if (actions.maybe_commit_history != nullptr) {
        actions.maybe_commit_history(history_commit);
    }
    if (history_commit) {
        *state->history_commit_pending = false;
    }

    if (actions.maybe_auto_upload_current_slot != nullptr) {
        actions.maybe_auto_upload_current_slot(*state->auto_upload_commit_requested);
    }
    if (actions.maybe_auto_play_current_slot != nullptr) {
        actions.maybe_auto_play_current_slot(*state->auto_play_commit_requested);
    }
    *state->auto_upload_commit_requested = false;
    *state->auto_play_commit_requested = false;
}

}  // namespace drumrom::main_ui_orchestration
