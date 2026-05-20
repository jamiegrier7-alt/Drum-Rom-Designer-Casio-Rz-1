#pragma once

#include <alsa/asoundlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace drumrom::main_ui_midi {

enum class SysexDumpState : int {
    Idle = 0,
    WaitingForAck,  // Sent 10/14 request; waiting for F0 44 01 00 70 30 ACK
    Receiving,      // Got ACK, sent 7n 31; collecting nibble data blocks
    Done,           // Received terminal F7
    Error,
};

inline const char* sysex_dump_state_name(SysexDumpState s) {
    switch (s) {
        case SysexDumpState::Idle:          return "Idle";
        case SysexDumpState::WaitingForAck: return "Waiting for ACK...";
        case SysexDumpState::Receiving:     return "Receiving...";
        case SysexDumpState::Done:          return "Done";
        case SysexDumpState::Error:         return "Error";
        default:                            return "?";
    }
}

// Forward declaration of Actions for use in State
struct Actions;

struct State {
    snd_rawmidi_t** midi_in = nullptr;
    std::vector<std::string>* available_midi_in_ports = nullptr;
    int* selected_midi_in_port = nullptr;
    bool* midi_in_enabled = nullptr;
    std::string* midi_in_status = nullptr;

    snd_rawmidi_t** midi_out = nullptr;
    std::vector<std::string>* available_midi_out_ports = nullptr;
    int* selected_midi_out_port = nullptr;
    bool* midi_out_enabled = nullptr;
    std::string* midi_out_status = nullptr;

    int* rz1_sysex_channel = nullptr;
    std::string* rz1_sysex_status = nullptr;
    std::vector<unsigned char>* rz1_sysex_capture = nullptr;
    std::size_t* rz1_sysex_message_count = nullptr;
    bool* rz1_sysex_overflow = nullptr;
    int* rz1_sysex_dump_delay_ms = nullptr;
    int* rz1_sysex_handshake_byte_delay_ms = nullptr;
    std::uint8_t* last_rz1_sysex_request = nullptr;
    char* rz1_sysex_save_path = nullptr;
    std::size_t rz1_sysex_capture_max_bytes = 0;

    std::array<std::int8_t, 128>* midi_note_velocity = nullptr;
    std::size_t* selected_slot = nullptr;
    std::size_t* last_midi_note_slot = nullptr;

    bool* midi_debug_monitor_enabled = nullptr;
    std::vector<std::string>* midi_debug_monitor_lines = nullptr;
    std::size_t midi_debug_monitor_max_lines = 0;

    SysexDumpState* sysex_dump_state = nullptr;
    Actions* actions = nullptr;  // Callbacks for UI integration
};

struct Actions {
    void (*set_status)(const std::string& msg) = nullptr;
    // Called when a dump completes to load split samples into editor slots
    // Parameters: array of 4 paths (sample1, sample2, sample3, sample4)
    void (*load_split_samples)(const std::array<std::string, 4>& sample_paths) = nullptr;
};

struct PollContext {
    bool editor_page_active = false;
    int slot_base_midi_note = 36;
    std::size_t slot_count = 0;
};

void refresh_midi_in_ports(State* state);
void open_midi_in_port(State* state, int port_index);
void close_midi_in_port(State* state);

void refresh_midi_out_ports(State* state);
void open_midi_out_port(State* state, int port_index);
void close_midi_out_port(State* state);

bool send_rz1_sysex_command(State* state, const Actions& actions, std::uint8_t command_zz, const char* command_name);
bool send_manual_rz1_handshake_31(State* state, const Actions& actions);
bool save_rz1_sysex_capture(State* state, const Actions& actions);
bool send_rz1_sysex_dump_to_rz1(State* state, const Actions& actions, bool is_sample);

void poll_midi_input(State* state, const PollContext& context);

}  // namespace drumrom::main_ui_midi
