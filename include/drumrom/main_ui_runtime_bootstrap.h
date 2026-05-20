#pragma once

#include <SDL2/SDL.h>

namespace drumrom::main_ui_runtime_bootstrap {

struct State {
    void (*load_settings)() = nullptr;
    int* monitor_width = nullptr;
    int* monitor_height = nullptr;
    int forced_monitor_width = 1920;
    int forced_monitor_height = 1080;

    const char* window_title = "RZ-1 Drum ROM Editor";
    SDL_Window** window_out = nullptr;
    SDL_Renderer** renderer_out = nullptr;
    SDL_Renderer** sdl_renderer_out = nullptr;
};

bool initialize(State* state);

}  // namespace drumrom::main_ui_runtime_bootstrap
