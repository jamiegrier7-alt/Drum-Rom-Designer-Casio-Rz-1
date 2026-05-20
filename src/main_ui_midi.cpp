// MIDI I/O management, note routing, and RZ-1 SysEx send/capture/handshake logic.
#include "drumrom/main_ui_midi.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace drumrom::main_ui_midi {

namespace {
constexpr int kDefaultRz1DumpInterMessageDelayMs = 200;
constexpr int kMaxRz1DumpInterMessageDelayMs = 10000;
constexpr int kDefaultRz1HandshakeByteDelayMs = 2;
constexpr int kMaxRz1HandshakeByteDelayMs = 100;
constexpr int kRz1PeriodicHandshakeMs = 270;
constexpr unsigned char kRz1HandshakeRequestToSend = 0x31u;
constexpr unsigned char kRz1HandshakeMoreToSend = 0x32u;

struct PendingOutgoingMessage {
    std::vector<unsigned char> bytes;
    bool handshake = false;
    std::chrono::steady_clock::time_point due_time{};
};

std::deque<PendingOutgoingMessage> g_outgoing_queue;
std::chrono::steady_clock::time_point g_next_outgoing_due_time{};
bool g_outgoing_schedule_active = false;
bool g_rz1_initial_31_sent_for_send = false;
bool g_rz1_periodic_handshake_active = false;
std::chrono::steady_clock::time_point g_rz1_last_handshake_send_time{};

// ── ALSA rawmidi port enumeration ────────────────────────────────────────────
std::vector<std::string> g_rawmidi_in_hw_ids;
std::vector<std::string> g_rawmidi_out_hw_ids;

void enumerate_rawmidi_ports(
    std::vector<std::string>& names,
    std::vector<std::string>& hw_ids,
    bool input) {
    names.clear();
    hw_ids.clear();

    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctl_name[32];
        std::snprintf(ctl_name, sizeof(ctl_name), "hw:%d", card);
        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctl_name, 0) < 0) { continue; }

        int device = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &device) == 0 && device >= 0) {
            snd_rawmidi_info_t* info = nullptr;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, static_cast<unsigned int>(device));
            snd_rawmidi_info_set_stream(info,
                input ? SND_RAWMIDI_STREAM_INPUT : SND_RAWMIDI_STREAM_OUTPUT);
            snd_rawmidi_info_set_subdevice(info, 0);

            if (snd_ctl_rawmidi_info(ctl, info) < 0) { continue; }

            const int subcount = static_cast<int>(snd_rawmidi_info_get_subdevices_count(info));
            for (int sub = 0; sub < subcount; ++sub) {
                snd_rawmidi_info_set_subdevice(info, static_cast<unsigned int>(sub));
                if (sub > 0 && snd_ctl_rawmidi_info(ctl, info) < 0) { continue; }

                char hw_id[64];
                std::snprintf(hw_id, sizeof(hw_id), "hw:%d,%d,%d", card, device, sub);

                std::string name = snd_rawmidi_info_get_name(info);
                if (subcount > 1) {
                    name += std::string(" [") + snd_rawmidi_info_get_subdevice_name(info) + "]";
                }
                names.push_back(std::move(name));
                hw_ids.push_back(hw_id);
            }
        }
        snd_ctl_close(ctl);
    }
}

// ── MIDI byte-stream parser ───────────────────────────────────────────────────
// Converts raw ALSA rawmidi bytes into discrete MIDI messages.
// Handles channel messages, SysEx F0..F7, real-time single-byte messages,
// and Casio RZ-1 proprietary 2-byte handshake pairs (7n 31 / 7n 32).
struct RawMidiParser {
    enum class InState { Idle, SysEx, CasioHigh, ChannelMsg };
    InState in_state = InState::Idle;
    std::vector<unsigned char> accum;
    int channel_bytes_remaining = 0;
};

RawMidiParser g_midi_parser;

// ── Raw sample-dump stream receiver ─────────────────────────────────────────
// Processes incoming bytes in real time (before the MIDI parser reassembles
// them), strips the 6-byte header, filters embedded 70 XX handshake signals,
// accumulates nibblized audio data, and sends 70 31 replies as needed.
struct RawSampleReceiver {
    bool active           = false;
    bool header_consumed  = false;  // have we eaten F0 44 01 00 70 30?
    int  header_pos       = 0;      // how many header bytes matched so far
    bool eat_next         = false;  // previous byte was 0x7n — eat the follower byte
    unsigned char marker_prefix = 0x70u; // remember 0x7n for reply channel consistency
    std::size_t data_byte_count = 0; // raw data bytes (excluding 6-byte header)
    static constexpr std::size_t kBlockSize = 256;
    static constexpr std::size_t kFirstBlockDataSize = 250;  // 256 - 6 header
    static constexpr std::array<unsigned char, 6> kHeader{0xF0,0x44,0x01,0x00,0x70,0x30};
};
RawSampleReceiver g_raw_receiver;

int channel_msg_length(unsigned char status) {
    const unsigned char type = status & 0xF0u;
    if (type == 0xC0u || type == 0xD0u) { return 2; }
    return 3;
}

