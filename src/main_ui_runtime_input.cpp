// Keyboard shortcut decoding and mapping to editor/runtime actions.
#include "drumrom/main_ui_runtime_input.h"

namespace drumrom::main_ui_runtime_input {

ShortcutResult decode_shortcuts(SDL_Keycode key,
                                SDL_Keymod mods,
                                bool text_input,
                                bool selected_slot_is_synth) {
    ShortcutResult result{};
    const bool ctrl = (mods & KMOD_CTRL) != 0;
    const bool shift = (mods & KMOD_SHIFT) != 0;

    if (!ctrl && !text_input) {
        int slot_index = -1;
        switch (key) {
            case SDLK_a: slot_index = 0; break;
            case SDLK_s: slot_index = 1; break;
            case SDLK_d: slot_index = 2; break;
            case SDLK_f: slot_index = 3; break;
            case SDLK_g: slot_index = 4; break;
            case SDLK_h: slot_index = 5; break;
            case SDLK_j: slot_index = 6; break;
            case SDLK_k: slot_index = 7; break;
            case SDLK_z: slot_index = 8; break;
            case SDLK_x: slot_index = 9; break;
            case SDLK_c: slot_index = 10; break;
            case SDLK_v: slot_index = 11; break;
            case SDLK_b: slot_index = 12; break;
            case SDLK_n: slot_index = 13; break;
            case SDLK_m: slot_index = 14; break;
            case SDLK_COMMA: slot_index = 15; break;
            default: break;
        }
        if (slot_index >= 0) {
            result.handled = true;
            result.select_slot = true;
            result.slot_index = static_cast<std::size_t>(slot_index);
            return result;
        }
    }

    if (ctrl && !text_input && key == SDLK_z && shift) {
        result.handled = true;
        result.redo = true;
        return result;
    }
    if (ctrl && !text_input && key == SDLK_z) {
        result.handled = true;
        result.undo = true;
        return result;
    }
    if (ctrl && !text_input && key == SDLK_y) {
        result.handled = true;
        result.redo = true;
        return result;
    }
    if (ctrl && !text_input && key == SDLK_c) {
        result.handled = true;
        result.copy_slot = true;
        return result;
    }
    if (ctrl && !text_input && key == SDLK_v) {
        result.handled = true;
        result.paste_slot = true;
        return result;
    }
    if (ctrl && (key == SDLK_w || key == SDLK_q)) {
        result.handled = true;
        result.should_quit = true;
        return result;
    }
    if (!ctrl && !text_input && key == SDLK_SPACE) {
        result.handled = true;
        result.play_preview = true;
        return result;
    }
    if (!ctrl && !text_input && key == SDLK_r) {
        result.handled = true;
        result.randomize = true;
        return result;
    }
    if (!ctrl && !text_input && key == SDLK_y) {
        result.handled = true;
        result.randomize_reverb = true;
        return result;
    }
    if (!ctrl && !text_input && key == SDLK_t) {
        result.handled = true;
        result.toggle_source_kind = true;
        return result;
    }

    if (!ctrl && !text_input && selected_slot_is_synth) {
        int drum_kind_index = -1;
        switch (key) {
            case SDLK_1: drum_kind_index = 0; break;
            case SDLK_2: drum_kind_index = 1; break;
            case SDLK_3: drum_kind_index = 2; break;
            case SDLK_4: drum_kind_index = 3; break;
            case SDLK_5: drum_kind_index = 4; break;
            case SDLK_6: drum_kind_index = 5; break;
            case SDLK_7: drum_kind_index = 6; break;
            default: break;
        }
        if (drum_kind_index >= 0) {
            result.handled = true;
            result.set_drum_kind = true;
            result.drum_kind_index = drum_kind_index;
        }
    }

    return result;
}

}  // namespace drumrom::main_ui_runtime_input
