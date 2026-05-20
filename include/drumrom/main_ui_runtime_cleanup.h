#pragma once

#include <SDL2/SDL.h>

#include <array>

namespace drumrom::main_ui_runtime_cleanup {

struct State {
    std::array<SDL_Texture*, 5>* drum_type_textures = nullptr;
    SDL_Texture** drum_icons_texture = nullptr;
    SDL_Renderer** sdl_renderer = nullptr;
    SDL_AudioDeviceID* preview_audio_device = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;

    void (*close_midi_in_port)() = nullptr;
    void (*close_midi_out_port)() = nullptr;
    void (*clear_midi_owners)() = nullptr;
};

void shutdown(State* state);

}  // namespace drumrom::main_ui_runtime_cleanup