std::vector<std::vector<unsigned char>> parse_midi_bytes(
    RawMidiParser& parser,
    const unsigned char* buf,
    std::size_t len) {
    std::vector<std::vector<unsigned char>> messages;

    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char b = buf[i];

        // Real-time single-byte messages (0xF8-0xFF) can appear anywhere
        if (b >= 0xF8u) {
            messages.push_back({b});
            continue;
        }

        switch (parser.in_state) {
        case RawMidiParser::InState::SysEx:
            parser.accum.push_back(b);
            if (b == 0xF7u) {
                messages.push_back(std::move(parser.accum));
                parser.accum.clear();
                parser.in_state = RawMidiParser::InState::Idle;
            } else if (b & 0x80u) {
                // Unexpected status byte mid-SysEx — abort and reprocess
                parser.accum.clear();
                parser.in_state = RawMidiParser::InState::Idle;
                --i;
            }
            break;

        case RawMidiParser::InState::CasioHigh:
            // Second byte of a Casio 7n pair
            parser.accum.push_back(b);
            messages.push_back(std::move(parser.accum));
            parser.accum.clear();
            parser.in_state = RawMidiParser::InState::Idle;
            break;

        case RawMidiParser::InState::ChannelMsg:
            parser.accum.push_back(b);
            if (--parser.channel_bytes_remaining == 0) {
                messages.push_back(std::move(parser.accum));
                parser.accum.clear();
                parser.in_state = RawMidiParser::InState::Idle;
            }
            break;

        case RawMidiParser::InState::Idle:
        default:
            if (b == 0xF0u) {
                parser.accum.clear();
                parser.accum.push_back(b);
                parser.in_state = RawMidiParser::InState::SysEx;
            } else if ((b & 0xF0u) == 0x70u) {
                // Casio RZ-1 proprietary 2-byte pair: 7n followed by one byte
                parser.accum.clear();
                parser.accum.push_back(b);
                parser.in_state = RawMidiParser::InState::CasioHigh;
            } else if (b >= 0x80u) {
                parser.accum.clear();
                parser.accum.push_back(b);
                parser.channel_bytes_remaining = channel_msg_length(b) - 1;
                if (parser.channel_bytes_remaining == 0) {
                    messages.push_back(std::move(parser.accum));
                    parser.accum.clear();
                    parser.in_state = RawMidiParser::InState::Idle;
                } else {
                    parser.in_state = RawMidiParser::InState::ChannelMsg;
                }
            }
            // Orphan data bytes (running status) — ignored
            break;
        }
    }
    return messages;
}

bool send_handshake_message(State* state, unsigned char first, unsigned char second);
void disarm_rz1_receive_poll();
void note_rz1_receive_activity();

// ── Sample splitting and loader ──────────────────────────────────────────────
// Splits dumped nibble data into 4 equal parts and saves as raw 8-bit samples
static std::array<std::string, 4> split_and_save_dump_samples(
    const std::vector<unsigned char>& capture_data) {
    std::array<std::string, 4> result{"", "", "", ""};
    if (capture_data.empty()) {
        return result;
    }

    // Capture stream may include in-band 7n XX handshakes and terminal F7.
    // Keep only nibble payload bytes (0x00..0x0F) for audio reconstruction.
    std::vector<unsigned char> nibble_data;
    nibble_data.reserve(capture_data.size());
    for (std::size_t i = 0; i < capture_data.size(); ++i) {
        const unsigned char b = capture_data[i];
        if (b == 0xF7u) {
            continue;
        }
        if ((b & 0xF0u) == 0x70u && i + 1 < capture_data.size()) {
            ++i; // eat 7n handshake follower (31/32/etc)
            continue;
        }
        if (b <= 0x0Fu) {
            nibble_data.push_back(b);
        }
    }

    if (nibble_data.empty()) {
        return result;
    }

    // Convert nibbles to signed 8-bit bytes (pairs of nibbles)
    // Each nibble pair becomes one byte. Two nibbles per byte.
    std::vector<std::int8_t> byte_data;
    for (std::size_t i = 0; i + 1 < nibble_data.size(); i += 2) {
        const unsigned char high = nibble_data[i];
        const unsigned char low = nibble_data[i + 1];
        // Combine nibbles: high is upper 4 bits, low is lower 4 bits
        // Convert to signed 8-bit: 0x00-0x0F is for 4-bit values, may need proper mapping
        // For now, just combine them as: (high << 4) | low, then treat as signed
        const unsigned char combined = (high << 4) | low;
        byte_data.push_back(static_cast<std::int8_t>(combined));
    }

    if (byte_data.empty()) {
        return result;
    }

    // RZ-1 sample RAM is 16,384 bytes total: pads 1-3 are 4,096 each,
    // and pad 4 is slightly shorter because the last RAM byte stores link flags.
    // Keep split compatible with this hardware layout.
    constexpr std::array<std::size_t, 4> kPadSizes = {4096u, 4096u, 4096u, 4095u};
    constexpr std::size_t kRequiredBytes = 16383u;
    if (byte_data.size() < kRequiredBytes) {
        return result;
    }

    // Create samples directory if it doesn't exist
    std::filesystem::path samples_dir("samples");
    std::error_code ec;
    std::filesystem::create_directories(samples_dir, ec);

    // Save each part to a file
    const std::array<const char*, 4> sample_names = {
        "dump_sample1.raw", "dump_sample2.raw", "dump_sample3.raw", "dump_sample4.raw"
    };

    std::size_t cursor = 0;
    for (std::size_t part = 0; part < 4; ++part) {
        const std::size_t part_size = kPadSizes[part];
        const std::size_t start = cursor;
        const std::size_t end = std::min(start + part_size, byte_data.size());
        cursor = end;

        const std::filesystem::path out_path = samples_dir / sample_names[part];

        std::ofstream out(out_path, std::ios::binary);
        if (out.is_open()) {
            out.write(reinterpret_cast<const char*>(byte_data.data() + start),
                      static_cast<std::streamsize>(end - start));
            if (out.good()) {
                result[part] = out_path.string();
            }
        }
    }

    return result;
}

