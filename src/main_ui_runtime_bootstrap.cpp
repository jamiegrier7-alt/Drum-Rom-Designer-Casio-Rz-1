// Initializes SDL, ImGui, audio, and runtime dependencies at application boot.
#include "drumrom/main_ui_runtime_bootstrap.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <filesystem>

namespace drumrom::main_ui_runtime_bootstrap {

bool initialize(State* state) {
    if (state == nullptr || state->window_out == nullptr || state->renderer_out == nullptr ||
        state->sdl_renderer_out == nullptr) {
        return false;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        return false;
    }

    if (state->load_settings != nullptr) {
        state->load_settings();
    }
    if (state->monitor_width != nullptr) {
        *state->monitor_width = state->forced_monitor_width;
    }
    if (state->monitor_height != nullptr) {
        *state->monitor_height = state->forced_monitor_height;
    }

    const int window_width = state->monitor_width ? *state->monitor_width : state->forced_monitor_width;
    const int window_height = state->monitor_height ? *state->monitor_height : state->forced_monitor_height;

    *state->window_out = SDL_CreateWindow(
        state->window_title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width,
        window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (*state->window_out == nullptr) {
        SDL_Quit();
        return false;
    }

    *state->renderer_out = SDL_CreateRenderer(
        *state->window_out,
        -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (*state->renderer_out == nullptr) {
        SDL_DestroyWindow(*state->window_out);
        *state->window_out = nullptr;
        SDL_Quit();
        return false;
    }

    *state->sdl_renderer_out = *state->renderer_out;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    std::error_code ec;
    std::filesystem::create_directories("settings", ec);
    io.IniFilename = "settings/imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(*state->window_out, *state->renderer_out);
    ImGui_ImplSDLRenderer2_Init(*state->renderer_out);

    return true;
}

}  // namespace drumrom::main_ui_runtime_bootstrap
