// Per-frame runtime flow: begin frame, render UI, and present output.
#include "drumrom/main_ui_runtime_frame.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace drumrom::main_ui_runtime_frame {

void run(SDL_Renderer* renderer, const Actions& actions) {
    if (actions.apply_pending_snapshot != nullptr) {
        actions.apply_pending_snapshot();
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (actions.update_ui_scale != nullptr) {
        actions.update_ui_scale();
    }
    if (actions.poll_midi_input != nullptr) {
        actions.poll_midi_input();
    }
    if (actions.render_ui != nullptr) {
        actions.render_ui();
    }

    ImGui::Render();

    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
}

}  // namespace drumrom::main_ui_runtime_frame