// ── Raw-stream sample receiver ───────────────────────────────────────────────
static void process_raw_sample_bytes(
    State* state, const unsigned char* buf, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char b = buf[i];

        // ── 1. Consume the opening 6-byte header ────────────────────────────
        if (!g_raw_receiver.header_consumed) {
            if (b == RawSampleReceiver::kHeader[static_cast<std::size_t>(g_raw_receiver.header_pos)]) {
                ++g_raw_receiver.header_pos;
                if (g_raw_receiver.header_pos == 6) {
                    g_raw_receiver.header_consumed = true;
                    // Transition: WaitingForAck → Receiving
                    if (state->sysex_dump_state != nullptr) {
                        *state->sysex_dump_state = SysexDumpState::Receiving;
                    }
                    *state->rz1_sysex_status = "RZ-1 ACK/header received — streaming sample data...";
                    note_rz1_receive_activity();
                }
            } else {
                // byte doesn't match; restart match attempt from this byte
                g_raw_receiver.header_pos = 0;
                if (b == RawSampleReceiver::kHeader[0]) {
                    g_raw_receiver.header_pos = 1;
                }
            }
            continue;
        }

        // ── 2. Count data bytes (periodic 70 31 pacing is handled elsewhere) ──
        ++g_raw_receiver.data_byte_count;

        // ── 3. Consume second byte of a 70 XX pair ───────────────────────────
        if (g_raw_receiver.eat_next) {
            const unsigned char marker = b;
            g_raw_receiver.eat_next = false;

            // Preserve in-band handshake payload in capture.
            note_rz1_receive_activity();
            if (state->rz1_sysex_capture->size() < state->rz1_sysex_capture_max_bytes) {
                state->rz1_sysex_capture->push_back(marker);
            } else {
                *state->rz1_sysex_overflow = true;
            }

            if (marker == kRz1HandshakeMoreToSend && state->sysex_dump_state != nullptr &&
                (*state->sysex_dump_state == SysexDumpState::WaitingForAck ||
                 *state->sysex_dump_state == SysexDumpState::Receiving)) {
                if (send_handshake_message(state, g_raw_receiver.marker_prefix, kRz1HandshakeRequestToSend)) {
                    g_rz1_last_handshake_send_time = std::chrono::steady_clock::now();
                }
            }
            continue;
        }

        // ── 4. End of transfer ───────────────────────────────────────────────
        if (b == 0xF7u) {
            // Preserve terminal F7 so saved capture can be reconstructed exactly.
            if (state->rz1_sysex_capture->size() < state->rz1_sysex_capture_max_bytes) {
                state->rz1_sysex_capture->push_back(0xF7u);
            } else {
                *state->rz1_sysex_overflow = true;
            }

            if (state->sysex_dump_state != nullptr) {
                *state->sysex_dump_state = SysexDumpState::Done;
            }
            disarm_rz1_receive_poll();
            *state->rz1_sysex_status = "Sample dump complete — " +
                std::to_string(state->rz1_sysex_capture->size()) + " bytes captured (header stripped)";
            
            // Send final 70 31 handshake to exit dump mode on hardware
            // (Per protocol.txt: required to gracefully exit hardware dump state)
            send_handshake_message(state, 0x70u, kRz1HandshakeRequestToSend);
            
            // Automatically split captured data into 4 samples and load them
            if (state->rz1_sysex_capture != nullptr && !state->rz1_sysex_capture->empty()) {
                const auto split_paths = split_and_save_dump_samples(*state->rz1_sysex_capture);
                if (split_paths[0] != "" && state->actions != nullptr && state->actions->load_split_samples != nullptr) {
                    state->actions->load_split_samples(split_paths);
                    *state->rz1_sysex_status += " — split into 4 samples and loaded into editor";
                }
            }
            
            g_raw_receiver = RawSampleReceiver{};
            return;
        }

        // ── 5. In-band handshake signal (70 XX) ─────────────────────────────
        // 0x7n is never a valid nibble (nibbles are 0x00–0x0F), so it is safe
        // to treat any 0x7n as an in-band control pair prefix.
        if ((b & 0xF0u) == 0x70u) {
            // Preserve handshake prefix in capture stream.
            note_rz1_receive_activity();
            if (state->rz1_sysex_capture->size() < state->rz1_sysex_capture_max_bytes) {
                state->rz1_sysex_capture->push_back(b);
            } else {
                *state->rz1_sysex_overflow = true;
            }

            g_raw_receiver.marker_prefix = b;
            g_raw_receiver.eat_next = true;
            continue;
        }

        // ── 6. Sample nibble data ────────────────────────────────────────────
        note_rz1_receive_activity();
        if (state->rz1_sysex_capture->size() < state->rz1_sysex_capture_max_bytes) {
            state->rz1_sysex_capture->push_back(b);
        } else {
            *state->rz1_sysex_overflow = true;
        }
    }

    if (g_raw_receiver.header_consumed && state->rz1_sysex_capture != nullptr &&
        !state->rz1_sysex_capture->empty()) {
        *state->rz1_sysex_status = "Receiving: " +
            std::to_string(state->rz1_sysex_capture->size()) + " bytes";
    }
}

std::vector<unsigned char> make_handshake_message(unsigned char first, unsigned char second) {
    return {first, second};
}

void arm_rz1_receive_poll() {
}

void purge_queued_handshake_messages() {
    if (g_outgoing_queue.empty()) {
        return;
    }

    g_outgoing_queue.erase(
        std::remove_if(g_outgoing_queue.begin(), g_outgoing_queue.end(),
            [](const PendingOutgoingMessage& pending) { return pending.handshake; }),
        g_outgoing_queue.end());

    if (g_outgoing_queue.empty()) {
        g_outgoing_schedule_active = false;
    }
}

void disarm_rz1_receive_poll() {
    g_rz1_initial_31_sent_for_send = false;
    g_rz1_periodic_handshake_active = false;
    g_rz1_last_handshake_send_time = std::chrono::steady_clock::time_point{};
    purge_queued_handshake_messages();
}

void note_rz1_receive_activity() {
}

