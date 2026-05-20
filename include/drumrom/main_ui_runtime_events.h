#pragma once

#include <SDL2/SDL.h>

#include <cstddef>

namespace drumrom::main_ui_runtime_events {

struct State {
    bool* running = nullptr;
    SDL_Window* window = nullptr;
    bool* window_resize_pending = nullptr;
    int* pending_window_width = nullptr;
    int* pending_window_height = nullptr;
};

struct Actions {
    bool (*selected_slot_is_synth)() = nullptr;

    void (*select_slot)(std::size_t slot_index, bool play_preview) = nullptr;
    void (*perform_undo)() = nullptr;
    void (*perform_redo)() = nullptr;
    void (*copy_slot)() = nullptr;
    void (*paste_slot)() = nullptr;
    void (*play_preview)() = nullptr;
    void (*randomize_slot)() = nullptr;
    void (*randomize_reverb)() = nullptr;
    void (*set_drum_kind_index)(int drum_kind_index) = nullptr;
    void (*toggle_source_kind)() = nullptr;
};

void process_events(State* state, const Actions& actions);

}  // namespace drumrom::main_ui_runtime_events
