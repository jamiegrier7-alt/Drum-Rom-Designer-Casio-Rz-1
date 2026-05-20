#pragma once

#include <SDL2/SDL.h>

#include <cstddef>

namespace drumrom::main_ui_runtime_input {

struct ShortcutResult {
    bool handled = false;
    bool should_quit = false;

    bool select_slot = false;
    std::size_t slot_index = 0;

    bool undo = false;
    bool redo = false;
    bool copy_slot = false;
    bool paste_slot = false;
    bool play_preview = false;
    bool randomize = false;
    bool randomize_reverb = false;

    bool set_drum_kind = false;
    int drum_kind_index = 0;

    bool toggle_source_kind = false;
};

ShortcutResult decode_shortcuts(SDL_Keycode key,
                                SDL_Keymod mods,
                                bool text_input,
                                bool selected_slot_is_synth);

}  // namespace drumrom::main_ui_runtime_input