void maybe_send_periodic_handshake(State* state) {
    if (state == nullptr || !g_rz1_periodic_handshake_active ||
        state->midi_out == nullptr || state->midi_out_enabled == nullptr ||
        *state->midi_out == nullptr || !*state->midi_out_enabled ||
        state->sysex_dump_state == nullptr ||
        (*state->sysex_dump_state != SysexDumpState::WaitingForAck &&
         *state->sysex_dump_state != SysexDumpState::Receiving)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (g_rz1_last_handshake_send_time.time_since_epoch().count() == 0) {
        g_rz1_last_handshake_send_time = now;
        return;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - g_rz1_last_handshake_send_time).count();
    const int periodic_ms = std::clamp(
        state->rz1_sysex_dump_delay_ms != nullptr
            ? *state->rz1_sysex_dump_delay_ms
            : kRz1PeriodicHandshakeMs,
        50,
        2000);
    if (elapsed_ms < periodic_ms) {
        return;
    }

    if (send_handshake_message(state, 0x70u, kRz1HandshakeRequestToSend)) {
        g_rz1_last_handshake_send_time = now;
    }
}

int get_outgoing_delay_ms(const State* state) {
    return std::clamp(
        state != nullptr && state->rz1_sysex_dump_delay_ms != nullptr
            ? *state->rz1_sysex_dump_delay_ms
            : kDefaultRz1DumpInterMessageDelayMs,
        0,
        kMaxRz1DumpInterMessageDelayMs);
}

int get_handshake_byte_delay_ms(const State* state) {
    return std::clamp(
        state != nullptr && state->rz1_sysex_handshake_byte_delay_ms != nullptr
            ? *state->rz1_sysex_handshake_byte_delay_ms
            : kDefaultRz1HandshakeByteDelayMs,
        0,
        kMaxRz1HandshakeByteDelayMs);
}

void append_midi_debug_line(State* state, const char* direction, const std::vector<unsigned char>& message) {
    if (state == nullptr || state->midi_debug_monitor_enabled == nullptr ||
        state->midi_debug_monitor_lines == nullptr || !*state->midi_debug_monitor_enabled ||
        message.empty()) {
        return;
    }

    std::ostringstream oss;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    oss << now_ms << "ms " << (direction != nullptr ? direction : "MIDI") << " [" << message.size() << "b]:";
    oss << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char b : message) {
        oss << ' ' << std::setw(2) << static_cast<int>(b);
    }

    state->midi_debug_monitor_lines->push_back(oss.str());
    const std::size_t max_lines = state->midi_debug_monitor_max_lines > 0
        ? state->midi_debug_monitor_max_lines
        : 200u;
    if (state->midi_debug_monitor_lines->size() > max_lines) {
        const std::size_t overflow = state->midi_debug_monitor_lines->size() - max_lines;
        state->midi_debug_monitor_lines->erase(
            state->midi_debug_monitor_lines->begin(),
            state->midi_debug_monitor_lines->begin() + static_cast<std::ptrdiff_t>(overflow));
    }
}


bool enqueue_outgoing_message(
    const State* state,
    const std::vector<unsigned char>& message,
    bool handshake,
    bool delay_before_send = false,
    int delay_override_ms = -1) {
    if (message.empty()) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const int effective_delay_ms = std::max(0, delay_override_ms >= 0 ? delay_override_ms : get_outgoing_delay_ms(state));
    const auto delay = std::chrono::milliseconds(effective_delay_ms);
    if (!g_outgoing_schedule_active || g_next_outgoing_due_time < now) {
        g_next_outgoing_due_time = delay_before_send ? (now + delay) : now;
        g_outgoing_schedule_active = true;
    }

    PendingOutgoingMessage pending{};
    pending.bytes = message;
    pending.handshake = handshake;
    pending.due_time = g_next_outgoing_due_time;
    g_outgoing_queue.push_back(std::move(pending));

    g_next_outgoing_due_time += delay;
    return true;
}


bool send_outgoing_now(State* state, const std::vector<unsigned char>& message, bool handshake) {
    if (state == nullptr || message.empty()) {
        return false;
    }
    (void)handshake;

    bool sent = false;

    if (!sent && state->midi_out != nullptr && state->midi_out_enabled != nullptr &&
        *state->midi_out != nullptr && *state->midi_out_enabled) {
        const ssize_t written = snd_rawmidi_write(
            *state->midi_out, message.data(), message.size());
        snd_rawmidi_drain(*state->midi_out);
        sent = (written == static_cast<ssize_t>(message.size()));
    }

    if (sent) {
        append_midi_debug_line(state, "OUT", message);
    }
    return sent;
}

void process_outgoing_queue(State* state) {
    if (state == nullptr) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    while (!g_outgoing_queue.empty() && g_outgoing_queue.front().due_time <= now) {
        PendingOutgoingMessage pending = std::move(g_outgoing_queue.front());
        g_outgoing_queue.pop_front();

        if (!send_outgoing_now(state, pending.bytes, pending.handshake)) {
            if (state->rz1_sysex_status != nullptr) {
                *state->rz1_sysex_status = "Failed to send queued MIDI message";
            }
            break;
        }
        now = std::chrono::steady_clock::now();
    }

    if (g_outgoing_queue.empty()) {
        g_outgoing_schedule_active = false;
    }
}


bool send_handshake_message(State* state, unsigned char first, unsigned char second) {
    if (state == nullptr || state->midi_out == nullptr || state->midi_out_enabled == nullptr ||
        *state->midi_out == nullptr || !*state->midi_out_enabled) {
        return false;
    }

    const std::vector<unsigned char> hs = make_handshake_message(first, second);
    if (!enqueue_outgoing_message(state, hs, true, true, get_handshake_byte_delay_ms(state))) {
        return false;
    }
    process_outgoing_queue(state);
    return true;
}

}

void refresh_midi_in_ports(State* state) {
    if (state == nullptr || state->available_midi_in_ports == nullptr || state->midi_in_status == nullptr) {
        return;
    }

    enumerate_rawmidi_ports(*state->available_midi_in_ports, g_rawmidi_in_hw_ids, true);

    if (state->available_midi_in_ports->empty()) {
        state->available_midi_in_ports->push_back("No MIDI inputs available");
        *state->midi_in_status = "No MIDI input ports found";
    } else {
        *state->midi_in_status =
            std::to_string(state->available_midi_in_ports->size()) + " MIDI input port(s) available";
    }
}

void close_midi_in_port(State* state) {
    if (state == nullptr || state->midi_in == nullptr || state->midi_in_enabled == nullptr ||
        state->selected_midi_in_port == nullptr || state->midi_in_status == nullptr) {
        return;
    }

    if (*state->midi_in != nullptr) {
        snd_rawmidi_close(*state->midi_in);
        *state->midi_in = nullptr;
    }
    g_midi_parser = RawMidiParser{};  // reset parser state
    *state->midi_in_enabled = false;
    *state->selected_midi_in_port = -1;
    *state->midi_in_status = "MIDI input disconnected";
}

