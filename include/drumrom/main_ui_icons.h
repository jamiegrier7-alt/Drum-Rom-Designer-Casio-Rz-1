#pragma once

#include "imgui.h"

#include <SDL2/SDL.h>

#include <array>

namespace drumrom::main_ui_icons {

struct State {
    SDL_Renderer* sdl_renderer = nullptr;

    bool* drum_type_textures_attempted = nullptr;
    std::array<SDL_Texture*, 5>* drum_type_textures = nullptr;

    bool* drum_icons_texture_attempted = nullptr;
    SDL_Texture** drum_icons_texture = nullptr;
    bool* drum_icons_uv_ready = nullptr;
    std::array<ImVec4, 16>* drum_icons_uvs = nullptr;
};

void ensure_drum_type_textures_loaded(State* state);
void ensure_drum_icons_texture_loaded(State* state);
void sprite_uv_for_tile(const State& state, int tile, ImVec2* uv0, ImVec2* uv1);

}  // namespace drumrom::main_ui_icons
