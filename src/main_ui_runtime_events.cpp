// Processes SDL events and forwards input to runtime/editor handlers.
#include "drumrom/main_ui_runtime_events.h"

#include "drumrom/main_ui_runtime_input.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"

namespace drumrom::main_ui_runtime_events {

void process_events(State* state, const Actions& actions) {
    if (state == nullptr || state->running == nullptr) {
        return;
    }

    if (state->window_resize_pending != nullptr && *state->window_resize_pending && state->window != nullptr &&
        state->pending_window_width != nullptr && state->pending_window_height != nullptr) {
        SDL_SetWindowSize(state->window, *state->pending_window_width, *state->pending_window_height);
        *state->window_resize_pending = false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
            *state->running = false;
            continue;
        }

        if (event.type != SDL_KEYDOWN || event.key.repeat != 0) {
            continue;
        }

        const SDL_Keycode key = event.key.keysym.sym;
        const SDL_Keymod mods = SDL_GetModState();
        const bool text_input = ImGui::GetIO().WantTextInput;
        const bool selected_slot_is_synth = actions.selected_slot_is_synth != nullptr
            ? actions.selected_slot_is_synth()
            : false;

        const auto shortcut = drumrom::main_ui_runtime_input::decode_shortcuts(
            key,
            mods,
            text_input,
            selected_slot_is_synth);
        if (!shortcut.handled) {
            continue;
        }

        if (shortcut.should_quit) {
            *state->running = false;
        } else if (shortcut.select_slot) {
            if (actions.select_slot != nullptr) {
                actions.select_slot(shortcut.slot_index, true);
            }
        } else if (shortcut.undo) {
            if (actions.perform_undo != nullptr) {
                actions.perform_undo();
            }
        } else if (shortcut.redo) {
            if (actions.perform_redo != nullptr) {
                actions.perform_redo();
            }
        } else if (shortcut.copy_slot) {
            if (actions.copy_slot != nullptr) {
                actions.copy_slot();
            }
        } else if (shortcut.paste_slot) {
            if (actions.paste_slot != nullptr) {
                actions.paste_slot();
            }
        } else if (shortcut.play_preview) {
            if (actions.play_preview != nullptr) {
                actions.play_preview();
            }
        } else if (shortcut.randomize) {
            if (actions.randomize_slot != nullptr) {
                actions.randomize_slot();
            }
        } else if (shortcut.randomize_reverb) {
            if (actions.randomize_reverb != nullptr) {
                actions.randomize_reverb();
            }
        } else if (shortcut.set_drum_kind) {
            if (actions.set_drum_kind_index != nullptr) {
                actions.set_drum_kind_index(shortcut.drum_kind_index);
            }
        } else if (shortcut.toggle_source_kind) {
            if (actions.toggle_source_kind != nullptr) {
                actions.toggle_source_kind();
            }
        }
    }
}

}  // namespace drumrom::main_ui_runtime_events