void open_midi_in_port(State* state, int port_index) {
    if (state == nullptr || state->midi_in == nullptr || state->available_midi_in_ports == nullptr ||
        state->midi_in_status == nullptr || state->selected_midi_in_port == nullptr || state->midi_in_enabled == nullptr) {
        return;
    }

    close_midi_in_port(state);

    if (port_index < 0 ||
        static_cast<std::size_t>(port_index) >= g_rawmidi_in_hw_ids.size()) {
        *state->midi_in_status = "Invalid MIDI input port";
        return;
    }

    snd_rawmidi_t* handle = nullptr;
    const int err = snd_rawmidi_open(&handle, nullptr,
        g_rawmidi_in_hw_ids[static_cast<std::size_t>(port_index)].c_str(),
        SND_RAWMIDI_NONBLOCK);
    if (err < 0 || handle == nullptr) {
        *state->midi_in_status = std::string("Failed to open input port: ") + snd_strerror(err);
        *state->midi_in_enabled = false;
        return;
    }

    *state->midi_in = handle;
    *state->selected_midi_in_port = port_index;
    *state->midi_in_enabled = true;
    *state->midi_in_status = std::string("Input: ") +
        (*state->available_midi_in_ports)[static_cast<std::size_t>(port_index)];
}

void refresh_midi_out_ports(State* state) {
    if (state == nullptr || state->available_midi_out_ports == nullptr || state->midi_out_status == nullptr) {
        return;
    }

    enumerate_rawmidi_ports(*state->available_midi_out_ports, g_rawmidi_out_hw_ids, false);

    if (state->available_midi_out_ports->empty()) {
        state->available_midi_out_ports->push_back("No MIDI outputs available");
        *state->midi_out_status = "No MIDI output ports found";
    } else {
        *state->midi_out_status =
            std::to_string(state->available_midi_out_ports->size()) + " MIDI output port(s) available";
    }
}

void close_midi_out_port(State* state) {
    if (state == nullptr || state->midi_out == nullptr || state->midi_out_enabled == nullptr ||
        state->selected_midi_out_port == nullptr || state->midi_out_status == nullptr) {
        return;
    }

    if (*state->midi_out != nullptr) {
        snd_rawmidi_drain(*state->midi_out);
        snd_rawmidi_close(*state->midi_out);
        *state->midi_out = nullptr;
    }
    *state->midi_out_enabled = false;
    *state->selected_midi_out_port = -1;
    *state->midi_out_status = "MIDI output disconnected";
}

void open_midi_out_port(State* state, int port_index) {
    if (state == nullptr || state->midi_out == nullptr || state->available_midi_out_ports == nullptr ||
        state->midi_out_status == nullptr || state->selected_midi_out_port == nullptr || state->midi_out_enabled == nullptr) {
        return;
    }

    close_midi_out_port(state);

    if (port_index < 0 ||
        static_cast<std::size_t>(port_index) >= g_rawmidi_out_hw_ids.size()) {
        *state->midi_out_status = "Invalid MIDI output port";
        return;
    }

    snd_rawmidi_t* handle = nullptr;
    const int err = snd_rawmidi_open(nullptr, &handle,
        g_rawmidi_out_hw_ids[static_cast<std::size_t>(port_index)].c_str(), 0);
    if (err < 0 || handle == nullptr) {
        *state->midi_out_status = std::string("Failed to open output port: ") + snd_strerror(err);
        *state->midi_out_enabled = false;
        return;
    }

    *state->midi_out = handle;
    *state->selected_midi_out_port = port_index;
    *state->midi_out_enabled = true;
    *state->midi_out_status = std::string("Output: ") +
        (*state->available_midi_out_ports)[static_cast<std::size_t>(port_index)];
}

bool send_rz1_sysex_command(State* state, const Actions& actions, std::uint8_t command_zz, const char* command_name) {
    if (state == nullptr || state->midi_out == nullptr || state->midi_out_enabled == nullptr || state->rz1_sysex_status == nullptr ||
        state->rz1_sysex_channel == nullptr || state->last_rz1_sysex_request == nullptr || state->rz1_sysex_capture == nullptr ||
        state->rz1_sysex_message_count == nullptr || state->rz1_sysex_overflow == nullptr) {
        return false;
    }

    if (*state->midi_out == nullptr || !*state->midi_out_enabled) {
        *state->rz1_sysex_status = "MIDI output is not connected";
        return false;
    }

    const int channel_index = std::clamp(*state->rz1_sysex_channel, 1, 16) - 1;
    const std::uint8_t channel_byte = static_cast<std::uint8_t>(0x70u | static_cast<std::uint8_t>(channel_index));
    std::vector<unsigned char> message = {
        0xF0, 0x44, 0x01, 0x00, channel_byte, command_zz, 0x00,
    };
    if (command_zz == 0x10u || command_zz == 0x14u) {
        message.push_back(static_cast<unsigned char>(channel_byte));
        message.push_back(kRz1HandshakeRequestToSend);
    }

    try {
        if (!enqueue_outgoing_message(state, message, false)) {
            *state->rz1_sysex_status = "Failed to queue SysEx command";
            return false;
        }
        process_outgoing_queue(state);
        *state->last_rz1_sysex_request = command_zz;
        if (command_zz == 0x10u || command_zz == 0x14u) {
            state->rz1_sysex_capture->clear();
            *state->rz1_sysex_message_count = 0;
            *state->rz1_sysex_overflow = false;
            arm_rz1_receive_poll();

            g_rz1_initial_31_sent_for_send = true;
            g_rz1_periodic_handshake_active = true;
            g_rz1_last_handshake_send_time = std::chrono::steady_clock::now();

            // Activate the raw stream receiver — it will handle everything
            // from header detection through data accumulation to F7 terminator.
            g_raw_receiver = RawSampleReceiver{};
            g_raw_receiver.active = true;

            if (state->sysex_dump_state != nullptr) {
                *state->sysex_dump_state = SysexDumpState::WaitingForAck;
            }
            *state->rz1_sysex_status = std::string("Sent: ") +
                (command_name != nullptr ? command_name : "SysEx") +
                " — initial handshake embedded, waiting for RZ-1 ACK/header";
        } else {
            if (state->sysex_dump_state != nullptr) {
                *state->sysex_dump_state = SysexDumpState::Idle;
            }
            disarm_rz1_receive_poll();
            *state->rz1_sysex_status = std::string("Sent: ") + (command_name != nullptr ? command_name : "SysEx");
        }
        if (actions.set_status != nullptr) {
            actions.set_status(*state->rz1_sysex_status);
        }
        return true;
    } catch (...) {
        *state->rz1_sysex_status = "SysEx send failed";
        return false;
    }
}

