// Releases SDL, ImGui, audio, and runtime resources during shutdown.
#include "drumrom/main_ui_runtime_cleanup.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace drumrom::main_ui_runtime_cleanup {

void shutdown(State* state) {
    if (state == nullptr) {
        return;
    }

    if (state->drum_type_textures != nullptr) {
        for (SDL_Texture*& tex : *state->drum_type_textures) {
            if (tex != nullptr) {
                SDL_DestroyTexture(tex);
                tex = nullptr;
            }
        }
    }

    if (state->drum_icons_texture != nullptr && *state->drum_icons_texture != nullptr) {
        SDL_DestroyTexture(*state->drum_icons_texture);
        *state->drum_icons_texture = nullptr;
    }

    if (state->sdl_renderer != nullptr) {
        *state->sdl_renderer = nullptr;
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (state->preview_audio_device != nullptr && *state->preview_audio_device != 0) {
        SDL_CloseAudioDevice(*state->preview_audio_device);
        *state->preview_audio_device = 0;
    }

    if (state->close_midi_in_port != nullptr) {
        state->close_midi_in_port();
    }
    if (state->close_midi_out_port != nullptr) {
        state->close_midi_out_port();
    }
    if (state->clear_midi_owners != nullptr) {
        state->clear_midi_owners();
    }

    if (state->renderer != nullptr) {
        SDL_DestroyRenderer(state->renderer);
    }
    if (state->window != nullptr) {
        SDL_DestroyWindow(state->window);
    }
    SDL_Quit();
}

}  // namespace drumrom::main_ui_runtime_cleanup
