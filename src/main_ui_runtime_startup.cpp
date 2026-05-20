// Runs one-time startup initialization and first-frame setup tasks.
#include "drumrom/main_ui_runtime_startup.h"

namespace drumrom::main_ui_runtime_startup {

void perform_startup(State* state) {
    if (state == nullptr) {
        return;
    }

    if (state->set_slot_sample_rate != nullptr) {
        for (std::size_t i = 0; i < state->slot_count; ++i) {
            state->set_slot_sample_rate(i, state->sample_rate);
        }
    }

    if (state->refresh_sample_folders != nullptr) {
        state->refresh_sample_folders();
    }
    if (state->refresh_loop_files != nullptr) {
        state->refresh_loop_files();
    }
    if (state->refresh_preset_folders != nullptr) {
        state->refresh_preset_folders();
    }
    if (state->load_layout_config != nullptr) {
        state->load_layout_config();
    }
    if (state->load_settings != nullptr) {
        state->load_settings();
    }

    if (state->load_default_rz1_kit_into_editor != nullptr && state->set_status != nullptr) {
        if (state->load_default_rz1_kit_into_editor()) {
            state->set_status("Loaded default kit: default-rz1-kit.kit");
        } else {
            state->set_status("Failed to load default-rz1-kit.kit");
        }
    }

    if (state->history_clear != nullptr) {
        state->history_clear();
    }
    if (state->history_index != nullptr) {
        *state->history_index = 0;
    }
    if (state->history_initialized != nullptr) {
        *state->history_initialized = false;
    }
    if (state->history_commit_pending != nullptr) {
        *state->history_commit_pending = false;
    }
    if (state->initialize_history_if_needed != nullptr) {
        state->initialize_history_if_needed();
    }

    if (state->refresh_midi_in_ports != nullptr) {
        state->refresh_midi_in_ports();
    }
    if (state->refresh_midi_out_ports != nullptr) {
        state->refresh_midi_out_ports();
    }

    const int midi_in_count = state->available_midi_in_port_count ? state->available_midi_in_port_count() : 0;
    if (state->open_midi_in_port != nullptr && state->settings_midi_in_port_index >= 0 &&
        state->settings_midi_in_port_index < midi_in_count) {
        state->open_midi_in_port(state->settings_midi_in_port_index);
    }

    const int midi_out_count = state->available_midi_out_port_count ? state->available_midi_out_port_count() : 0;
    if (state->open_midi_out_port != nullptr && state->settings_midi_out_port_index >= 0 &&
        state->settings_midi_out_port_index < midi_out_count) {
        state->open_midi_out_port(state->settings_midi_out_port_index);
    }
}

}  // namespace drumrom::main_ui_runtime_startup