bool send_manual_rz1_handshake_31(State* state, const Actions& actions) {
    if (state == nullptr || state->midi_out == nullptr || state->midi_out_enabled == nullptr ||
        state->rz1_sysex_channel == nullptr || state->rz1_sysex_status == nullptr) {
        return false;
    }

    if (*state->midi_out == nullptr || !*state->midi_out_enabled) {
        *state->rz1_sysex_status = "MIDI output is not connected";
        if (actions.set_status != nullptr) {
            actions.set_status(*state->rz1_sysex_status);
        }
        return false;
    }

    const int channel_index = std::clamp(*state->rz1_sysex_channel, 1, 16) - 1;
    const unsigned char channel_byte = static_cast<unsigned char>(0x70u | static_cast<unsigned char>(channel_index));
    if (send_handshake_message(state, channel_byte, kRz1HandshakeRequestToSend)) {
        *state->rz1_sysex_status = "Manual handshake sent: 7n 31";
        if (actions.set_status != nullptr) {
            actions.set_status(*state->rz1_sysex_status);
        }
        return true;
    }

    *state->rz1_sysex_status = "Manual handshake send failed";
    if (actions.set_status != nullptr) {
        actions.set_status(*state->rz1_sysex_status);
    }
    return false;
}

bool save_rz1_sysex_capture(State* state, const Actions& actions) {
    if (state == nullptr || state->rz1_sysex_capture == nullptr || state->rz1_sysex_status == nullptr ||
        state->rz1_sysex_save_path == nullptr) {
        return false;
    }

    if (state->rz1_sysex_capture->empty()) {
        *state->rz1_sysex_status = "No captured SysEx data to save";
        return false;
    }

    std::filesystem::path out_path(state->rz1_sysex_save_path);
    if (out_path.empty()) {
        out_path = std::filesystem::path("captures/rz1_capture.syx");
    }

    std::error_code ec;
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out.is_open()) {
        *state->rz1_sysex_status = std::string("Failed to open file: ") + out_path.string();
        return false;
    }

    out.write(reinterpret_cast<const char*>(state->rz1_sysex_capture->data()),
              static_cast<std::streamsize>(state->rz1_sysex_capture->size()));
    if (!out.good()) {
        *state->rz1_sysex_status = std::string("Failed to write file: ") + out_path.string();
        return false;
    }

    *state->rz1_sysex_status = std::string("Saved SysEx capture: ") + out_path.string();
    if (actions.set_status != nullptr) {
        actions.set_status(*state->rz1_sysex_status);
    }
    return true;
}

