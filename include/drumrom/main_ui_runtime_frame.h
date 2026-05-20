#pragma once

#include <SDL2/SDL.h>

namespace drumrom::main_ui_runtime_frame {

struct Actions {
    void (*apply_pending_snapshot)() = nullptr;
    void (*update_ui_scale)() = nullptr;
    void (*poll_midi_input)() = nullptr;
    void (*render_ui)() = nullptr;
};

void run(SDL_Renderer* renderer, const Actions& actions);

}  // namespace drumrom::main_ui_runtime_frame
