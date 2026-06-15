#pragma once

#include <cstddef>
#include <vector>

namespace drumrom::app_core_history {

template <typename Snapshot>
void push_snapshot(std::vector<Snapshot>* history,
                   std::size_t* history_index,
                   bool* history_initialized,
                   bool history_applying,
                   const Snapshot& snapshot,
                   std::size_t max_entries = 256) {
    if (history == nullptr || history_index == nullptr || history_initialized == nullptr || max_entries == 0) {
        return;
    }
    if (history_applying) {
        return;
    }
    if (*history_initialized && *history_index + 1 < history->size()) {
        history->erase(history->begin() + static_cast<std::ptrdiff_t>(*history_index + 1), history->end());
    }
    history->push_back(snapshot);
    if (history->size() > max_entries) {
        history->erase(history->begin());
    }
    *history_index = history->empty() ? 0 : (history->size() - 1);
    *history_initialized = true;
}

template <typename Snapshot, typename CaptureFn>
void initialize_history_if_needed(std::vector<Snapshot>* history,
                                  std::size_t* history_index,
                                  bool* history_initialized,
                                  bool history_applying,
                                  CaptureFn capture_snapshot,
                                  std::size_t max_entries = 256) {
    if (history == nullptr || history_index == nullptr || history_initialized == nullptr || *history_initialized) {
        return;
    }
    push_snapshot(history, history_index, history_initialized, history_applying, capture_snapshot(), max_entries);
}

template <typename Snapshot, typename CaptureFn>
void maybe_commit_history(bool committed,
                          bool history_applying,
                          std::vector<Snapshot>* history,
                          std::size_t* history_index,
                          bool* history_initialized,
                          CaptureFn capture_snapshot,
                          std::size_t max_entries = 256) {
    if (!committed || history == nullptr || history_index == nullptr || history_initialized == nullptr) {
        return;
    }
    if (history_applying) {
        return;
    }
    initialize_history_if_needed(history,
                                 history_index,
                                 history_initialized,
                                 history_applying,
                                 capture_snapshot,
                                 max_entries);
    push_snapshot(history, history_index, history_initialized, history_applying, capture_snapshot(), max_entries);
}

template <typename Snapshot>
bool can_undo(const std::vector<Snapshot>& history,
              std::size_t history_index,
              bool history_initialized) {
    return history_initialized && !history.empty() && history_index > 0;
}

template <typename Snapshot>
bool can_redo(const std::vector<Snapshot>& history,
              std::size_t history_index,
              bool history_initialized) {
    return history_initialized && !history.empty() && (history_index + 1) < history.size();
}

template <typename Snapshot>
bool select_undo_target(const std::vector<Snapshot>& history,
                        std::size_t history_index,
                        bool history_initialized,
                        Snapshot* pending_snapshot,
                        std::size_t* pending_history_index,
                        bool* has_pending_snapshot) {
    if (pending_snapshot == nullptr || pending_history_index == nullptr || has_pending_snapshot == nullptr) {
        return false;
    }
    if (!can_undo(history, history_index, history_initialized)) {
        return false;
    }

    const std::size_t target_index = history_index - 1;
    *pending_snapshot = history[target_index];
    *pending_history_index = target_index;
    *has_pending_snapshot = true;
    return true;
}

template <typename Snapshot>
bool select_redo_target(const std::vector<Snapshot>& history,
                        std::size_t history_index,
                        bool history_initialized,
                        Snapshot* pending_snapshot,
                        std::size_t* pending_history_index,
                        bool* has_pending_snapshot) {
    if (pending_snapshot == nullptr || pending_history_index == nullptr || has_pending_snapshot == nullptr) {
        return false;
    }
    if (!can_redo(history, history_index, history_initialized)) {
        return false;
    }

    const std::size_t target_index = history_index + 1;
    *pending_snapshot = history[target_index];
    *pending_history_index = target_index;
    *has_pending_snapshot = true;
    return true;
}

}  // namespace drumrom::app_core_history