void poll_midi_input(State* state, const PollContext& context) {
    if (state == nullptr || state->midi_in == nullptr || state->midi_in_enabled == nullptr ||
        state->rz1_sysex_capture == nullptr || state->rz1_sysex_message_count == nullptr || state->rz1_sysex_overflow == nullptr ||
        state->last_rz1_sysex_request == nullptr || state->rz1_sysex_status == nullptr || state->midi_note_velocity == nullptr ||
        state->selected_slot == nullptr || state->last_midi_note_slot == nullptr) {
        return;
    }

    process_outgoing_queue(state);
    maybe_send_periodic_handshake(state);
    (void)context;

    if (*state->midi_in == nullptr || !*state->midi_in_enabled) {
        return;
    }

    // ── Read raw bytes, parse into discrete MIDI messages ──────────────────
    std::vector<std::vector<unsigned char>> incoming_messages;
    bool raw_receiver_was_active_this_poll = false;
    {
        unsigned char raw_buf[2048];
        while (true) {
            const ssize_t n = snd_rawmidi_read(*state->midi_in, raw_buf, sizeof(raw_buf));
            if (n <= 0) { break; }
            // Log raw chunk immediately so the monitor shows progress during long SysEx streams
            append_midi_debug_line(state, "IN~",
                std::vector<unsigned char>(raw_buf, raw_buf + static_cast<std::size_t>(n)));
            // Feed active sample-dump receiver before the MIDI parser
            if (g_raw_receiver.active) {
                raw_receiver_was_active_this_poll = true;
                process_raw_sample_bytes(state, raw_buf, static_cast<std::size_t>(n));
            }
            auto batch = parse_midi_bytes(g_midi_parser, raw_buf, static_cast<std::size_t>(n));
            incoming_messages.insert(incoming_messages.end(),
                std::make_move_iterator(batch.begin()),
                std::make_move_iterator(batch.end()));
        }
    }

    for (const auto& message : incoming_messages) {
        if (message.empty()) { continue; }

        const bool is_send_transfer =
            (*state->last_rz1_sysex_request == 0x10u || *state->last_rz1_sysex_request == 0x14u);
        const bool raw_receiver_owns_transfer =
            raw_receiver_was_active_this_poll || g_raw_receiver.active;
        if (raw_receiver_owns_transfer) {
            const bool is_sysex = (!message.empty() && message[0] == 0xF0u);
            const bool is_handshake_pair =
                (message.size() == 2 && (message[0] & 0xF0u) == 0x70u &&
                 (message[1] == kRz1HandshakeRequestToSend || message[1] == kRz1HandshakeMoreToSend));
            const bool is_terminal_f7 = (message.size() == 1 && message[0] == 0xF7u);

            if (is_sysex || is_handshake_pair || is_terminal_f7) {
                continue;
            }
        }

        append_midi_debug_line(state, "IN", message);

        if (!message.empty() && message[0] == 0xF0) {
            // ── RZ-1 ACK: F0 44 01 00 70 30 [F7] ──────────────────────────────
            // Short ACK is tiny (~7 bytes); long frames with 70 30 are data blocks.
            const bool is_rz1_short_ack = (message.size() >= 6 && message.size() < 20 &&
                message[1] == 0x44 && message[2] == 0x01 &&
                message[3] == 0x00 && (message[4] & 0xF0u) == 0x70u && message[5] == 0x30);

            const bool is_rz1_more_to_send_sysex = (message.size() >= 6 &&
                message[1] == 0x44 && message[2] == 0x01 &&
                message[3] == 0x00 && (message[4] & 0xF0u) == 0x70u && message[5] == 0x32);

            if (is_rz1_short_ack && state->sysex_dump_state != nullptr &&
                *state->sysex_dump_state == SysexDumpState::WaitingForAck &&
                (*state->last_rz1_sysex_request == 0x10u || *state->last_rz1_sysex_request == 0x14u)) {
                note_rz1_receive_activity();
                if (g_rz1_initial_31_sent_for_send) {
                    *state->sysex_dump_state = SysexDumpState::Receiving;
                    *state->rz1_sysex_status = "RZ-1 ACK received — initial handshake already sent, receiving dump...";
                } else {
                    bool sent = false;
                    if (state->midi_out != nullptr && *state->midi_out != nullptr && state->midi_out_enabled != nullptr && *state->midi_out_enabled) {
                        sent = send_handshake_message(state, static_cast<unsigned char>(message[4]), kRz1HandshakeRequestToSend);
                    }
                    if (sent) {
                        g_rz1_initial_31_sent_for_send = true;
                    }
                    *state->sysex_dump_state = sent ? SysexDumpState::Receiving : SysexDumpState::Error;
                    *state->rz1_sysex_status = sent
                        ? "RZ-1 ACK received — handshake sent, receiving dump..."
                        : "RZ-1 ACK received but handshake send failed";
                }
                continue;  // don't capture the ACK itself
            }

            if (is_rz1_short_ack) {
                if (*state->last_rz1_sysex_request == 0x20u || *state->last_rz1_sysex_request == 0x24u) {
                    *state->rz1_sysex_status = "RZ-1 ACK received for RECEIVE request — now send the dump to RZ-1";
                } else {
                    *state->rz1_sysex_status = "RZ-1 ACK received";
                }
                continue;  // never store ACK frames in capture buffer
            }

            if (is_rz1_more_to_send_sysex && state->sysex_dump_state != nullptr &&
                *state->sysex_dump_state == SysexDumpState::Receiving) {
                note_rz1_receive_activity();
                if (state->rz1_sysex_capture->size() + 2 <= state->rz1_sysex_capture_max_bytes) {
                    state->rz1_sysex_capture->push_back(message[4]);
                    state->rz1_sysex_capture->push_back(kRz1HandshakeMoreToSend);
                }
                if (state->midi_out != nullptr && *state->midi_out != nullptr && state->midi_out_enabled != nullptr && *state->midi_out_enabled) {
                    if (send_handshake_message(state, message[4], kRz1HandshakeRequestToSend)) {
                        *state->rz1_sysex_status = "Handshake: requesting next block (" +
                            std::to_string(state->rz1_sysex_capture->size()) + " bytes so far)";
                    }
                }
                continue;
            }

            // ── Capture data block ─────────────────────────────────────────────
            const std::size_t remaining = (state->rz1_sysex_capture_max_bytes > state->rz1_sysex_capture->size())
                ? (state->rz1_sysex_capture_max_bytes - state->rz1_sysex_capture->size())
                : 0u;
            const std::size_t to_copy = std::min(remaining, message.size());
            note_rz1_receive_activity();
            if (to_copy > 0u) {
                state->rz1_sysex_capture->insert(state->rz1_sysex_capture->end(),
                    message.begin(), message.begin() + static_cast<std::ptrdiff_t>(to_copy));
            }
            if (to_copy < message.size()) {
                *state->rz1_sysex_overflow = true;
            }
            (*state->rz1_sysex_message_count)++;

            char request_hex[8] = "--";
            if (*state->last_rz1_sysex_request != 0x00u) {
                std::snprintf(request_hex, sizeof(request_hex), "%02X", *state->last_rz1_sysex_request);
            }
            *state->rz1_sysex_status = "Captured SysEx " + std::to_string(state->rz1_sysex_capture->size()) +
                " bytes in " + std::to_string(*state->rz1_sysex_message_count) + " message(s)" +
                " (last request zz=" + request_hex + ")";
            if (*state->rz1_sysex_overflow) {
                *state->rz1_sysex_status += " [buffer full: 512KB]";
            }

            if (is_send_transfer && state->sysex_dump_state != nullptr &&
                *state->sysex_dump_state == SysexDumpState::Receiving &&
                state->midi_out != nullptr && state->midi_out_enabled != nullptr &&
                *state->midi_out != nullptr && *state->midi_out_enabled) {
                if (send_handshake_message(state, static_cast<unsigned char>(message[4]), kRz1HandshakeRequestToSend)) {
                    *state->rz1_sysex_status += " — sent 7n31";
                }
            }
            continue;
        }

        // ── RZ-1 handshake packets: 7n 31 / 7n 32 ─────────────────────────────
        if (message.size() == 2 &&
            (message[0] & 0xF0u) == 0x70u &&
            (message[1] == kRz1HandshakeRequestToSend || message[1] == kRz1HandshakeMoreToSend)) {
            if (state->sysex_dump_state != nullptr &&
                *state->sysex_dump_state == SysexDumpState::Receiving) {
                note_rz1_receive_activity();
                // Capture the handshake bytes (needed for replay during RECEIVE)
                if (state->rz1_sysex_capture->size() + 2 <= state->rz1_sysex_capture_max_bytes) {
                    state->rz1_sysex_capture->push_back(message[0]);
                    state->rz1_sysex_capture->push_back(message[1]);
                }

                // Reply with 7n 31 for either handshake variant.
                if (message[1] == kRz1HandshakeMoreToSend && state->midi_out != nullptr && *state->midi_out != nullptr && state->midi_out_enabled != nullptr && *state->midi_out_enabled) {
                    if (send_handshake_message(state, message[0], kRz1HandshakeRequestToSend)) {
                        *state->rz1_sysex_status = "Handshake: requesting next block (" +
                            std::to_string(state->rz1_sysex_capture->size()) + " bytes so far)";
                    }
                } else if (message[1] == kRz1HandshakeRequestToSend) {
                    *state->rz1_sysex_status = "Handshake: RZ-1 ACK received (" +
                        std::to_string(state->rz1_sysex_capture->size()) + " bytes so far)";
                }
            }
            continue;
        }

        // ── Standalone F7 = end of transmission ───────────────────────────────
        if (message.size() == 1 && message[0] == 0xF7u) {
            if (state->sysex_dump_state != nullptr &&
                *state->sysex_dump_state == SysexDumpState::Receiving) {
                *state->sysex_dump_state = SysexDumpState::Done;
                disarm_rz1_receive_poll();
                *state->rz1_sysex_status = "Dump complete: " +
                    std::to_string(state->rz1_sysex_capture->size()) + " bytes, " +
                    std::to_string(*state->rz1_sysex_message_count) + " SysEx message(s)";
            }
            continue;
        }

        if (message.size() >= 3) {
            const unsigned char status = message[0] & 0xF0;
            const unsigned char note = message[1];
            const unsigned char velocity = message[2];

            if (status == 0x90 && velocity > 0) {
                (*state->midi_note_velocity)[note] = static_cast<std::int8_t>(velocity);

                if (context.editor_page_active) {
                    const int slot_top_note = context.slot_base_midi_note + static_cast<int>(context.slot_count);
                    if (note >= static_cast<unsigned char>(context.slot_base_midi_note) && note < static_cast<unsigned char>(slot_top_note)) {
                        const std::size_t slot = static_cast<std::size_t>(note - static_cast<unsigned char>(context.slot_base_midi_note));
                        *state->selected_slot = slot;
                        *state->last_midi_note_slot = slot;
                    }
                }
            } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
                (*state->midi_note_velocity)[note] = 0;
            }
        }
    }

}

bool send_rz1_sysex_dump_to_rz1(State* state, const Actions& actions, bool is_sample) {
    if (state == nullptr || state->midi_out == nullptr || state->midi_out_enabled == nullptr ||
        state->rz1_sysex_capture == nullptr || state->rz1_sysex_status == nullptr ||
        state->rz1_sysex_channel == nullptr) {
        return false;
    }
    if (*state->midi_out == nullptr || !*state->midi_out_enabled) {
        *state->rz1_sysex_status = "MIDI output is not connected";
        return false;
    }
    if (state->rz1_sysex_capture->empty()) {
        *state->rz1_sysex_status = "No captured SysEx data in buffer — load or capture a dump first";
        return false;
    }

    const std::uint8_t zz = is_sample ? 0x20u : 0x24u;
    const int ch = std::clamp(*state->rz1_sysex_channel, 1, 16) - 1;
    const unsigned char ch_byte = static_cast<unsigned char>(0x70u | static_cast<unsigned char>(ch));

    // Patch header: remove F0 44 01 00 70 30 [F7] and prepend proper receive command
    const auto& cap = *state->rz1_sysex_capture;
    static const std::array<unsigned char, 6> kAckPrefix = {0xF0, 0x44, 0x01, 0x00, 0x70, 0x30};

    std::size_t start = 0;
    if (cap.size() >= 6 && std::equal(kAckPrefix.begin(), kAckPrefix.end(), cap.begin())) {
        start = 6;
        if (start < cap.size() && cap[start] == 0xF7u) {
            ++start;
        }
    }

    std::vector<unsigned char> patched;
    patched.reserve(cap.size() + 8);
    const std::array<unsigned char, 8> receive_cmd = {
        0xF0, 0x44, 0x01, 0x00, ch_byte, zz, 0x00, 0xF7
    };
    patched.insert(patched.end(), receive_cmd.begin(), receive_cmd.end());
    patched.insert(patched.end(), cap.begin() + static_cast<std::ptrdiff_t>(start), cap.end());

    // Parse and send individual messages: F0...F7 SysEx blocks + 2-byte handshake pairs
    std::size_t pos = 0;
    std::size_t sent_msgs = 0;
    while (pos < patched.size()) {
            if (patched[pos] == 0xF0u) {
                std::size_t end = pos + 1;
                while (end < patched.size() && patched[end] != 0xF7u) { ++end; }
                if (end < patched.size()) { ++end; }  // include F7
                std::vector<unsigned char> msg(
                    patched.begin() + static_cast<std::ptrdiff_t>(pos),
                    patched.begin() + static_cast<std::ptrdiff_t>(end));
                if (!enqueue_outgoing_message(state, msg, false)) {
                    *state->rz1_sysex_status = "Failed to queue dump message";
                    return false;
                }
                ++sent_msgs;
                pos = end;
            } else if ((patched[pos] & 0xF0u) == 0x70u && pos + 1 < patched.size()) {
                // Handshake byte pair
                if (send_handshake_message(state, patched[pos], patched[pos + 1])) {
                    ++sent_msgs;
                    pos += 2;
                } else {
                    ++pos;
                }
            } else {
                ++pos;
            }
        }

    process_outgoing_queue(state);

    *state->rz1_sysex_status = std::string("Sent ") + (is_sample ? "sample" : "rhythm") +
        " dump to RZ-1 (" + std::to_string(sent_msgs) + " messages, " +
        std::to_string(patched.size()) + " bytes)";
    if (actions.set_status != nullptr) {
        actions.set_status(*state->rz1_sysex_status);
    }
    return true;
}

}  // namespace drumrom::main_ui_midi
