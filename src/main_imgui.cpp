// Main application: global state, UI orchestration, preset/kit I/O, and rendering loop.
#include "drumrom/synth.h"
#include "drumrom/synth_elements.h"
#include "drumrom/synth_elements_exact.h"
#include "drumrom/synth_freeverb.h"
#include "drumrom/sample_browser_fs.h"
#include "drumrom/preset_browser_fs.h"
#include "drumrom/ui_bottom_toolbar.h"
#include "drumrom/ui_editor_page.h"
#include "drumrom/settings_io.h"
#include "drumrom/onerom_usb_routing.h"
#include "drumrom/ui_pin_matrix_page.h"
#include "drumrom/ui_settings_page.h"
#include "drumrom/main_ui_orchestration.h"
#include "drumrom/main_ui_editor_dispatch.h"
#include "drumrom/main_ui_midi.h"
#include "drumrom/main_ui_overlay_controls.h"
#include "drumrom/main_ui_overlay_waveform.h"
#include "drumrom/main_ui_icons.h"
#include "drumrom/main_ui_runtime_input.h"
#include "drumrom/main_ui_runtime_cleanup.h"
#include "drumrom/main_ui_runtime_startup.h"
#include "drumrom/main_ui_runtime_frame.h"
#include "drumrom/main_ui_runtime_events.h"
#include "drumrom/main_ui_runtime_bootstrap.h"
#include "drumrom/app_core_slot_layout.h"
#include "drumrom/app_core_history.h"
#include "drumrom/sample_dsp_shared.h"
#include "drumrom/sample_param_schema.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <sndfile.h>
#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using drumrom::synth::DrumParams;
using drumrom::synth::EnvelopeShape;
using drumrom::ui_bottom_toolbar::RamSampleLayout;
using drumrom::app_core_slot_layout::SlotDef;

// Slot definitions
constexpr int kSampleRate = 20833;
constexpr std::array<SlotDef, 16> kSlots = {{
    {"tom1", "Tom 1", 3791, false},
    {"tom3", "Tom 3", 4087, false},
    {"rimshot", "Rim", 1303, false},
    {"open_hihat", "Open HH", 12009, false},
    {"clap", "Claps", 2511, false},
    {"cowbell", "Cowbell", 1951, false},
    {"sample1", "Sample 1", 4096, true},
    {"sample3", "Sample 3", 4096, true},
    {"tom2", "Tom 2", 3844, false},
    {"kick", "BD", 1627, false},
    {"snare", "SD", 3224, false},
    {"closed_hihat", "Closed HH", 1223, false},
    {"ride", "Ride", 13935, false},
    {"crash", "Crash", 14371, false},
    {"sample2", "Sample 2", 4096, true},
    {"sample4", "Sample 4", 4096, true},
}};

enum class SourceKind { Synth, Sample, Loop };
enum class DrumKind { Kick, Snare, Hihat, Tom, Clap, Elements, ElementsExact };
enum class UiPage { Editor, PinMatrix, Settings };
enum class AmpEnvelopeMode { Off, PreFit, Output };  // Off=none, PreFit=old behavior (compressed), Output=new behavior (correct timing)
enum class SysexFolderTarget { Sample, Rhythm };

// Sample editor state
struct SampleEdit {
    std::string path;
    float source_rate_hz = 20833.0f;
    int start_pct = 0;
    int end_pct = 100;
    int loop_start_pct = 0;
    int loop_end_pct = 100;
    float loop_increment_pct = 0.0f;
    float tune_semitones = 0.0f;
    float filter_cutoff_hz = 9000.0f;
    float filter_cutoff_end_hz = 1800.0f;
    float filter_env_decay_s = 0.180f;
    float filter_resonance = 0.2f;
    float amp_attack_s = 0.001f;
    float amp_decay_s = 0.060f;
    float amp_sustain = 0.90f;
    float amp_release_s = 0.020f;
    AmpEnvelopeMode amp_envelope_mode = AmpEnvelopeMode::Output;
};

// Slot configuration
struct SlotConfig {
    SourceKind source = SourceKind::Synth;
    DrumKind drum = DrumKind::Tom;
    DrumParams params;
    drumrom::synth::ElementsParams elements_params;
    drumrom::synth::ElementsExactParams elements_exact_params;
    SampleEdit sample;
    float output_gain_db = 0.0f;
    float limiter_ceiling = 1.0f;
    int output_shaper_mode = 2;      // 0 = off, 1 = hard clip, 2 = EOS-style soft saturation
    float output_saturation = 0.65f; // Soft saturation drive amount.
    std::uint32_t seed = 0xC001D00DU;
};

// Global state
std::array<SlotConfig, kSlots.size()> g_slot_cfg{};
std::size_t g_selected_slot = 0;
UiPage g_ui_page = UiPage::Editor;
RamSampleLayout g_ram_sample_layout = RamSampleLayout::None;
std::optional<SlotConfig> g_slot_clipboard;
std::size_t g_slot_clipboard_source = static_cast<std::size_t>(-1);

struct EditorSnapshot {
    std::array<SlotConfig, kSlots.size()> slots;
    std::size_t selected_slot = 0;
    UiPage ui_page = UiPage::Editor;
    RamSampleLayout ram_sample_layout = RamSampleLayout::None;
    std::array<std::array<std::uint8_t, 28>, 28> pin_matrix{};
};

std::vector<EditorSnapshot> g_history;
std::size_t g_history_index = 0;
bool g_history_initialized = false;
bool g_history_applying = false;
bool g_history_commit_pending = false;
bool g_has_pending_snapshot = false;
EditorSnapshot g_pending_snapshot;
std::size_t g_pending_history_index = 0;

std::string g_status;
double g_status_expire_time = 0.0;
bool g_params_dirty = false;
bool g_wave_preview_dirty = true;
std::size_t g_wave_preview_slot = 0;
std::vector<float> g_wave_preview;
std::size_t g_wave_preview_sample_length = 0;  // Length of processed sample (before ADSR in Pre-Fit mode)
bool g_auto_upload_enabled = true;
bool g_auto_upload_busy = false;
bool g_auto_upload_commit_requested = false;
bool g_auto_play_enabled = false;
bool g_auto_play_commit_requested = false;
SDL_Renderer* g_sdl_renderer = nullptr;
SDL_AudioDeviceID g_preview_audio_device = 0;
SDL_Texture* g_drum_icons_texture = nullptr;
bool g_drum_icons_texture_attempted = false;
bool g_drum_icons_uv_ready = false;
std::array<ImVec4, 16> g_drum_icons_uvs{};
bool g_drum_type_textures_attempted = false;
std::array<SDL_Texture*, 5> g_drum_type_textures{{nullptr, nullptr, nullptr, nullptr, nullptr}};
std::string g_last_sample_error;
std::string g_sample_path_status_input;
std::string g_sample_path_status_resolved;
bool g_sample_path_status_exists = false;
enum class OverlayId {
    Amp,
    Pitch,
    ToneOrFilter,
    FmModPitch,
    FmIndex,
    AmPitch,
    AmDepth,
};
enum class WavePreviewMode { Slot, RomOverview };
OverlayId g_overlay_selected = OverlayId::Amp;
OverlayId g_overlay_drag_selected = OverlayId::Amp;
int g_overlay_drag_point = -1;
WavePreviewMode g_wave_preview_mode = WavePreviewMode::Slot;
WavePreviewMode g_wave_preview_mode_cached = WavePreviewMode::Slot;
std::vector<std::string> g_sample_folders;
std::vector<std::string> g_current_folder_files;
int g_selected_folder = -1;
std::string g_selected_folder_path;
int g_selected_sample_file = -1;
bool g_sample_browser_audition_enabled = true;
std::vector<std::string> g_loop_files;
int g_selected_loop_file = -1;
std::string g_selected_loop_folder_path;
std::vector<std::size_t> g_loop_split_boundaries;
std::size_t g_loop_split_waveform_length = 0;
std::vector<std::size_t> g_loop_split_slot_indices;
int g_loop_split_selected_region = -1;
int g_loop_split_drag_boundary = -1;
std::string g_loop_split_cached_path;
int g_loop_split_cached_target_pads = 12;
bool g_loop_split_cached_autofit = false;
std::vector<std::size_t> g_rom_overview_boundaries;
std::vector<std::size_t> g_rom_overview_slot_indices;
std::size_t g_rom_overview_waveform_length = 0;
int g_rom_overview_selected_region = -1;
std::size_t g_sample_path_slot = static_cast<std::size_t>(-1);
std::vector<std::string> g_preset_folders;
std::vector<std::string> g_current_preset_files;
int g_selected_preset_folder = -1;
std::string g_selected_preset_folder_path;
int g_selected_preset_file = -1;
bool g_show_kits_mode = false;  // Toggle between Presets (false) and Kits (true) mode
SysexFolderTarget g_sysex_folder_target = SysexFolderTarget::Sample;
bool g_show_sysex_file_browser = false;
std::string g_focused_listbox_id;  // Track which listbox has keyboard focus (e.g., "SampleFiles", "PresetFiles", "LoopFiles")
bool g_default_sample_template_loaded = false;
SlotConfig g_default_sample_slot_template{};

// MIDI input state
snd_rawmidi_t* g_midi_in = nullptr;
std::vector<std::string> g_available_midi_in_ports;
int g_selected_midi_in_port = -1;
bool g_midi_in_enabled = false;
std::string g_midi_in_status;
std::array<std::int8_t, 128> g_midi_note_velocity{};  // Current velocity for each note
std::size_t g_last_midi_note_slot = static_cast<std::size_t>(-1);

// MIDI output state
snd_rawmidi_t* g_midi_out = nullptr;
std::vector<std::string> g_available_midi_out_ports;
std::vector<std::string> g_available_onerom_serials;
int g_selected_midi_out_port = -1;
int g_selected_onerom_rom_a = -1;
int g_selected_onerom_rom_b = -1;
bool g_midi_out_enabled = false;
std::string g_midi_out_status;
std::string g_onerom_usb_status;
int g_rz1_sysex_channel = 1;  // 1-16, encoded as n=0x0-0xF in 0x7n byte
int g_rz1_sysex_dump_delay_ms = 270;
int g_rz1_sysex_handshake_byte_delay_ms = 8;
std::string g_rz1_sysex_status;
constexpr std::size_t kRz1SysexCaptureMaxBytes = 512u * 1024u;
std::vector<unsigned char> g_rz1_sysex_capture;
std::size_t g_rz1_sysex_message_count = 0;
bool g_rz1_sysex_overflow = false;
std::uint8_t g_last_rz1_sysex_request = 0x00u;
char g_rz1_sysex_save_path[256] = "captures/rz1_capture.syx";
drumrom::main_ui_midi::SysexDumpState g_sysex_dump_state = drumrom::main_ui_midi::SysexDumpState::Idle;
bool g_midi_debug_monitor_enabled = true;
std::vector<std::string> g_midi_debug_monitor_lines;
constexpr std::size_t kMidiDebugMonitorMaxLines = 240u;

constexpr std::size_t kEpromPinCount = 28;
constexpr std::size_t kPinMatrixBits = kEpromPinCount * kEpromPinCount;
constexpr std::size_t kPinMatrixPackedBytes = (kPinMatrixBits + 7u) / 8u;
constexpr std::uint32_t kOneromUserPluginContextPtrAddr = 0x2008002Cu;
constexpr std::uint32_t kPinbendContextMagic = 0x50424D58u;  // 'XMBP'
constexpr std::uint32_t kPinbendContextAbiVersion = 1u;
constexpr std::uint32_t kPinbendContextCommitOffset = 12u;
constexpr std::uint32_t kPinbendContextMatrixOffset = 16u;
const std::array<const char*, kEpromPinCount> kEprom27c256PinLabels{{
    "1:A14", "2:A12", "3:A7", "4:A6", "5:A5", "6:A4", "7:A3", "8:A2", "9:A1", "10:A0",
    "11:D0", "12:D1", "13:D2", "14:GND", "15:D3", "16:D4", "17:D5", "18:D6", "19:D7", "20:/CE",
    "21:A10", "22:/OE", "23:A11", "24:A9", "25:A8", "26:A13", "27:A15", "28:VCC",
}};
std::array<std::array<std::uint8_t, kEpromPinCount>, kEpromPinCount> g_pinbend_matrix{};
char g_sample_path_buf[512] = "";
char g_preset_path_buf[256] = "new-preset";

constexpr std::uint32_t kPresetMagic = 0x525A3150U;  // RZ1P
constexpr std::uint32_t kPresetVersion = 10;
constexpr std::uint32_t kPresetAllMagic = 0x525A3141U;  // RZ1A
constexpr std::size_t kLegacyPresetDrumParamsV2Size = 588u;
constexpr std::size_t kLegacyPresetDrumParamsV7Size = 600u;
constexpr std::size_t kLegacyPresetDrumParamsV8Size = sizeof(DrumParams) - (7u * sizeof(float));
static_assert(kLegacyPresetDrumParamsV8Size >= kLegacyPresetDrumParamsV7Size, "Unexpected legacy DrumParams size ordering");

bool read_legacy_drum_params(std::istream& in, std::uint32_t version, DrumParams& params) {
    auto bytes_after_path_for_version = [&](std::uint32_t ver) -> std::size_t {
        std::size_t n = 0;
        n += sizeof(std::uint32_t);  // source_rate_hz
        n += sizeof(float);          // start_pct
        n += sizeof(float);          // end_pct
        if (ver >= 6) {
            n += sizeof(float) * 3;  // loop start/end/increment
        }
        n += sizeof(float) * 9;      // tune, filter, filter_end, filter_decay, resonance, amp ADSR
        if (ver >= 4) {
            n += sizeof(float) * 2;  // output gain + limiter
        }
        if (ver >= 5) {
            n += sizeof(float) * 2;  // shaper mode + saturation (historical float payload)
        }
        if (ver >= 7) {
            n += sizeof(std::uint32_t);  // amp envelope mode
        }
        if (ver >= 8) {
            n += sizeof(drumrom::synth::ElementsParams);
        }
        return n;
    };

    auto try_read_with_size = [&](std::size_t legacy_size) -> bool {
        const std::streampos start = in.tellg();
        if (start == std::streampos(-1)) {
            return false;
        }

        std::array<std::uint8_t, kLegacyPresetDrumParamsV8Size> legacy_params{};
        in.read(reinterpret_cast<char*>(legacy_params.data()), static_cast<std::streamsize>(legacy_size));
        if (!in) {
            in.clear();
            in.seekg(start);
            return false;
        }

        // Validate the next field (sample path length) to detect wrong legacy layout size.
        std::uint32_t sample_path_len = 0;
        in.read(reinterpret_cast<char*>(&sample_path_len), sizeof(sample_path_len));
        if (!in || sample_path_len > 1000) {
            in.clear();
            in.seekg(start);
            return false;
        }

        const std::streampos after_path_len = in.tellg();
        if (after_path_len == std::streampos(-1)) {
            in.clear();
            in.seekg(start);
            return false;
        }

        // Ensure enough bytes remain for path + fixed payload fields for this version.
        in.seekg(0, std::ios::end);
        const std::streampos end = in.tellg();
        in.seekg(after_path_len);
        if (!in || end == std::streampos(-1) || end < after_path_len) {
            in.clear();
            in.seekg(start);
            return false;
        }

        const std::size_t required_tail = static_cast<std::size_t>(sample_path_len) + bytes_after_path_for_version(version);
        const std::size_t remaining = static_cast<std::size_t>(end - after_path_len);
        if (remaining < required_tail) {
            in.clear();
            in.seekg(start);
            return false;
        }

        if (version >= 8) {
            // Extra plausibility check for mixed v8 layouts.
            in.seekg(after_path_len + static_cast<std::streamoff>(sample_path_len));
            float source_rate_hz = 0.0f;
            float start_pct = 0.0f;
            float end_pct = 0.0f;
            in.read(reinterpret_cast<char*>(&source_rate_hz), sizeof(source_rate_hz));
            in.read(reinterpret_cast<char*>(&start_pct), sizeof(start_pct));
            in.read(reinterpret_cast<char*>(&end_pct), sizeof(end_pct));
            if (!in || !std::isfinite(source_rate_hz) || source_rate_hz < 1000.0f || source_rate_hz > 500000.0f || start_pct < -1.0f || start_pct > 1000.0f || end_pct < -1.0f || end_pct > 1000.0f) {
                in.clear();
                in.seekg(start);
                return false;
            }
        }

        in.seekg(after_path_len);
        if (!in) {
            in.clear();
            in.seekg(start);
            return false;
        }

        in.seekg(-static_cast<std::streamoff>(sizeof(sample_path_len)), std::ios::cur);
        if (!in) {
            in.clear();
            in.seekg(start);
            return false;
        }

        params = DrumParams{};
        std::memcpy(&params, legacy_params.data(), legacy_size);

        // New in v9: these fields do not exist in v5-v8 payloads.
        params.reverb.early_level = 0.35f;
        params.reverb.early_spread = 1.0f;
        params.reverb.diffusion = 0.5f;
        params.reverb.tone = 0.75f;
        params.reverb.late_mix = 0.65f;
        params.reverb.size = 1.0f;
        params.reverb.decay_shape = 0.5f;
        return true;
    };

    if (version >= 8) {
        // Some legacy v8 files were written with a 600-byte DrumParams block,
        // while others used the later 628-byte layout. Some kits also carried
        // the older 588-byte payload with a v8 header. Probe all safely.
        return try_read_with_size(kLegacyPresetDrumParamsV8Size) ||
               try_read_with_size(kLegacyPresetDrumParamsV7Size) ||
               try_read_with_size(kLegacyPresetDrumParamsV2Size);
    }
    if (version >= 4) {
        return try_read_with_size(kLegacyPresetDrumParamsV7Size);
    }
    return try_read_with_size(kLegacyPresetDrumParamsV2Size);
}

// Colors
constexpr ImVec4 kColorGreen = ImVec4(0x00 / 255.0f, 0xA5 / 255.0f, 0xA5 / 255.0f, 1.0f);
constexpr ImVec4 kColorGreenDark = ImVec4(0x00 / 255.0f, 0x63 / 255.0f, 0x63 / 255.0f, 1.0f);
constexpr ImVec4 kColorGreenLite = ImVec4(0x00 / 255.0f, 0xD1 / 255.0f, 0xD1 / 255.0f, 1.0f);

// Envelope colors (match overlay rendering)
constexpr ImVec4 kEnvelopeColorAmp = ImVec4(0.92f, 0.41f, 0.35f, 1.0f);       // orange-red
constexpr ImVec4 kEnvelopeColorPitch = ImVec4(0.35f, 0.57f, 1.0f, 1.0f);     // blue
constexpr ImVec4 kEnvelopeColorTone = ImVec4(0.27f, 0.80f, 0.47f, 1.0f);     // green
constexpr ImVec4 kEnvelopeColorFmPitch = ImVec4(0.95f, 0.80f, 0.30f, 1.0f);  // yellow
constexpr ImVec4 kEnvelopeColorFmIndex = ImVec4(0.82f, 0.42f, 0.90f, 1.0f);  // purple
constexpr ImVec4 kEnvelopeColorAmPitch = ImVec4(0.30f, 0.85f, 0.90f, 1.0f);  // cyan
constexpr ImVec4 kEnvelopeColorAmDepth = ImVec4(0.95f, 0.60f, 0.25f, 1.0f);  // orange

static_assert(std::is_trivially_copyable_v<DrumParams>, "DrumParams must be trivially copyable for preset serialization.");
static_assert(std::is_trivially_copyable_v<drumrom::synth::ElementsParams>, "ElementsParams must be trivially copyable for preset serialization.");
static_assert(std::is_trivially_copyable_v<drumrom::synth::ElementsExactParams>, "ElementsExactParams must be trivially copyable for preset serialization.");

// Layout configuration (can be edited live and saved)
struct LayoutConfig {
    float waveform_pane_height = 350.0f;
    float footer_height = 105.0f;
    float source_panel_height = 40.0f;
};

// Settings configuration
struct Settings {
    int midi_in_port_index = -1;
    int midi_out_port_index = -1;
    char samples_folder[256] = "samples";
    int monitor_width = 1920;
    int monitor_height = 1080;
    char onerom_serial_rom_a[128] = "";
    char onerom_serial_rom_b[128] = "";
    int onerom_single_device_role = 0;  // 0 = ROM A, 1 = ROM B
    bool loop_split_reset_slots = true;
    int loop_split_target_pads = 12;
    bool loop_split_autofit = false;
};

LayoutConfig g_layout_cfg;
Settings g_settings;
SDL_Window* g_window = nullptr;
bool g_window_resize_pending = false;
int g_pending_window_width = 0;
int g_pending_window_height = 0;
float g_ui_scale = 1.0f;
constexpr int kBaseWindowWidth = 1920;
constexpr int kBaseWindowHeight = 1080;

std::vector<std::int8_t> render_slot(std::size_t slot_idx, std::mt19937& rng);
bool upload_slot_to_device(std::size_t slot_idx);
bool play_slot_preview(std::size_t slot_idx);
bool play_original_sample_preview(const SampleEdit& sample);
void initialize_history_if_needed();
void maybe_commit_history(bool committed);
bool can_undo();
void refresh_midi_in_ports();
void open_midi_in_port(int port_index);
void close_midi_in_port();
void refresh_midi_out_ports();
void open_midi_out_port(int port_index);
void close_midi_out_port();
void refresh_onerom_usb_devices();
void poll_midi_input();
bool send_rz1_sysex_command(std::uint8_t command_zz, const char* command_name);
bool save_typed_rz1_sysex_capture();
bool load_typed_rz1_sysex_capture();
bool save_sample_rz1_sysex_capture();
bool load_sample_rz1_sysex_capture();
bool save_rhythm_rz1_sysex_capture();
bool load_rhythm_rz1_sysex_capture();
bool send_rz1_sysex_dump_to_rz1_sample();
bool send_rz1_sysex_dump_to_rz1_rhythm();
bool build_sample_pad_sysex(bool send_after_build);
void refresh_sysex_files_for_target(SysexFolderTarget target);
bool can_redo();
void perform_undo();
void perform_redo();
void select_slot(std::size_t slot_idx, bool trigger_preview);
void save_layout_config();
void load_layout_config();
void save_settings();
void load_settings();
void render_settings_page();
std::size_t get_slot_capacity(std::size_t slot_idx);
bool is_slot_enabled(std::size_t slot_idx);
void set_ram_sample_layout(RamSampleLayout layout);
bool listbox_nav_with_focus(const std::string& listbox_id, int item_count, int* selected_index, bool* activate_selection);
void render_action_pane(SlotConfig& cfg, bool* changed);
void render_editor_left_pane(SlotConfig& cfg, bool& changed, float scaled_waveform_height, float source_panel_height);
void render_action_pane_bridge(void* slot_config, bool* changed);
void render_editor_left_pane_bridge(void* slot_config, bool* changed, float scaled_waveform_height, float source_panel_height);
bool ensure_default_sample_for_slot(std::size_t slot_idx, SlotConfig& cfg);
bool load_slot_preset(const std::string& path, SlotConfig& cfg);
bool restore_default_rz1_kit_file();
bool load_default_rz1_kit_into_editor();
bool load_rom_ab_samples_into_editor();
bool fetch_rom_ab_samples_from_onerom_devices();
bool upload_rom_a_slots_to_device();
bool upload_rom_b_slots_to_device();
bool upload_all_rom_slots_to_device();
bool upload_midi_sample_data();

// ============================================================================
// Utility Functions
// ============================================================================

void set_status(const std::string& msg) {
    g_status = msg;
    g_status_expire_time = ImGui::GetTime() + 3.0;  // Show for 3 seconds
}

void save_layout_config() {
    std::error_code ec;
    std::filesystem::create_directories("settings", ec);
    std::ofstream file("settings/layout_config.json");
    if (file.is_open()) {
        file << "{\n";
        file << "  \"waveform_pane_height\": " << g_layout_cfg.waveform_pane_height << ",\n";
        file << "  \"footer_height\": " << g_layout_cfg.footer_height << ",\n";
        file << "  \"source_panel_height\": " << g_layout_cfg.source_panel_height << "\n";
        file << "}\n";
        file.close();
        set_status("Layout saved to settings/layout_config.json");
    }
}

void load_layout_config() {
    std::ifstream file("settings/layout_config.json");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("waveform_pane_height") != std::string::npos) {
                std::sscanf(line.c_str(), "%*[^:]: %f", &g_layout_cfg.waveform_pane_height);
            } else if (line.find("footer_height") != std::string::npos) {
                std::sscanf(line.c_str(), "%*[^:]: %f", &g_layout_cfg.footer_height);
            } else if (line.find("source_panel_height") != std::string::npos) {
                std::sscanf(line.c_str(), "%*[^:]: %f", &g_layout_cfg.source_panel_height);
            }
        }
        file.close();
    }
}

void save_settings() {
    std::error_code ec;
    std::filesystem::create_directories("settings", ec);
    if (drumrom::settings_io::save_settings_file(
            "settings/settings.json",
            g_settings.midi_in_port_index,
            g_settings.midi_out_port_index,
            g_settings.samples_folder,
            g_settings.monitor_width,
            g_settings.monitor_height,
            g_settings.onerom_serial_rom_a,
            g_settings.onerom_serial_rom_b,
            g_settings.onerom_single_device_role,
            g_settings.loop_split_reset_slots,
            g_settings.loop_split_target_pads,
            g_settings.loop_split_autofit)) {
        set_status("Settings saved");
    }
}

void load_settings() {
    drumrom::settings_io::load_settings_file(
        "settings/settings.json",
        &g_settings.midi_in_port_index,
        &g_settings.midi_out_port_index,
        g_settings.samples_folder,
        sizeof(g_settings.samples_folder),
        &g_settings.monitor_width,
        &g_settings.monitor_height,
        g_settings.onerom_serial_rom_a,
        sizeof(g_settings.onerom_serial_rom_a),
        g_settings.onerom_serial_rom_b,
        sizeof(g_settings.onerom_serial_rom_b),
        &g_settings.onerom_single_device_role,
        &g_settings.loop_split_reset_slots,
        &g_settings.loop_split_target_pads,
        &g_settings.loop_split_autofit);
    g_settings.loop_split_target_pads = (g_settings.loop_split_target_pads == 16) ? 16 : 12;
    std::strncpy(g_settings.samples_folder, "samples", sizeof(g_settings.samples_folder) - 1);
    g_settings.samples_folder[sizeof(g_settings.samples_folder) - 1] = '\0';
}

void select_slot(std::size_t slot_idx, bool trigger_preview) {
    if (slot_idx >= kSlots.size()) {
        return;
    }
    if (!is_slot_enabled(slot_idx)) {
        return;
    }

    const bool slot_changed = (g_selected_slot != slot_idx);
    g_selected_slot = slot_idx;
    if (slot_changed) {
        g_wave_preview_dirty = true;
    }
    if (trigger_preview) {
        (void)play_slot_preview(slot_idx);
    }
}

float clampf(float v, float mn, float mx) {
    return std::max(mn, std::min(mx, v));
}

bool is_sample_based_source(SourceKind source) {
    return source == SourceKind::Sample || source == SourceKind::Loop;
}

EditorSnapshot capture_snapshot() {
    EditorSnapshot s;
    s.slots = g_slot_cfg;
    s.selected_slot = g_selected_slot;
    s.ui_page = g_ui_page;
    s.ram_sample_layout = g_ram_sample_layout;
    s.pin_matrix = g_pinbend_matrix;
    return s;
}

void apply_snapshot(const EditorSnapshot& s) {
    g_history_applying = true;
    g_slot_cfg = s.slots;
    g_ui_page = s.ui_page;
    g_ram_sample_layout = s.ram_sample_layout;
    g_selected_slot = drumrom::app_core_slot_layout::normalized_selected_slot(
        kSlots,
        g_ram_sample_layout,
        s.selected_slot);
    g_pinbend_matrix = s.pin_matrix;
    g_sample_path_slot = static_cast<std::size_t>(-1);
    g_params_dirty = true;
    g_wave_preview_dirty = true;
    g_history_commit_pending = false;
    g_history_applying = false;
}

std::size_t get_slot_capacity(std::size_t slot_idx) {
    return drumrom::app_core_slot_layout::get_slot_capacity(kSlots, slot_idx, g_ram_sample_layout);
}

bool is_slot_enabled(std::size_t slot_idx) {
    return drumrom::app_core_slot_layout::is_slot_enabled(kSlots, slot_idx, g_ram_sample_layout);
}

void set_ram_sample_layout(RamSampleLayout layout) {
    if (g_ram_sample_layout == layout) {
        return;
    }
    g_ram_sample_layout = layout;
    g_selected_slot = drumrom::app_core_slot_layout::selected_slot_after_layout_change(
        kSlots,
        g_ram_sample_layout,
        g_selected_slot);
    g_wave_preview_dirty = true;
    g_params_dirty = true;
    g_history_commit_pending = true;
}
void push_snapshot(const EditorSnapshot& s) {
    drumrom::app_core_history::push_snapshot(
        &g_history,
        &g_history_index,
        &g_history_initialized,
        g_history_applying,
        s,
        256);
}

void initialize_history_if_needed() {
    drumrom::app_core_history::initialize_history_if_needed(
        &g_history,
        &g_history_index,
        &g_history_initialized,
        g_history_applying,
        []() { return capture_snapshot(); },
        256);
}

void maybe_commit_history(bool committed) {
    drumrom::app_core_history::maybe_commit_history(
        committed,
        g_history_applying,
        &g_history,
        &g_history_index,
        &g_history_initialized,
        []() { return capture_snapshot(); },
        256);
}

bool can_undo() {
    return drumrom::app_core_history::can_undo(g_history, g_history_index, g_history_initialized);
}

bool can_redo() {
    return drumrom::app_core_history::can_redo(g_history, g_history_index, g_history_initialized);
}

void perform_undo() {
    if (!can_undo()) {
        return;
    }
    g_history_commit_pending = false;
    if (drumrom::app_core_history::select_undo_target(
            g_history,
            g_history_index,
            g_history_initialized,
            &g_pending_snapshot,
            &g_pending_history_index,
            &g_has_pending_snapshot)) {
        set_status("Undo");
    }
}

void perform_redo() {
    if (!can_redo()) {
        return;
    }
    g_history_commit_pending = false;
    if (drumrom::app_core_history::select_redo_target(
            g_history,
            g_history_index,
            g_history_initialized,
            &g_pending_snapshot,
            &g_pending_history_index,
            &g_has_pending_snapshot)) {
        set_status("Redo");
    }
}

float ui_rate_from_internal(float internal_rate, float min_rate, float max_rate) {
    const float r = clampf(internal_rate, min_rate, max_rate);
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float internal_rate_from_ui(float ui_rate, float min_rate, float max_rate) {
    const float r = clampf(ui_rate, min_rate, max_rate);
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float ui_attack_from_internal(float internal_rate, float min_rate, float max_rate) {
    const float r = clampf(internal_rate, min_rate, max_rate);
    if (r <= (min_rate + 0.0001f)) {
        return min_rate;
    }
    return clampf((min_rate + max_rate) - r, min_rate, max_rate);
}

float internal_attack_from_ui(float ui_rate, float min_rate, float max_rate) {
    const float u = clampf(ui_rate, min_rate, max_rate);
    if (u <= (min_rate + 0.0001f)) {
        return min_rate; // keep exact 0 as instant attack in DSP
    }
    const float r = (min_rate + max_rate) - u;
    return clampf(r, min_rate + 0.0001f, max_rate);
}

bool slider_float_with_text_input(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags = 0);
bool slider_rate_with_text_input(const char* label, float* internal_rate, float min_rate, float max_rate, bool attack_mode, const char* format, ImGuiSliderFlags flags = 0);

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string strip_wrapping_quotes(std::string s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string shell_quote_posix(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::optional<std::string> run_command_capture_output(const std::string& command) {
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return std::nullopt;
    }

    std::array<char, 512> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#if defined(_WIN32)
    const int exit_code = _pclose(pipe);
#else
    const int exit_code = pclose(pipe);
#endif
    if (exit_code != 0) {
        return std::nullopt;
    }

    output = trim_copy(output);
    if (output.empty()) {
        return std::nullopt;
    }
    return output;
}

std::optional<std::string> open_folder_dialog(const std::string& initial_path) {
    const std::string initial = initial_path.empty() ? "." : initial_path;
#if defined(_WIN32)
    std::string escaped = initial;
    std::size_t pos = 0;
    while ((pos = escaped.find("'", pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }
    const std::string command =
        "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$dialog = New-Object System.Windows.Forms.FolderBrowserDialog; "
        "$dialog.SelectedPath = '" + escaped + "'; "
        "if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Write-Output $dialog.SelectedPath }\"";
    return run_command_capture_output(command);
#elif defined(__APPLE__)
    (void)initial;
    return run_command_capture_output("osascript -e 'POSIX path of (choose folder)' 2>/dev/null");
#else
    const std::string quoted_initial = shell_quote_posix(initial);
    const std::string command =
        "bash -lc \"if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --directory --filename=" + quoted_initial +
        "; elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getexistingdirectory " + quoted_initial +
        "; fi\" 2>/dev/null";
    return run_command_capture_output(command);
#endif
}

std::filesystem::path resolve_sample_path(const std::string& raw_input) {
    std::string p = trim_copy(strip_wrapping_quotes(trim_copy(raw_input)));
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.rfind("file://", 0) == 0) {
        p = p.substr(7);
    }
    if (p.size() >= 3 && p[0] == '/' && std::isalpha(static_cast<unsigned char>(p[1])) && p[2] == ':') {
        p = p.substr(1);
    }
    if (p.rfind("~/", 0) == 0) {
        if (const char* home = std::getenv("HOME")) {
            p = std::string(home) + p.substr(1);
        }
    }

    std::filesystem::path in(p);
    if (in.empty()) {
        return {};
    }
    if (in.is_absolute() && std::filesystem::exists(in)) {
        return in;
    }
    if (std::filesystem::exists(in)) {
        return in;
    }

    std::vector<std::filesystem::path> sample_roots = {
        std::filesystem::path("samples"),
        std::filesystem::path("../samples"),
        std::filesystem::path("../../samples")
    };
    if (char* base = SDL_GetBasePath()) {
        const std::filesystem::path base_path(base);
        SDL_free(base);
        sample_roots.push_back(base_path / "samples");
        sample_roots.push_back(base_path.parent_path() / "samples");
    }

    for (const auto& root : sample_roots) {
        const std::filesystem::path under_samples = root / in;
        if (std::filesystem::exists(under_samples)) {
            return under_samples;
        }
    }

    const std::string norm = in.generic_string();
    if (norm.rfind("samples/", 0) == 0) {
        const std::filesystem::path rel_in_samples(norm.substr(8));
        for (const auto& root : sample_roots) {
            const std::filesystem::path candidate = root / rel_in_samples;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    }

    std::string in_name = in.filename().string();
    if (in_name.empty()) {
        const std::size_t slash = p.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < p.size()) {
            in_name = p.substr(slash + 1);
        }
    }
    if (!in_name.empty()) {
        std::string in_name_lc = in_name;
        std::transform(in_name_lc.begin(), in_name_lc.end(), in_name_lc.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        for (const auto& root : sample_roots) {
            if (!std::filesystem::exists(root)) {
                continue;
            }
            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                std::string entry_name = entry.path().filename().string();
                std::transform(entry_name.begin(), entry_name.end(), entry_name.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (entry_name == in_name_lc) {
                    return entry.path();
                }
            }
        }
    }

    return in;
}

struct RomDefaultSampleDef {
    const char* slot_name;
    const char* rom_path;
    std::size_t offset;
    std::size_t length;
    const char* raw_path;
};

constexpr std::array<RomDefaultSampleDef, 12> kRomDefaultSamples = {{
    {"tom1", "Casio-Rz-1/SOUND ROM A.bin", 0u, 3791u, "samples/rz1_original/tom1.raw"},
    {"tom2", "Casio-Rz-1/SOUND ROM A.bin", 3791u, 3844u, "samples/rz1_original/tom2.raw"},
    {"tom3", "Casio-Rz-1/SOUND ROM A.bin", 7635u, 4087u, "samples/rz1_original/tom3.raw"},
    {"kick", "Casio-Rz-1/SOUND ROM A.bin", 11722u, 1627u, "samples/rz1_original/kick.raw"},
    {"snare", "Casio-Rz-1/SOUND ROM A.bin", 13349u, 3224u, "samples/rz1_original/snare.raw"},
    {"rimshot", "Casio-Rz-1/SOUND ROM A.bin", 16573u, 1303u, "samples/rz1_original/rimshot.raw"},
    {"closed_hihat", "Casio-Rz-1/SOUND ROM A.bin", 17876u, 1223u, "samples/rz1_original/closed_hihat.raw"},
    {"open_hihat", "Casio-Rz-1/SOUND ROM A.bin", 19099u, 12009u, "samples/rz1_original/open_hihat.raw"},
    {"clap", "Casio-Rz-1/SOUND ROM B.bin", 0u, 2511u, "samples/rz1_original/clap.raw"},
    {"ride", "Casio-Rz-1/SOUND ROM B.bin", 2511u, 13935u, "samples/rz1_original/ride.raw"},
    {"cowbell", "Casio-Rz-1/SOUND ROM B.bin", 16446u, 1951u, "samples/rz1_original/cowbell.raw"},
    {"crash", "Casio-Rz-1/SOUND ROM B.bin", 18397u, 14371u, "samples/rz1_original/crash.raw"},
}};

const RomDefaultSampleDef* find_rom_default_for_slot(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return nullptr;
    }
    const std::string slot_name = kSlots[slot_idx].name;
    for (const auto& def : kRomDefaultSamples) {
        if (slot_name == def.slot_name) {
            return &def;
        }
    }
    return nullptr;
}

bool ensure_raw_extracted_from_rom(const RomDefaultSampleDef& def) {
    const std::filesystem::path out_path(def.raw_path);
    std::error_code ec;
    if (std::filesystem::exists(out_path, ec) && !ec) {
        const auto sz = std::filesystem::file_size(out_path, ec);
        if (!ec && sz == static_cast<std::uintmax_t>(def.length)) {
            return true;
        }
    }

    const std::filesystem::path rom_path(def.rom_path);
    std::ifstream rom(rom_path, std::ios::binary);
    if (!rom) {
        return false;
    }

    rom.seekg(0, std::ios::end);
    const std::streamoff rom_size = rom.tellg();
    if (rom_size <= 0 || static_cast<std::size_t>(rom_size) < (def.offset + def.length)) {
        return false;
    }

    rom.seekg(static_cast<std::streamoff>(def.offset), std::ios::beg);
    std::vector<char> bytes(def.length);
    rom.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!rom) {
        return false;
    }

    const auto parent = out_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void load_default_sample_template_if_needed() {
    if (g_default_sample_template_loaded) {
        return;
    }

    SlotConfig tmp{};
    if (load_slot_preset("presets/default-sample.drum", tmp)) {
        g_default_sample_slot_template = tmp;
    } else {
        g_default_sample_slot_template = SlotConfig{};
    }
    g_default_sample_template_loaded = true;
}

bool ensure_default_sample_for_slot(std::size_t slot_idx, SlotConfig& cfg) {
    load_default_sample_template_if_needed();

    const std::filesystem::path resolved_existing = resolve_sample_path(cfg.sample.path);
    if (!cfg.sample.path.empty() && std::filesystem::exists(resolved_existing)) {
        return true;
    }

    cfg.sample = g_default_sample_slot_template.sample;
    cfg.output_gain_db = g_default_sample_slot_template.output_gain_db;
    cfg.limiter_ceiling = g_default_sample_slot_template.limiter_ceiling;
    cfg.output_shaper_mode = g_default_sample_slot_template.output_shaper_mode;
    cfg.output_saturation = g_default_sample_slot_template.output_saturation;

    if (slot_idx < kSlots.size() && kSlots[slot_idx].is_ram_sample) {
        // Factory default kit keeps RAM pads silent.
        cfg.sample.path.clear();
        return true;
    }

    const RomDefaultSampleDef* rom_def = find_rom_default_for_slot(slot_idx);
    if (rom_def == nullptr) {
        cfg.sample.path.clear();
        return true;
    }

    if (!ensure_raw_extracted_from_rom(*rom_def)) {
        set_status(std::string("Failed to extract ROM sample for ") + kSlots[slot_idx].name);
        return false;
    }

    cfg.sample.path = rom_def->raw_path;
    return true;
}

void refresh_sample_path_status() {
    if (g_selected_slot >= g_slot_cfg.size()) {
        g_sample_path_status_input.clear();
        g_sample_path_status_resolved.clear();
        g_sample_path_status_exists = false;
        return;
    }

    const std::string& raw = g_slot_cfg[g_selected_slot].sample.path;
    if (g_sample_path_status_input != raw) {
        g_sample_path_status_input = raw;
        g_sample_path_status_resolved.clear();
        g_sample_path_status_exists = false;
        if (!raw.empty()) {
            const std::filesystem::path resolved = resolve_sample_path(raw);
            if (!resolved.empty()) {
                g_sample_path_status_resolved = resolved.string();
            }
        }
    }

    if (!g_sample_path_status_resolved.empty()) {
        std::error_code ec;
        g_sample_path_status_exists = std::filesystem::exists(std::filesystem::path(g_sample_path_status_resolved), ec) && !ec;
    } else {
        g_sample_path_status_exists = false;
    }
}

std::size_t get_slot_start_address(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return 0;
    }

    const std::string slot_name = kSlots[slot_idx].name;

    // Canonical OneROM layout derived from RZ-1 ROM A/B slot positions.
    if (slot_name == "tom1") return 0;
    if (slot_name == "tom2") return 3791;
    if (slot_name == "tom3") return 7635;
    if (slot_name == "kick") return 11722;
    if (slot_name == "snare") return 13349;
    if (slot_name == "rimshot") return 16573;
    if (slot_name == "closed_hihat") return 17876;
    if (slot_name == "open_hihat") return 19099;

    // ROM B starts at 0x8000 (32768).
    if (slot_name == "clap") return 32768;
    if (slot_name == "ride") return 35279;
    if (slot_name == "cowbell") return 49214;
    if (slot_name == "crash") return 51165;

    // User sample slots begin after ROM A+B.
    constexpr std::size_t kRamStartAddress = 65536;
    if (slot_name == "sample1") return kRamStartAddress;
    if (slot_name == "sample2") return kRamStartAddress + get_slot_capacity(6);
    if (slot_name == "sample3") return kRamStartAddress + get_slot_capacity(6) + get_slot_capacity(14);
    if (slot_name == "sample4") return kRamStartAddress + get_slot_capacity(6) + get_slot_capacity(14) + get_slot_capacity(7);

    // Fallback for unknown slots.
    std::size_t addr = 0;
    for (std::size_t i = 0; i < slot_idx; ++i) {
        addr += get_slot_capacity(i);
    }
    return addr;
}

std::size_t get_slot_upload_start_address(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return 0;
    }

    const std::string slot_name = kSlots[slot_idx].name;

    // ROM A slots (local to ROM A image/chip)
    if (slot_name == "tom1") return 0;
    if (slot_name == "tom2") return 3791;
    if (slot_name == "tom3") return 7635;
    if (slot_name == "kick") return 11722;
    if (slot_name == "snare") return 13349;
    if (slot_name == "rimshot") return 16573;
    if (slot_name == "closed_hihat") return 17876;
    if (slot_name == "open_hihat") return 19099;

    // ROM B slots are local to ROM B image/chip, so they also start at 0.
    if (slot_name == "clap") return 0;
    if (slot_name == "ride") return 2511;
    if (slot_name == "cowbell") return 16446;
    if (slot_name == "crash") return 18397;

    return 0;
}

std::vector<std::int8_t> read_raw_signed8(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<std::int8_t>(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>()
    );
}

std::vector<float> decode_signed8(const std::vector<std::int8_t>& raw) {
    std::vector<float> out;
    out.reserve(raw.size());
    for (auto s : raw) {
        out.push_back(static_cast<float>(s) / 127.0f);
    }
    return out;
}

std::vector<float> resample_rate_linear(const std::vector<float>& in, int src_rate, int dst_rate) {
    if (in.empty() || src_rate <= 0 || dst_rate <= 0 || src_rate == dst_rate) {
        return in;
    }
    const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const std::size_t out_n = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(static_cast<double>(in.size()) * ratio)));
    std::vector<float> out;
    out.reserve(out_n);
    for (std::size_t i = 0; i < out_n; ++i) {
        const double src_pos = static_cast<double>(i) / ratio;
        const std::size_t i0 = static_cast<std::size_t>(std::floor(src_pos));
        const std::size_t i1 = std::min(i0 + 1, in.size() - 1);
        const float frac = static_cast<float>(src_pos - static_cast<double>(i0));
        out.push_back((in[i0] * (1.0f - frac)) + (in[i1] * frac));
    }
    return out;
}

std::vector<float> sample_range(const std::vector<float>& in, int start_pct, int end_pct) {
    if (in.empty()) {
        return {};
    }
    const int s_pct = std::max(0, std::min(99, start_pct));
    const int e_pct = std::max(s_pct + 1, std::min(100, end_pct));
    const std::size_t s = (in.size() * static_cast<std::size_t>(s_pct)) / 100;
    const std::size_t e = (in.size() * static_cast<std::size_t>(e_pct)) / 100;
    return std::vector<float>(in.begin() + static_cast<std::ptrdiff_t>(s), in.begin() + static_cast<std::ptrdiff_t>(e));
}

std::vector<float> tune_resample(const std::vector<float>& in, float semitones) {
    if (in.empty()) {
        return {};
    }
    const float ratio = std::pow(2.0f, semitones / 12.0f);
    if (std::abs(ratio - 1.0f) < 0.001f) {
        return in;
    }
    std::vector<float> out;
    out.reserve(in.size());
    float idx = 0.0f;
    while (idx < static_cast<float>(in.size() - 1)) {
        const std::size_t i0 = static_cast<std::size_t>(idx);
        const std::size_t i1 = std::min(i0 + 1, in.size() - 1);
        const float frac = idx - static_cast<float>(i0);
        out.push_back((in[i0] * (1.0f - frac)) + (in[i1] * frac));
        idx += ratio;
    }
    return out.empty() ? in : out;
}

void apply_filter24_with_env(std::vector<float>& in, float cutoff_start_hz, float cutoff_end_hz, float env_decay_s, float resonance) {
    drumrom::sample_dsp::apply_filter24_with_env(
        &in,
        static_cast<float>(kSampleRate),
        cutoff_start_hz,
        cutoff_end_hz,
        env_decay_s,
        resonance);
}

void apply_adsr(std::vector<float>& in, float attack_s, float decay_s, float sustain, float release_s) {
    if (in.empty()) {
        return;
    }
    const std::size_t n = in.size();
    const std::size_t a = static_cast<std::size_t>(std::max(0.0f, attack_s) * kSampleRate);
    const std::size_t d = static_cast<std::size_t>(std::max(0.0f, decay_s) * kSampleRate);
    const std::size_t r = static_cast<std::size_t>(std::max(0.0f, release_s) * kSampleRate);
    const float s = clampf(sustain, 0.0f, 1.0f);

    for (std::size_t i = 0; i < n; ++i) {
        float g = 1.0f;
        if (a > 0 && i < a) {
            g = static_cast<float>(i) / static_cast<float>(a);
        } else if (d > 0 && i < (a + d)) {
            const float x = static_cast<float>(i - a) / static_cast<float>(d);
            g = 1.0f + ((s - 1.0f) * x);
        } else {
            g = s;
        }

        if (r > 0 && i >= (n - std::min(r, n))) {
            const std::size_t start = n - std::min(r, n);
            const float x = static_cast<float>(i - start) / static_cast<float>(std::max<std::size_t>(1, n - start));
            g *= (1.0f - x);
        }
        in[i] *= g;
    }
}

void normalize_peak(std::vector<float>& in) {
    float peak = 0.0f;
    for (float v : in) {
        peak = std::max(peak, std::fabs(v));
    }
    if (peak <= 0.00001f) {
        return;
    }
    const float inv = 1.0f / peak;
    for (float& v : in) {
        v *= inv;
    }
}

float apply_output_shaper_sample(float sample, float gain_db, float limiter_ceiling, int output_shaper_mode, float output_saturation) {
    const float linear = std::pow(10.0f, gain_db / 20.0f);
    const float ceiling = clampf(limiter_ceiling, 0.1f, 1.0f);
    const float driven = sample * linear;

    if (output_shaper_mode == 0) {
        return clampf(driven, -1.0f, 1.0f);
    }
    if (output_shaper_mode == 1) {
        return clampf(driven, -ceiling, ceiling);
    }

    const float normalized = driven / ceiling;
    const float sat = clampf(output_saturation, 0.0f, 1.0f);
    const float drive = 1.0f + (sat * 24.0f);
    const float norm = std::tanh(drive);
    if (norm <= 0.00001f) {
        return clampf(driven, -ceiling, ceiling);
    }
    const float shaped = std::tanh(normalized * drive) / norm;
    return clampf(shaped * ceiling, -ceiling, ceiling);
}

std::vector<float> apply_loop_window(const std::vector<float>& in, std::size_t target_len, int loop_start_pct, int loop_end_pct, float loop_increment_pct) {
    if (in.empty() || target_len == 0) {
        return {};
    }

    const int s_pct = std::clamp(loop_start_pct, 0, 99);
    const int e_pct = std::clamp(loop_end_pct, s_pct + 1, 100);
    const std::size_t start0 = (in.size() * static_cast<std::size_t>(s_pct)) / 100;
    const std::size_t end0 = (in.size() * static_cast<std::size_t>(e_pct)) / 100;
    const std::size_t win_len = std::max<std::size_t>(1, end0 > start0 ? (end0 - start0) : 1);
    if (win_len >= in.size()) {
        return in;
    }

    const std::size_t max_start = in.size() - win_len;
    const float span = static_cast<float>(max_start + 1u);
    const float inc = (loop_increment_pct / 100.0f) * static_cast<float>(in.size());
    float start_f = static_cast<float>(std::min(start0, max_start));

    std::vector<float> out;
    out.reserve(target_len);
    while (out.size() < target_len) {
        std::size_t ws = static_cast<std::size_t>(std::clamp(start_f, 0.0f, static_cast<float>(max_start)));
        const std::size_t n = std::min(win_len, target_len - out.size());
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(in[ws + i]);
        }

        if (std::abs(inc) > 0.0001f && span > 1.0f) {
            start_f += inc;
            while (start_f < 0.0f) {
                start_f += span;
            }
            while (start_f >= span) {
                start_f -= span;
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> encode_signed8(const std::vector<float>& in) {
    std::vector<std::uint8_t> out;
    out.reserve(in.size());
    for (float v : in) {
        const int iv = static_cast<int>(clampf(v, -1.0f, 1.0f) * 127.0f);
        out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(iv)));
    }
    return out;
}

std::optional<std::pair<std::vector<float>, int>> load_sample_any_format(const SampleEdit& sample) {
    g_last_sample_error.clear();
    if (sample.path.empty()) {
        g_last_sample_error = "Sample path is empty";
        return std::nullopt;
    }
    const std::filesystem::path resolved = resolve_sample_path(sample.path);
    if (resolved.empty() || !std::filesystem::exists(resolved)) {
        g_last_sample_error = "File not found: " + sample.path;
        return std::nullopt;
    }
    const std::string resolved_str = resolved.string();

    std::string ext = resolved.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".raw") {
        const auto raw = read_raw_signed8(resolved_str);
        if (raw.empty()) {
            g_last_sample_error = "RAW file is empty or unreadable: " + resolved_str;
            return std::nullopt;
        }
        const int raw_rate = std::max(1, static_cast<int>(sample.source_rate_hz));
        return std::make_pair(decode_signed8(raw), raw_rate);
    }

    SF_INFO info{};
    SNDFILE* f = sf_open(resolved_str.c_str(), SFM_READ, &info);
    if (!f || info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        g_last_sample_error = std::string("Unsupported or unreadable audio file: ") + resolved_str;
        if (f) {
            sf_close(f);
        }
        return std::nullopt;
    }

    std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * static_cast<std::size_t>(info.channels));
    const sf_count_t got = sf_readf_float(f, interleaved.data(), info.frames);
    sf_close(f);
    if (got <= 0) {
        g_last_sample_error = "Decoded zero frames: " + resolved_str;
        return std::nullopt;
    }

    std::vector<float> mono(static_cast<std::size_t>(got));
    for (sf_count_t i = 0; i < got; ++i) {
        float acc = 0.0f;
        for (int ch = 0; ch < info.channels; ++ch) {
            acc += interleaved[(static_cast<std::size_t>(i) * static_cast<std::size_t>(info.channels)) + static_cast<std::size_t>(ch)];
        }
        mono[static_cast<std::size_t>(i)] = acc / static_cast<float>(info.channels);
    }

    return std::make_pair(std::move(mono), info.samplerate);
}

std::vector<float> process_sample_preview(const SampleEdit& sample) {
    auto loaded = load_sample_any_format(sample);
    if (!loaded.has_value()) {
        return {};
    }

    auto f = resample_rate_linear(loaded->first, loaded->second, kSampleRate);
    f = sample_range(f, sample.start_pct, sample.end_pct);
    f = tune_resample(f, sample.tune_semitones);
    apply_filter24_with_env(f, sample.filter_cutoff_hz, sample.filter_cutoff_end_hz, sample.filter_env_decay_s, sample.filter_resonance);
    // For preview, apply ADSR based on mode
    if (sample.amp_envelope_mode != AmpEnvelopeMode::Off) {
        apply_adsr(f, sample.amp_attack_s, sample.amp_decay_s, sample.amp_sustain, sample.amp_release_s);
    }
    normalize_peak(f);
    return f;
}

// Process sample without ADSR (will be applied after fitting to slot size)
std::vector<float> process_sample_for_render(const SampleEdit& sample) {
    auto loaded = load_sample_any_format(sample);
    if (!loaded.has_value()) {
        return {};
    }

    auto f = resample_rate_linear(loaded->first, loaded->second, kSampleRate);
    f = sample_range(f, sample.start_pct, sample.end_pct);
    f = tune_resample(f, sample.tune_semitones);
    apply_filter24_with_env(f, sample.filter_cutoff_hz, sample.filter_cutoff_end_hz, sample.filter_env_decay_s, sample.filter_resonance);
    normalize_peak(f);
    return f;
}

// Generate ADSR envelope curve for visualization
std::vector<float> generate_adsr_envelope(std::size_t length, float attack_s, float decay_s, float sustain, float release_s) {
    if (length == 0) return {};
    std::vector<float> env(length);
    
    const std::size_t a = static_cast<std::size_t>(std::max(0.0f, attack_s) * kSampleRate);
    const std::size_t d = static_cast<std::size_t>(std::max(0.0f, decay_s) * kSampleRate);
    const std::size_t r = static_cast<std::size_t>(std::max(0.0f, release_s) * kSampleRate);
    const float s = clampf(sustain, 0.0f, 1.0f);

    for (std::size_t i = 0; i < length; ++i) {
        float g = 1.0f;
        if (a > 0 && i < a) {
            g = static_cast<float>(i) / static_cast<float>(a);
        } else if (d > 0 && i < (a + d)) {
            const float x = static_cast<float>(i - a) / static_cast<float>(d);
            g = 1.0f + ((s - 1.0f) * x);
        } else {
            g = s;
        }

        if (r > 0 && i >= (length - std::min(r, length))) {
            const std::size_t start = length - std::min(r, length);
            const float x = static_cast<float>(i - start) / static_cast<float>(std::max<std::size_t>(1, length - start));
            g *= (1.0f - x);
        }
        env[i] = g;
    }
    return env;
}

bool is_supported_sample_file(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".raw";
}

std::filesystem::path sample_root_path() {
    std::string root = trim_copy(strip_wrapping_quotes(g_settings.samples_folder));
    if (root.empty()) {
        root = "samples";
    }
    return std::filesystem::path(root).lexically_normal();
}

bool sample_path_within_root(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    return drumrom::sample_browser_fs::path_within_root(candidate, root);
}

std::string folder_label(const std::string& folder) {
    return drumrom::sample_browser_fs::folder_label(folder, sample_root_path());
}

std::string sample_browser_entry_label(const std::string& entry_path, const std::filesystem::path& current_folder, const std::filesystem::path& root) {
    return drumrom::sample_browser_fs::entry_label(entry_path, current_folder, root);
}

bool is_supported_preset_file(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ".drum" || e == ".kit" || e == ".slotpreset" || e == ".allslotpreset";
}

std::string preset_folder_label(const std::string& folder) {
    std::error_code ec;
    const std::filesystem::path p(folder);
    const std::filesystem::path root("presets");
    auto rel = std::filesystem::relative(p, root, ec);
    if (ec || rel.empty() || rel == ".") {
        return "presets";
    }
    return std::string("presets/") + rel.string();
}

void sync_sample_path_from_slot() {
    if (g_selected_slot == g_sample_path_slot) {
        return;
    }
    const std::string& path = g_slot_cfg[g_selected_slot].sample.path;
    std::snprintf(g_sample_path_buf, sizeof(g_sample_path_buf), "%s", path.c_str());
    g_sample_path_slot = g_selected_slot;
}

void refresh_sample_files_for_folder() {
    drumrom::sample_browser_fs::refresh_files_for_folder(
        sample_root_path(),
        &g_selected_folder_path,
        &g_current_folder_files,
        &g_selected_sample_file);
}

void refresh_sample_folders() {
    drumrom::sample_browser_fs::refresh_folders(
        sample_root_path(),
        &g_sample_folders,
        &g_selected_folder,
        &g_selected_folder_path,
        &g_current_folder_files,
        &g_selected_sample_file);
}

#include "loop-extract.cpp"

// Helper: Get root folder for current mode (presets or kits)
std::string get_current_preset_root() {
    return drumrom::preset_browser_fs::current_root(g_show_kits_mode);
}

void refresh_preset_files_for_folder() {
    drumrom::preset_browser_fs::refresh_files_for_root(
        g_show_kits_mode,
        &g_current_preset_files,
        &g_selected_preset_file);
}

void refresh_preset_folders() {
    drumrom::preset_browser_fs::refresh_folders(
        &g_preset_folders,
        &g_selected_preset_folder,
        &g_selected_preset_folder_path,
        g_show_kits_mode,
        &g_current_preset_files,
        &g_selected_preset_file);
}

void refresh_sysex_files_for_target(SysexFolderTarget target) {
    const std::filesystem::path root =
        (target == SysexFolderTarget::Sample)
            ? std::filesystem::path("sysex-samples")
            : std::filesystem::path("sysex-rhythm");

    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    g_current_preset_files.clear();
    if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::filesystem::path p = entry.path();
            if (p.extension() == ".syx") {
                g_current_preset_files.push_back(p.string());
            }
        }
    }

    std::sort(g_current_preset_files.begin(), g_current_preset_files.end());
    if (g_current_preset_files.empty()) {
        g_selected_preset_file = -1;
    } else {
        g_selected_preset_file = std::clamp(g_selected_preset_file, 0, static_cast<int>(g_current_preset_files.size()) - 1);
    }
}

void write_raw_signed8(const std::string& path, const std::vector<std::int8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

// ============================================================================
// Preset Serialization
// ============================================================================

// Helper: Convert preset name to full path (prepend presets/ or kits/, append appropriate extension)
std::string preset_name_to_path(const std::string& name, bool is_kit = false) {
    return drumrom::preset_browser_fs::name_to_path(name, is_kit);
}

// Helper: Convert full path to preset name (remove presets/ prefix and extension suffix)
std::string preset_path_to_name(const std::string& path) {
    return drumrom::preset_browser_fs::path_to_name(path);
}

bool save_slot_preset(const std::string& path, const SlotConfig& cfg) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&kPresetMagic), sizeof(kPresetMagic));
    f.write(reinterpret_cast<const char*>(&kPresetVersion), sizeof(kPresetVersion));
    
    std::uint32_t src = static_cast<std::uint32_t>(cfg.source);
    std::uint32_t drm = static_cast<std::uint32_t>(cfg.drum);
    f.write(reinterpret_cast<const char*>(&src), sizeof(src));
    f.write(reinterpret_cast<const char*>(&drm), sizeof(drm));
    f.write(reinterpret_cast<const char*>(&cfg.seed), sizeof(cfg.seed));
    f.write(reinterpret_cast<const char*>(&cfg.params), sizeof(cfg.params));
    
    std::uint32_t sample_path_len = cfg.sample.path.size();
    f.write(reinterpret_cast<const char*>(&sample_path_len), sizeof(sample_path_len));
    f.write(cfg.sample.path.data(), sample_path_len);
    f.write(reinterpret_cast<const char*>(&cfg.sample.source_rate_hz), sizeof(cfg.sample.source_rate_hz));
    f.write(reinterpret_cast<const char*>(&cfg.sample.start_pct), sizeof(cfg.sample.start_pct));
    f.write(reinterpret_cast<const char*>(&cfg.sample.end_pct), sizeof(cfg.sample.end_pct));
    f.write(reinterpret_cast<const char*>(&cfg.sample.loop_start_pct), sizeof(cfg.sample.loop_start_pct));
    f.write(reinterpret_cast<const char*>(&cfg.sample.loop_end_pct), sizeof(cfg.sample.loop_end_pct));
    f.write(reinterpret_cast<const char*>(&cfg.sample.loop_increment_pct), sizeof(cfg.sample.loop_increment_pct));
    f.write(reinterpret_cast<const char*>(&cfg.sample.tune_semitones), sizeof(cfg.sample.tune_semitones));
    f.write(reinterpret_cast<const char*>(&cfg.sample.filter_cutoff_hz), sizeof(cfg.sample.filter_cutoff_hz));
    f.write(reinterpret_cast<const char*>(&cfg.sample.filter_cutoff_end_hz), sizeof(cfg.sample.filter_cutoff_end_hz));
    f.write(reinterpret_cast<const char*>(&cfg.sample.filter_env_decay_s), sizeof(cfg.sample.filter_env_decay_s));
    f.write(reinterpret_cast<const char*>(&cfg.sample.filter_resonance), sizeof(cfg.sample.filter_resonance));
    f.write(reinterpret_cast<const char*>(&cfg.sample.amp_attack_s), sizeof(cfg.sample.amp_attack_s));
    f.write(reinterpret_cast<const char*>(&cfg.sample.amp_decay_s), sizeof(cfg.sample.amp_decay_s));
    f.write(reinterpret_cast<const char*>(&cfg.sample.amp_sustain), sizeof(cfg.sample.amp_sustain));
    f.write(reinterpret_cast<const char*>(&cfg.sample.amp_release_s), sizeof(cfg.sample.amp_release_s));
    
    // v4: gain and limiter
    f.write(reinterpret_cast<const char*>(&cfg.output_gain_db), sizeof(cfg.output_gain_db));
    f.write(reinterpret_cast<const char*>(&cfg.limiter_ceiling), sizeof(cfg.limiter_ceiling));
    // v5: output shaper mode and saturation
    f.write(reinterpret_cast<const char*>(&cfg.output_shaper_mode), sizeof(cfg.output_shaper_mode));
    f.write(reinterpret_cast<const char*>(&cfg.output_saturation), sizeof(cfg.output_saturation));
    // v7: amp envelope mode
    std::uint32_t amp_env_mode = static_cast<std::uint32_t>(cfg.sample.amp_envelope_mode);
    f.write(reinterpret_cast<const char*>(&amp_env_mode), sizeof(amp_env_mode));
    // v8: elements params
    f.write(reinterpret_cast<const char*>(&cfg.elements_params), sizeof(cfg.elements_params));
    // v10: exact Mutable Elements params
    f.write(reinterpret_cast<const char*>(&cfg.elements_exact_params), sizeof(cfg.elements_exact_params));
    
    return static_cast<bool>(f);
}

bool load_slot_preset(const std::string& path, SlotConfig& cfg) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::uint32_t magic, version;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (magic != kPresetMagic) return false;
    if (version > kPresetVersion) return false;

    std::uint32_t src, drm;
    f.read(reinterpret_cast<char*>(&src), sizeof(src));
    f.read(reinterpret_cast<char*>(&drm), sizeof(drm));
    // Migrate: old SourceKind::Elements (=3) → Synth + DrumKind::Elements
    if (src == 3) {
        cfg.source = SourceKind::Synth;
        cfg.drum = DrumKind::Elements;
    } else {
        cfg.source = static_cast<SourceKind>(src);
        cfg.drum = static_cast<DrumKind>(drm);
    }
    
    f.read(reinterpret_cast<char*>(&cfg.seed), sizeof(cfg.seed));
    cfg.params = DrumParams{};
    if (version >= 9) {
        f.read(reinterpret_cast<char*>(&cfg.params), sizeof(cfg.params));
    } else if (!read_legacy_drum_params(f, version, cfg.params)) {
        return false;
    }

    std::uint32_t sample_path_len;
    f.read(reinterpret_cast<char*>(&sample_path_len), sizeof(sample_path_len));
    if (sample_path_len > 1000) return false;
    
    std::vector<char> path_buf(sample_path_len);
    f.read(path_buf.data(), sample_path_len);
    cfg.sample.path.assign(path_buf.data(), sample_path_len);
    
    f.read(reinterpret_cast<char*>(&cfg.sample.source_rate_hz), sizeof(cfg.sample.source_rate_hz));
    f.read(reinterpret_cast<char*>(&cfg.sample.start_pct), sizeof(cfg.sample.start_pct));
    f.read(reinterpret_cast<char*>(&cfg.sample.end_pct), sizeof(cfg.sample.end_pct));
    if (version >= 6) {
        f.read(reinterpret_cast<char*>(&cfg.sample.loop_start_pct), sizeof(cfg.sample.loop_start_pct));
        f.read(reinterpret_cast<char*>(&cfg.sample.loop_end_pct), sizeof(cfg.sample.loop_end_pct));
        f.read(reinterpret_cast<char*>(&cfg.sample.loop_increment_pct), sizeof(cfg.sample.loop_increment_pct));
    } else {
        cfg.sample.loop_start_pct = cfg.sample.start_pct;
        cfg.sample.loop_end_pct = cfg.sample.end_pct;
        cfg.sample.loop_increment_pct = 0.0f;
    }
    f.read(reinterpret_cast<char*>(&cfg.sample.tune_semitones), sizeof(cfg.sample.tune_semitones));
    f.read(reinterpret_cast<char*>(&cfg.sample.filter_cutoff_hz), sizeof(cfg.sample.filter_cutoff_hz));
    f.read(reinterpret_cast<char*>(&cfg.sample.filter_cutoff_end_hz), sizeof(cfg.sample.filter_cutoff_end_hz));
    f.read(reinterpret_cast<char*>(&cfg.sample.filter_env_decay_s), sizeof(cfg.sample.filter_env_decay_s));
    f.read(reinterpret_cast<char*>(&cfg.sample.filter_resonance), sizeof(cfg.sample.filter_resonance));
    f.read(reinterpret_cast<char*>(&cfg.sample.amp_attack_s), sizeof(cfg.sample.amp_attack_s));
    f.read(reinterpret_cast<char*>(&cfg.sample.amp_decay_s), sizeof(cfg.sample.amp_decay_s));
    f.read(reinterpret_cast<char*>(&cfg.sample.amp_sustain), sizeof(cfg.sample.amp_sustain));
    f.read(reinterpret_cast<char*>(&cfg.sample.amp_release_s), sizeof(cfg.sample.amp_release_s));

    // v4 backward compat
    if (version >= 4) {
        f.read(reinterpret_cast<char*>(&cfg.output_gain_db), sizeof(cfg.output_gain_db));
        f.read(reinterpret_cast<char*>(&cfg.limiter_ceiling), sizeof(cfg.limiter_ceiling));
    } else {
        cfg.output_gain_db = 0.0f;
        cfg.limiter_ceiling = 1.0f;
    }

    // v5 backward compat
    if (version >= 5) {
        f.read(reinterpret_cast<char*>(&cfg.output_shaper_mode), sizeof(cfg.output_shaper_mode));
        f.read(reinterpret_cast<char*>(&cfg.output_saturation), sizeof(cfg.output_saturation));
        if (version < 6) {
            if (cfg.output_shaper_mode == 0) {
                cfg.output_shaper_mode = 1;
            } else if (cfg.output_shaper_mode == 1) {
                cfg.output_shaper_mode = 2;
            }
        }
    } else {
        cfg.output_shaper_mode = 2;
        cfg.output_saturation = 0.65f;
    }

    // v7 backward compat
    if (version >= 7) {
        std::uint32_t amp_env_mode;
        f.read(reinterpret_cast<char*>(&amp_env_mode), sizeof(amp_env_mode));
        cfg.sample.amp_envelope_mode = static_cast<AmpEnvelopeMode>(amp_env_mode);
    } else {
        cfg.sample.amp_envelope_mode = AmpEnvelopeMode::Output;
    }

    // v8: elements params
    if (version >= 8) {
        f.read(reinterpret_cast<char*>(&cfg.elements_params), sizeof(cfg.elements_params));
    } else {
        cfg.elements_params = drumrom::synth::ElementsParams{};
    }

    // v10: exact Mutable Elements params
    if (version >= 10) {
        f.read(reinterpret_cast<char*>(&cfg.elements_exact_params), sizeof(cfg.elements_exact_params));
    } else {
        cfg.elements_exact_params = drumrom::synth::ElementsExactParams{};
    }

    return static_cast<bool>(f);
}

bool write_slot_preset_payload(std::ostream& out, const SlotConfig& cfg) {
    const std::uint32_t source = static_cast<std::uint32_t>(cfg.source);
    const std::uint32_t drum = static_cast<std::uint32_t>(cfg.drum);
    const std::uint32_t seed = cfg.seed;
    const std::uint32_t path_len = static_cast<std::uint32_t>(cfg.sample.path.size());

    out.write(reinterpret_cast<const char*>(&source), sizeof(source));
    out.write(reinterpret_cast<const char*>(&drum), sizeof(drum));
    out.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    out.write(reinterpret_cast<const char*>(&cfg.params), sizeof(cfg.params));
    out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
    if (path_len > 0) {
        out.write(cfg.sample.path.data(), static_cast<std::streamsize>(path_len));
    }
    out.write(reinterpret_cast<const char*>(&cfg.sample.source_rate_hz), sizeof(cfg.sample.source_rate_hz));
    out.write(reinterpret_cast<const char*>(&cfg.sample.start_pct), sizeof(cfg.sample.start_pct));
    out.write(reinterpret_cast<const char*>(&cfg.sample.end_pct), sizeof(cfg.sample.end_pct));
    out.write(reinterpret_cast<const char*>(&cfg.sample.loop_start_pct), sizeof(cfg.sample.loop_start_pct));
    out.write(reinterpret_cast<const char*>(&cfg.sample.loop_end_pct), sizeof(cfg.sample.loop_end_pct));
    out.write(reinterpret_cast<const char*>(&cfg.sample.loop_increment_pct), sizeof(cfg.sample.loop_increment_pct));
    out.write(reinterpret_cast<const char*>(&cfg.sample.tune_semitones), sizeof(cfg.sample.tune_semitones));
    out.write(reinterpret_cast<const char*>(&cfg.sample.filter_cutoff_hz), sizeof(cfg.sample.filter_cutoff_hz));
    out.write(reinterpret_cast<const char*>(&cfg.sample.filter_cutoff_end_hz), sizeof(cfg.sample.filter_cutoff_end_hz));
    out.write(reinterpret_cast<const char*>(&cfg.sample.filter_env_decay_s), sizeof(cfg.sample.filter_env_decay_s));
    out.write(reinterpret_cast<const char*>(&cfg.sample.filter_resonance), sizeof(cfg.sample.filter_resonance));
    out.write(reinterpret_cast<const char*>(&cfg.sample.amp_attack_s), sizeof(cfg.sample.amp_attack_s));
    out.write(reinterpret_cast<const char*>(&cfg.sample.amp_decay_s), sizeof(cfg.sample.amp_decay_s));
    out.write(reinterpret_cast<const char*>(&cfg.sample.amp_sustain), sizeof(cfg.sample.amp_sustain));
    out.write(reinterpret_cast<const char*>(&cfg.sample.amp_release_s), sizeof(cfg.sample.amp_release_s));
    out.write(reinterpret_cast<const char*>(&cfg.output_gain_db), sizeof(cfg.output_gain_db));
    out.write(reinterpret_cast<const char*>(&cfg.limiter_ceiling), sizeof(cfg.limiter_ceiling));
    out.write(reinterpret_cast<const char*>(&cfg.output_shaper_mode), sizeof(cfg.output_shaper_mode));
    out.write(reinterpret_cast<const char*>(&cfg.output_saturation), sizeof(cfg.output_saturation));
    std::uint32_t amp_env_mode = static_cast<std::uint32_t>(cfg.sample.amp_envelope_mode);
    out.write(reinterpret_cast<const char*>(&amp_env_mode), sizeof(amp_env_mode));
    // v8: elements params
    out.write(reinterpret_cast<const char*>(&cfg.elements_params), sizeof(cfg.elements_params));
    // v10: exact Mutable Elements params
    out.write(reinterpret_cast<const char*>(&cfg.elements_exact_params), sizeof(cfg.elements_exact_params));
    return static_cast<bool>(out);
}

bool save_all_slot_presets(const std::string& path, const std::array<SlotConfig, kSlots.size()>& slots) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const std::uint32_t magic = kPresetAllMagic;
    const std::uint32_t ver = kPresetVersion;
    const std::uint32_t count = static_cast<std::uint32_t>(slots.size());
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::uint32_t slot_index = static_cast<std::uint32_t>(i);
        out.write(reinterpret_cast<const char*>(&slot_index), sizeof(slot_index));
        if (!write_slot_preset_payload(out, slots[i])) {
            return false;
        }
    }

    return static_cast<bool>(out);
}

bool load_all_slot_presets(const std::string& path, std::array<SlotConfig, kSlots.size()>& slots) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::uint32_t magic, ver, count;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&ver), sizeof(ver));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != kPresetAllMagic) return false;
    if (ver > kPresetVersion) return false;
    if (count == 0 || count > 256) return false;

    auto map_slot_index = [&](std::uint32_t slot_index) -> std::optional<std::size_t> {
        if (slot_index >= static_cast<std::uint32_t>(kSlots.size())) {
            return std::nullopt;
        }

        // Legacy layout (10 slots): tom1, tom2, tom3, kick, snare, rimshot, closed_hihat, open_hihat, metronome, empty
        // Current layout (16 slots): top row then bottom row RZ-1 pad arrangement.
        if (count == 10) {
            switch (slot_index) {
                case 0: return 0;   // tom1
                case 1: return 8;   // tom2
                case 2: return 1;   // tom3
                case 3: return 9;   // kick (BD)
                case 4: return 10;  // snare (SD)
                case 5: return 2;   // rimshot
                case 6: return 11;  // closed_hihat
                case 7: return 3;   // open_hihat
                // metronome_click and empty no longer have dedicated pads.
                case 8:
                case 9:
                default:
                    return std::nullopt;
            }
        }

        return static_cast<std::size_t>(slot_index);
    };

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t slot_index;
        in.read(reinterpret_cast<char*>(&slot_index), sizeof(slot_index));

        SlotConfig loaded_cfg{};

        std::uint32_t src, drm;
        in.read(reinterpret_cast<char*>(&src), sizeof(src));
        in.read(reinterpret_cast<char*>(&drm), sizeof(drm));
        // Migrate: old SourceKind::Elements (=3) → Synth + DrumKind::Elements
        if (src == 3) {
            loaded_cfg.source = SourceKind::Synth;
            loaded_cfg.drum = DrumKind::Elements;
        } else {
            loaded_cfg.source = static_cast<SourceKind>(src);
            loaded_cfg.drum = static_cast<DrumKind>(drm);
        }

        in.read(reinterpret_cast<char*>(&loaded_cfg.seed), sizeof(loaded_cfg.seed));
        loaded_cfg.params = DrumParams{};
        if (ver >= 9) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.params), sizeof(loaded_cfg.params));
        } else if (!read_legacy_drum_params(in, ver, loaded_cfg.params)) {
            return false;
        }

        std::uint32_t sample_path_len;
        in.read(reinterpret_cast<char*>(&sample_path_len), sizeof(sample_path_len));
        if (sample_path_len > 1000) return false;

        std::vector<char> path_buf(sample_path_len);
        in.read(path_buf.data(), sample_path_len);
        loaded_cfg.sample.path.assign(path_buf.data(), sample_path_len);

        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.source_rate_hz), sizeof(loaded_cfg.sample.source_rate_hz));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.start_pct), sizeof(loaded_cfg.sample.start_pct));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.end_pct), sizeof(loaded_cfg.sample.end_pct));
        if (ver >= 6) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.sample.loop_start_pct), sizeof(loaded_cfg.sample.loop_start_pct));
            in.read(reinterpret_cast<char*>(&loaded_cfg.sample.loop_end_pct), sizeof(loaded_cfg.sample.loop_end_pct));
            in.read(reinterpret_cast<char*>(&loaded_cfg.sample.loop_increment_pct), sizeof(loaded_cfg.sample.loop_increment_pct));
        } else {
            loaded_cfg.sample.loop_start_pct = loaded_cfg.sample.start_pct;
            loaded_cfg.sample.loop_end_pct = loaded_cfg.sample.end_pct;
            loaded_cfg.sample.loop_increment_pct = 0.0f;
        }
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.tune_semitones), sizeof(loaded_cfg.sample.tune_semitones));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.filter_cutoff_hz), sizeof(loaded_cfg.sample.filter_cutoff_hz));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.filter_cutoff_end_hz), sizeof(loaded_cfg.sample.filter_cutoff_end_hz));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.filter_env_decay_s), sizeof(loaded_cfg.sample.filter_env_decay_s));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.filter_resonance), sizeof(loaded_cfg.sample.filter_resonance));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.amp_attack_s), sizeof(loaded_cfg.sample.amp_attack_s));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.amp_decay_s), sizeof(loaded_cfg.sample.amp_decay_s));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.amp_sustain), sizeof(loaded_cfg.sample.amp_sustain));
        in.read(reinterpret_cast<char*>(&loaded_cfg.sample.amp_release_s), sizeof(loaded_cfg.sample.amp_release_s));

        if (ver >= 4) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.output_gain_db), sizeof(loaded_cfg.output_gain_db));
            in.read(reinterpret_cast<char*>(&loaded_cfg.limiter_ceiling), sizeof(loaded_cfg.limiter_ceiling));
        } else {
            loaded_cfg.output_gain_db = 0.0f;
            loaded_cfg.limiter_ceiling = 1.0f;
        }

        if (ver >= 5) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.output_shaper_mode), sizeof(loaded_cfg.output_shaper_mode));
            in.read(reinterpret_cast<char*>(&loaded_cfg.output_saturation), sizeof(loaded_cfg.output_saturation));
            if (ver < 6) {
                if (loaded_cfg.output_shaper_mode == 0) {
                    loaded_cfg.output_shaper_mode = 1;
                } else if (loaded_cfg.output_shaper_mode == 1) {
                    loaded_cfg.output_shaper_mode = 2;
                }
            }
        } else {
            loaded_cfg.output_shaper_mode = 2;
            loaded_cfg.output_saturation = 0.65f;
        }

        if (ver >= 7) {
            std::uint32_t amp_env_mode;
            in.read(reinterpret_cast<char*>(&amp_env_mode), sizeof(amp_env_mode));
            loaded_cfg.sample.amp_envelope_mode = static_cast<AmpEnvelopeMode>(amp_env_mode);
        } else {
            loaded_cfg.sample.amp_envelope_mode = AmpEnvelopeMode::Output;
        }

        // v8: elements params
        if (ver >= 8) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.elements_params), sizeof(loaded_cfg.elements_params));
        } else {
            loaded_cfg.elements_params = drumrom::synth::ElementsParams{};
        }

        // v10: exact Mutable Elements params
        if (ver >= 10) {
            in.read(reinterpret_cast<char*>(&loaded_cfg.elements_exact_params), sizeof(loaded_cfg.elements_exact_params));
        } else {
            loaded_cfg.elements_exact_params = drumrom::synth::ElementsExactParams{};
        }

        const std::optional<std::size_t> mapped_index = map_slot_index(slot_index);
        if (mapped_index.has_value() && mapped_index.value() < slots.size()) {
            slots[mapped_index.value()] = loaded_cfg;
            slots[mapped_index.value()].params.sample_rate = kSampleRate;
        }
    }

    return static_cast<bool>(in);
}

std::string default_rz1_kit_path() {
    const std::filesystem::path p = std::filesystem::path("kits") / "default-rz1-kit.kit";
    return p.string();
}

bool build_factory_default_rz1_kit(std::array<SlotConfig, kSlots.size()>& slots_out) {
    bool all_ok = true;
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        SlotConfig cfg{};
        cfg.params.sample_rate = kSampleRate;
        cfg.source = SourceKind::Sample;
        if (!ensure_default_sample_for_slot(i, cfg)) {
            all_ok = false;
        }
        slots_out[i] = cfg;
    }
    return all_ok;
}

bool restore_default_rz1_kit_file() {
    std::array<SlotConfig, kSlots.size()> slots{};
    const bool built_ok = build_factory_default_rz1_kit(slots);
    const std::string path = default_rz1_kit_path();
    if (!save_all_slot_presets(path, slots)) {
        return false;
    }
    return built_ok;
}

bool load_default_rz1_kit_into_editor() {
    const std::string path = default_rz1_kit_path();
    if (!std::filesystem::exists(path)) {
        if (!restore_default_rz1_kit_file()) {
            return false;
        }
    }

    if (!load_all_slot_presets(path, g_slot_cfg)) {
        if (!restore_default_rz1_kit_file()) {
            return false;
        }
        if (!load_all_slot_presets(path, g_slot_cfg)) {
            return false;
        }
    }

    for (auto& slot : g_slot_cfg) {
        slot.params.sample_rate = kSampleRate;
    }
    g_sample_path_slot = static_cast<std::size_t>(-1);
    g_wave_preview_dirty = true;
    g_params_dirty = true;
    return true;
}

bool load_rom_ab_samples_into_editor() {
    return fetch_rom_ab_samples_from_onerom_devices();
}

// ============================================================================
// Sample/Synth rendering
// ============================================================================

std::string shell_escape_single(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::pair<int, std::string> run_command_capture(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        return {-1, "Failed to start command"};
    }

    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        out += buffer;
    }
    const int rc = pclose(pipe);
    return {rc, out};
}

std::uint32_t read_le_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_le_u32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::vector<std::uint8_t> pack_pin_matrix_bits() {
    std::vector<std::uint8_t> packed(kPinMatrixPackedBytes, 0u);
    for (std::size_t y = 0; y < kEpromPinCount; ++y) {
        for (std::size_t x = 0; x < kEpromPinCount; ++x) {
            if (g_pinbend_matrix[y][x] == 0u) {
                continue;
            }
            const std::size_t idx = (y * kEpromPinCount) + x;
            packed[idx >> 3] |= static_cast<std::uint8_t>(1u << (idx & 7u));
        }
    }
    return packed;
}

bool write_binary_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool read_binary_file_exact(const std::string& path, std::size_t expected_size, std::vector<std::uint8_t>* out_bytes) {
    if (out_bytes == nullptr) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() != expected_size) {
        return false;
    }
    *out_bytes = std::move(bytes);
    return true;
}

bool send_pin_matrix_to_onerom_usb() {
    std::filesystem::create_directories("build");
    const std::string ptr_path = "build/pinbend_ctx_ptr.bin";
    const std::string hdr_path = "build/pinbend_ctx_hdr.bin";
    const std::string matrix_path = "build/pinbend_matrix_payload.bin";
    const std::string commit_path = "build/pinbend_commit_payload.bin";

    const std::string peek_ptr_cmd =
        "onerom inspect peek memory --address " + std::to_string(kOneromUserPluginContextPtrAddr) +
        " --length 4 --output " + shell_escape_single(ptr_path);
    const auto [peek_ptr_rc, peek_ptr_out] = run_command_capture(peek_ptr_cmd);
    if (peek_ptr_rc != 0) {
        set_status(std::string("Matrix send failed (ctx ptr): ") + (peek_ptr_out.empty() ? "(no output)" : peek_ptr_out));
        return false;
    }

    std::vector<std::uint8_t> ptr_bytes;
    if (!read_binary_file_exact(ptr_path, 4u, &ptr_bytes)) {
        set_status("Matrix send failed: could not read context pointer bytes");
        return false;
    }
    const std::uint32_t ctx_addr = read_le_u32(ptr_bytes.data());
    if (ctx_addr == 0u) {
        set_status("Matrix send failed: user plugin context is null");
        return false;
    }

    const std::string peek_hdr_cmd =
        "onerom inspect peek memory --address " + std::to_string(ctx_addr) +
        " --length 16 --output " + shell_escape_single(hdr_path);
    const auto [peek_hdr_rc, peek_hdr_out] = run_command_capture(peek_hdr_cmd);
    if (peek_hdr_rc != 0) {
        set_status(std::string("Matrix send failed (ctx hdr): ") + (peek_hdr_out.empty() ? "(no output)" : peek_hdr_out));
        return false;
    }

    std::vector<std::uint8_t> hdr_bytes;
    if (!read_binary_file_exact(hdr_path, 16u, &hdr_bytes)) {
        set_status("Matrix send failed: could not read plugin header bytes");
        return false;
    }

    const std::uint32_t magic = read_le_u32(hdr_bytes.data() + 0);
    const std::uint32_t abi_version = read_le_u32(hdr_bytes.data() + 4);
    const std::uint32_t matrix_bytes = read_le_u32(hdr_bytes.data() + 8);
    const std::uint32_t commit_seq = read_le_u32(hdr_bytes.data() + 12);

    if (magic != kPinbendContextMagic || abi_version != kPinbendContextAbiVersion) {
        set_status("Matrix send failed: plugin context signature mismatch");
        return false;
    }
    if (matrix_bytes != static_cast<std::uint32_t>(kPinMatrixPackedBytes)) {
        set_status("Matrix send failed: plugin matrix size mismatch");
        return false;
    }

    const std::vector<std::uint8_t> packed = pack_pin_matrix_bits();
    if (!write_binary_file(matrix_path, packed)) {
        set_status("Matrix send failed: could not write matrix payload file");
        return false;
    }

    const std::uint32_t matrix_addr = ctx_addr + kPinbendContextMatrixOffset;
    const std::string poke_matrix_cmd =
        "onerom control poke memory --address " + std::to_string(matrix_addr) +
        " --input " + shell_escape_single(matrix_path);
    const auto [poke_matrix_rc, poke_matrix_out] = run_command_capture(poke_matrix_cmd);
    if (poke_matrix_rc != 0) {
        set_status(std::string("Matrix send failed (write matrix): ") + (poke_matrix_out.empty() ? "(no output)" : poke_matrix_out));
        return false;
    }

    std::vector<std::uint8_t> commit_bytes(4u, 0u);
    write_le_u32(commit_bytes.data(), commit_seq + 1u);
    if (!write_binary_file(commit_path, commit_bytes)) {
        set_status("Matrix send failed: could not write commit payload file");
        return false;
    }

    const std::uint32_t commit_addr = ctx_addr + kPinbendContextCommitOffset;
    const std::string poke_commit_cmd =
        "onerom control poke memory --address " + std::to_string(commit_addr) +
        " --input " + shell_escape_single(commit_path);
    const auto [poke_commit_rc, poke_commit_out] = run_command_capture(poke_commit_cmd);
    if (poke_commit_rc != 0) {
        set_status(std::string("Matrix send failed (commit): ") + (poke_commit_out.empty() ? "(no output)" : poke_commit_out));
        return false;
    }

    set_status("Pin matrix sent to One ROM user plugin");
    return true;
}

std::string summarize_command_output(const std::string& output) {
    if (output.empty()) {
        return "(no output)";
    }
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(output.substr(start));
            break;
        }
        lines.push_back(output.substr(start, end - start));
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines.empty() ? "(no output)" : lines.back();
}

void copy_cstr_to_buffer(char* dst, std::size_t dst_capacity, const std::string& src) {
    if (dst == nullptr || dst_capacity == 0) {
        return;
    }
    std::snprintf(dst, dst_capacity, "%s", src.c_str());
}

int find_onerom_serial_index(const std::string& serial) {
    if (serial.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < g_available_onerom_serials.size(); ++i) {
        if (g_available_onerom_serials[i] == serial) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void sync_onerom_selection_from_settings() {
    g_selected_onerom_rom_a = find_onerom_serial_index(g_settings.onerom_serial_rom_a);
    g_selected_onerom_rom_b = find_onerom_serial_index(g_settings.onerom_serial_rom_b);

    if (g_available_onerom_serials.size() == 1) {
        const std::string& only_serial = g_available_onerom_serials[0];
        if (g_settings.onerom_single_device_role == 0) {
            g_selected_onerom_rom_a = 0;
            g_selected_onerom_rom_b = -1;
            copy_cstr_to_buffer(g_settings.onerom_serial_rom_a, sizeof(g_settings.onerom_serial_rom_a), only_serial);
            g_settings.onerom_serial_rom_b[0] = '\0';
        } else {
            g_selected_onerom_rom_a = -1;
            g_selected_onerom_rom_b = 0;
            copy_cstr_to_buffer(g_settings.onerom_serial_rom_b, sizeof(g_settings.onerom_serial_rom_b), only_serial);
            g_settings.onerom_serial_rom_a[0] = '\0';
        }
    }
}

void refresh_onerom_usb_devices() {
    const auto [rc, output] = run_command_capture("onerom scan");
    if (rc != 0) {
        g_available_onerom_serials.clear();
        g_selected_onerom_rom_a = -1;
        g_selected_onerom_rom_b = -1;
        g_onerom_usb_status = std::string("OneROM scan failed: ") + summarize_command_output(output);
        return;
    }

    g_available_onerom_serials = drumrom::onerom_usb_routing::parse_serials_from_scan_output(output);
    sync_onerom_selection_from_settings();
    g_onerom_usb_status = std::to_string(g_available_onerom_serials.size()) + " OneROM USB device(s) detected";
}

bool is_rom_slot_index(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }
    return drumrom::onerom_usb_routing::slot_belongs_to_any_rom(kSlots[slot_idx].name);
}

bool is_rom_a_slot_index(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }
    return drumrom::onerom_usb_routing::slot_belongs_to_rom_a(kSlots[slot_idx].name);
}

bool is_rom_b_slot_index(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }
    return drumrom::onerom_usb_routing::slot_belongs_to_rom_b(kSlots[slot_idx].name);
}

bool resolve_onerom_serial_for_slot(std::size_t slot_idx, std::string* serial_out, std::string* error_out) {
    if (serial_out == nullptr) {
        return false;
    }
    serial_out->clear();

    if (!is_rom_slot_index(slot_idx)) {
        if (error_out != nullptr) {
            *error_out = "Selected slot is not a ROM slot";
        }
        return false;
    }

    if (g_available_onerom_serials.empty()) {
        // Fall back to default selection behavior from onerom CLI.
        return true;
    }

    const bool slot_is_rom_a = is_rom_a_slot_index(slot_idx);
    const bool slot_is_rom_b = is_rom_b_slot_index(slot_idx);

    if (g_available_onerom_serials.size() == 1) {
        const bool role_matches = (slot_is_rom_a && g_settings.onerom_single_device_role == 0) ||
                                  (slot_is_rom_b && g_settings.onerom_single_device_role == 1);
        if (!role_matches) {
            if (error_out != nullptr) {
                *error_out = "Single OneROM device role does not match this slot (set role in Settings)";
            }
            return false;
        }
        *serial_out = g_available_onerom_serials[0];
        return true;
    }

    if (slot_is_rom_a) {
        const std::string serial = trim_copy(g_settings.onerom_serial_rom_a);
        if (serial.empty()) {
            if (error_out != nullptr) {
                *error_out = "No OneROM assigned for ROM A";
            }
            return false;
        }
        *serial_out = serial;
        return true;
    }

    if (slot_is_rom_b) {
        const std::string serial = trim_copy(g_settings.onerom_serial_rom_b);
        if (serial.empty()) {
            if (error_out != nullptr) {
                *error_out = "No OneROM assigned for ROM B";
            }
            return false;
        }
        *serial_out = serial;
        return true;
    }

    if (error_out != nullptr) {
        *error_out = "Slot does not map to ROM A or ROM B";
    }
    return false;
}

std::string build_onerom_base_command(const std::string& serial) {
    if (serial.empty()) {
        return "onerom";
    }
    return "onerom --serial " + shell_escape_single(serial);
}

bool write_slot_file(std::size_t slot_index, const std::vector<std::int8_t>& data) {
    std::filesystem::create_directories("samples");
    const std::string path = std::string("samples/") + kSlots[slot_index].name + ".raw";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return true;
}

bool fetch_rom_ab_samples_from_onerom_devices() {
    refresh_onerom_usb_devices();
    if (g_available_onerom_serials.empty()) {
        set_status("Get ROM failed: no OneROM USB device detected (scan in Settings)");
        return false;
    }

    std::size_t rom_a_slot = kSlots.size();
    std::size_t rom_b_slot = kSlots.size();
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (rom_a_slot == kSlots.size() && is_rom_a_slot_index(i)) {
            rom_a_slot = i;
        }
        if (rom_b_slot == kSlots.size() && is_rom_b_slot_index(i)) {
            rom_b_slot = i;
        }
    }
    if (rom_a_slot >= kSlots.size() || rom_b_slot >= kSlots.size()) {
        set_status("ROM slot mapping is incomplete");
        return false;
    }

    std::string serial_a;
    std::string serial_b;
    std::string route_err;
    if (!resolve_onerom_serial_for_slot(rom_a_slot, &serial_a, &route_err)) {
        set_status(std::string("Get ROM A failed: ") + route_err);
        return false;
    }
    if (!resolve_onerom_serial_for_slot(rom_b_slot, &serial_b, &route_err)) {
        set_status(std::string("Get ROM B failed: ") + route_err);
        return false;
    }

    std::filesystem::create_directories("build");
    const std::string rom_a_dump = "build/live_rom_a.bin";
    const std::string rom_b_dump = "build/live_rom_b.bin";
    const std::string peek_a_cmd =
        build_onerom_base_command(serial_a) + " peek --yes --address 0 --output " + shell_escape_single(rom_a_dump);
    const std::string peek_b_cmd =
        build_onerom_base_command(serial_b) + " peek --yes --address 0 --output " + shell_escape_single(rom_b_dump);

    const auto [peek_a_rc, peek_a_out] = run_command_capture(peek_a_cmd);
    if (peek_a_rc != 0) {
        set_status(std::string("Get ROM A failed: ") + summarize_command_output(peek_a_out));
        return false;
    }
    const auto [peek_b_rc, peek_b_out] = run_command_capture(peek_b_cmd);
    if (peek_b_rc != 0) {
        set_status(std::string("Get ROM B failed: ") + summarize_command_output(peek_b_out));
        return false;
    }

    constexpr std::size_t kRomImageBytes = 32768u;
    std::vector<std::uint8_t> rom_a_bytes;
    std::vector<std::uint8_t> rom_b_bytes;
    if (!read_binary_file_exact(rom_a_dump, kRomImageBytes, &rom_a_bytes)) {
        set_status("Get ROM A failed: unexpected dump size");
        return false;
    }
    if (!read_binary_file_exact(rom_b_dump, kRomImageBytes, &rom_b_bytes)) {
        set_status("Get ROM B failed: unexpected dump size");
        return false;
    }

    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (!is_rom_slot_index(i)) {
            continue;
        }

        const auto& source_bytes = is_rom_a_slot_index(i) ? rom_a_bytes : rom_b_bytes;
        const std::size_t slot_start = get_slot_upload_start_address(i);
        const std::size_t slot_size = kSlots[i].size;
        if (slot_start + slot_size > source_bytes.size()) {
            set_status(std::string("Get ROM failed: slot range out of ROM bounds for ") + kSlots[i].name);
            return false;
        }

        std::vector<std::int8_t> raw(slot_size);
        for (std::size_t j = 0; j < slot_size; ++j) {
            raw[j] = static_cast<std::int8_t>(source_bytes[slot_start + j]);
        }
        if (!write_slot_file(i, raw)) {
            set_status(std::string("Get ROM failed: could not write samples/") + kSlots[i].name + ".raw");
            return false;
        }

        SlotConfig& cfg = g_slot_cfg[i];
        cfg.source = SourceKind::Sample;
        cfg.sample.path = std::string("samples/") + kSlots[i].name + ".raw";
        cfg.sample.source_rate_hz = 20833.0f;
        cfg.sample.start_pct = 0;
        cfg.sample.end_pct = 100;
        cfg.sample.loop_start_pct = 0;
        cfg.sample.loop_end_pct = 100;
        cfg.sample.loop_increment_pct = 0.0f;
        cfg.params.sample_rate = kSampleRate;
    }

    g_sample_path_slot = static_cast<std::size_t>(-1);
    g_wave_preview_dirty = true;
    g_params_dirty = true;
    set_status("Fetched ROM A/B from OneROM devices into editor slots");
    return true;
}

bool render_one_slot(std::size_t slot_index) {
    if (slot_index >= kSlots.size()) {
        return false;
    }
    if (!is_slot_enabled(slot_index)) {
        return true;
    }
    // Skip RAM sample pads with no path — they are not part of ROM and don't need rendering
    if (kSlots[slot_index].is_ram_sample && g_slot_cfg[slot_index].sample.path.empty()) {
        return true;
    }
    std::mt19937 rng(g_slot_cfg[slot_index].seed);
    const auto data = render_slot(slot_index, rng);
    if (data.empty()) {
        set_status(std::string("Sample load failed for slot ") + kSlots[slot_index].name + ": " +
                   (g_last_sample_error.empty() ? "unknown error" : g_last_sample_error));
        return false;
    }
    if (!write_slot_file(slot_index, data)) {
        set_status(std::string("Failed to write slot ") + kSlots[slot_index].name);
        return false;
    }
    set_status(std::string("Rendered slot ") + kSlots[slot_index].name + " -> samples/" + kSlots[slot_index].name + ".raw");
    return true;
}

bool render_all_slots() {
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (!render_one_slot(i)) {
            return false;
        }
    }
    set_status("Rendered all slots to samples/*.raw");
    return true;
}

bool upload_rom_a_slots_to_device() {
    if (!render_all_slots()) {
        return false;
    }
    std::size_t uploaded = 0;
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (!is_rom_a_slot_index(i)) {
            continue;
        }
        if (!upload_slot_to_device(i)) {
            set_status(std::string("ROM A upload stopped at slot ") + kSlots[i].name);
            return false;
        }
        ++uploaded;
    }
    set_status("Uploaded ROM A slots: " + std::to_string(uploaded));
    return true;
}

bool upload_rom_b_slots_to_device() {
    if (!render_all_slots()) {
        return false;
    }
    std::size_t uploaded = 0;
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (!is_rom_b_slot_index(i)) {
            continue;
        }
        if (!upload_slot_to_device(i)) {
            set_status(std::string("ROM B upload stopped at slot ") + kSlots[i].name);
            return false;
        }
        ++uploaded;
    }
    set_status("Uploaded ROM B slots: " + std::to_string(uploaded));
    return true;
}

bool upload_all_rom_slots_to_device() {
    if (!render_all_slots()) {
        return false;
    }
    std::size_t uploaded = 0;
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (!is_rom_slot_index(i)) {
            continue;
        }
        if (!upload_slot_to_device(i)) {
            set_status(std::string("ROM upload stopped at slot ") + kSlots[i].name);
            return false;
        }
        ++uploaded;
    }
    set_status("Uploaded all ROM slots: " + std::to_string(uploaded));
    return true;
}

bool upload_midi_sample_data() {
    return send_rz1_sysex_command(0x10u, "10 MIDI SEND sample RAM");
}

bool ensure_support_raw_file(const std::string& path, std::size_t size, std::int8_t fill_value = 0) {
    std::error_code ec;
    const std::filesystem::path out_path(path);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    if (std::filesystem::exists(out_path, ec)) {
        std::ifstream in(out_path, std::ios::binary | std::ios::ate);
        if (in.is_open() && static_cast<std::size_t>(in.tellg()) == size) {
            return true;
        }
    }

    std::vector<std::int8_t> data(size, fill_value);
    write_raw_signed8(path, data);

    std::ifstream verify(out_path, std::ios::binary | std::ios::ate);
    return verify.is_open() && static_cast<std::size_t>(verify.tellg()) == size;
}

bool ensure_rom_builder_support_files() {
    // ROM A map includes non-editor support assets that may not exist in samples/.
    // Create silence placeholders so strict ROM builds can complete.
    if (!ensure_support_raw_file("samples/metronome_click.raw", 507u, 0)) {
        set_status("Failed to create samples/metronome_click.raw");
        return false;
    }
    if (!ensure_support_raw_file("samples/empty.raw", 1153u, 0)) {
        set_status("Failed to create samples/empty.raw");
        return false;
    }
    return true;
}

bool build_full_rom_images(std::string* rom_a_out = nullptr, std::string* rom_b_out = nullptr) {
    if (!render_all_slots()) {
        return false;
    }

    if (!ensure_rom_builder_support_files()) {
        return false;
    }

    std::filesystem::create_directories("roms");
    const std::string rom_a_path = "roms/gui_rz1_rom_a.bin";
    const std::string rom_b_path = "roms/gui_rz1_rom_b.bin";

    const auto [rc_a, out_a] = run_command_capture(
        "./build/drumrom_build_bin --map configs/rz1_rom_a_map.json --sample-dir samples --out " +
        shell_escape_single(rom_a_path) + " --strict");
    if (rc_a != 0) {
        set_status(std::string("ROM A build failed: ") + summarize_command_output(out_a));
        return false;
    }

    const auto [rc_b, out_b] = run_command_capture(
        "./build/drumrom_build_bin --map configs/rz1_rom_b_map.json --sample-dir samples --out " +
        shell_escape_single(rom_b_path) + " --strict");
    if (rc_b != 0) {
        set_status(std::string("ROM B build failed: ") + summarize_command_output(out_b));
        return false;
    }

    if (rom_a_out != nullptr) {
        *rom_a_out = rom_a_path;
    }
    if (rom_b_out != nullptr) {
        *rom_b_out = rom_b_path;
    }

    set_status("Built ROM images: roms/gui_rz1_rom_a.bin and roms/gui_rz1_rom_b.bin");
    return true;
}

bool build_rom_image() {
    return build_full_rom_images();
}

bool patch_slot_into_rom_image(const std::string& rom_path, std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }
    const SlotDef& s = kSlots[slot_idx];
    const std::string raw_path = std::string("samples/") + s.name + ".raw";
    const auto raw = read_raw_signed8(raw_path);
    if (raw.size() != s.size) {
        return false;
    }

    std::ifstream in(rom_path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t slot_start = get_slot_start_address(slot_idx);
    if (rom.size() < (slot_start + s.size)) {
        return false;
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
        rom[slot_start + i] = static_cast<std::uint8_t>(raw[i]);
    }

    std::ofstream out(rom_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    return static_cast<bool>(out);
}

bool upload_slot_to_device(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }

    const SlotDef& s = kSlots[slot_idx];
    if (!is_rom_slot_index(slot_idx)) {
        set_status(std::string("Slot ") + s.name + " is not a ROM slot. Use Upload MIDI for sample RAM transfer.");
        return false;
    }

    const std::string raw_path = std::string("samples/") + s.name + ".raw";

    if (!std::filesystem::exists(raw_path)) {
        set_status(std::string("Upload failed: ") + raw_path + " not found. Render slot first.");
        return false;
    }

    std::string assigned_serial;
    std::string routing_error;
    if (!resolve_onerom_serial_for_slot(slot_idx, &assigned_serial, &routing_error)) {
        set_status(std::string("Upload routing failed for slot ") + s.name + ": " + routing_error);
        return false;
    }

    const std::size_t start_addr = get_slot_upload_start_address(slot_idx);
    const std::string onerom_cmd = build_onerom_base_command(assigned_serial);
    const std::string cmd =
        onerom_cmd + " poke --yes --address " + std::to_string(start_addr) +
        " --input " + shell_escape_single(raw_path);

    const auto [rc, output] = run_command_capture(cmd);
    if (rc == 0) {
        set_status(std::string("Uploaded slot ") + s.name + " to live ROM image");
        return true;
    }

    set_status(std::string("Upload failed for slot ") + s.name + ": " + summarize_command_output(output));
    return false;
}

bool program_slot_to_device_full_rom(std::size_t slot_idx) {
    (void)slot_idx;

    refresh_onerom_usb_devices();
    if (g_available_onerom_serials.empty()) {
        set_status("Program full ROM failed: no OneROM USB device detected (scan in Settings)");
        return false;
    }

    std::size_t rom_a_slot = kSlots.size();
    std::size_t rom_b_slot = kSlots.size();
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        if (rom_a_slot == kSlots.size() && is_rom_a_slot_index(i)) {
            rom_a_slot = i;
        }
        if (rom_b_slot == kSlots.size() && is_rom_b_slot_index(i)) {
            rom_b_slot = i;
        }
    }
    if (rom_a_slot >= kSlots.size() || rom_b_slot >= kSlots.size()) {
        set_status("Program full ROM failed: ROM slot mapping is incomplete");
        return false;
    }

    std::string serial_a;
    std::string serial_b;
    std::string route_err;
    if (!resolve_onerom_serial_for_slot(rom_a_slot, &serial_a, &route_err)) {
        set_status(std::string("Program ROM A failed: ") + route_err);
        return false;
    }
    if (!resolve_onerom_serial_for_slot(rom_b_slot, &serial_b, &route_err)) {
        set_status(std::string("Program ROM B failed: ") + route_err);
        return false;
    }

    std::string rom_a_path;
    std::string rom_b_path;
    if (!build_full_rom_images(&rom_a_path, &rom_b_path)) {
        return false;
    }

    const std::string rom_a_arg = "file=" + rom_a_path + ",type=27c256";
    const std::string rom_b_arg = "file=" + rom_b_path + ",type=27c256";

    const std::string program_a_cmd =
        build_onerom_base_command(serial_a) + " program --yes --plugin usb --slot " + shell_escape_single(rom_a_arg);
    const auto [program_a_rc, program_a_out] = run_command_capture(program_a_cmd);
    if (program_a_rc != 0) {
        set_status(std::string("Program ROM A failed: ") + summarize_command_output(program_a_out));
        return false;
    }

    const std::string program_b_cmd =
        build_onerom_base_command(serial_b) + " program --yes --plugin usb --slot " + shell_escape_single(rom_b_arg);
    const auto [program_b_rc, program_b_out] = run_command_capture(program_b_cmd);
    if (program_b_rc != 0) {
        set_status(std::string("Program ROM B failed: ") + summarize_command_output(program_b_out));
        return false;
    }

    set_status("Programmed ROM A then ROM B to both OneROM devices");
    return true;
}

void maybe_auto_upload_current_slot(bool committed) {
    if (!committed || !g_auto_upload_enabled || !g_params_dirty || g_auto_upload_busy) {
        return;
    }
    if (g_selected_slot < g_slot_cfg.size() &&
        g_slot_cfg[g_selected_slot].source == SourceKind::Synth &&
        g_slot_cfg[g_selected_slot].drum == DrumKind::ElementsExact) {
        return;
    }
    g_auto_upload_busy = true;
    std::mt19937 rng(g_slot_cfg[g_selected_slot].seed);
    const auto data = render_slot(g_selected_slot, rng);
    if (data.empty() || !write_slot_file(g_selected_slot, data)) {
        set_status("Auto-upload render failed");
        g_auto_upload_busy = false;
        return;
    }
    const bool uploaded = upload_slot_to_device(g_selected_slot);
    g_auto_upload_busy = false;
    if (uploaded) {
        g_params_dirty = false;
    }
}

bool ensure_preview_audio_device() {
    if (g_preview_audio_device != 0) {
        return true;
    }

    SDL_AudioSpec desired{};
    desired.freq = kSampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 1024;

    SDL_AudioSpec obtained{};
    g_preview_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_preview_audio_device == 0) {
        set_status(std::string("Audio preview unavailable: ") + SDL_GetError());
        return false;
    }

    if (obtained.channels != 1 || obtained.format != AUDIO_S16SYS || obtained.freq != kSampleRate) {
        SDL_CloseAudioDevice(g_preview_audio_device);
        g_preview_audio_device = 0;
        set_status("Audio preview format mismatch (need mono 16-bit @ 20kHz)");
        return false;
    }

    SDL_PauseAudioDevice(g_preview_audio_device, 0);
    return true;
}

bool play_slot_preview(std::size_t slot_idx) {
    if (slot_idx >= kSlots.size()) {
        return false;
    }
    if (!ensure_preview_audio_device()) {
        return false;
    }

    std::mt19937 rng(g_slot_cfg[slot_idx].seed);
    const auto rendered = render_slot(slot_idx, rng);
    if (rendered.empty()) {
        set_status(std::string("Preview render failed for slot ") + kSlots[slot_idx].name + ": " +
                   (g_last_sample_error.empty() ? "unknown error" : g_last_sample_error));
        return false;
    }

    std::vector<std::int16_t> pcm;
    pcm.reserve(rendered.size());
    for (const std::int8_t s : rendered) {
        pcm.push_back(static_cast<std::int16_t>(s) << 8);
    }

    SDL_ClearQueuedAudio(g_preview_audio_device);
    const int queue_rc = SDL_QueueAudio(
        g_preview_audio_device,
        pcm.data(),
        static_cast<Uint32>(pcm.size() * sizeof(std::int16_t))
    );
    if (queue_rc != 0) {
        set_status(std::string("Audio preview queue failed: ") + SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(g_preview_audio_device, 0);
    return true;
}

bool play_original_sample_preview(const SampleEdit& sample) {
    if (!ensure_preview_audio_device()) {
        return false;
    }

    auto loaded = load_sample_any_format(sample);
    if (!loaded.has_value()) {
        set_status(std::string("Original sample audition failed: ") +
                   (g_last_sample_error.empty() ? "unknown error" : g_last_sample_error));
        return false;
    }

    auto mono = resample_rate_linear(loaded->first, loaded->second, kSampleRate);
    if (mono.empty()) {
        set_status("Original sample audition failed: no audio data");
        return false;
    }

    std::vector<std::int16_t> pcm;
    pcm.reserve(mono.size());
    for (float s : mono) {
        const float clamped = std::clamp(s, -1.0f, 1.0f);
        pcm.push_back(static_cast<std::int16_t>(clamped * 32767.0f));
    }

    SDL_ClearQueuedAudio(g_preview_audio_device);
    const int queue_rc = SDL_QueueAudio(
        g_preview_audio_device,
        pcm.data(),
        static_cast<Uint32>(pcm.size() * sizeof(std::int16_t))
    );
    if (queue_rc != 0) {
        set_status(std::string("Audio audition queue failed: ") + SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(g_preview_audio_device, 0);
    return true;
}

void maybe_auto_play_current_slot(bool committed) {
    if (!committed || !g_auto_play_enabled || !g_params_dirty) {
        return;
    }
    (void)play_slot_preview(g_selected_slot);
}

std::vector<std::int8_t> render_slot(std::size_t slot_idx, std::mt19937& rng) {
    if (slot_idx >= g_slot_cfg.size()) return {};

    const SlotConfig& cfg = g_slot_cfg[slot_idx];
    const std::size_t num_samples = get_slot_capacity(slot_idx);
    if (num_samples == 0) {
        return {};
    }
    std::vector<std::uint8_t> u8_signal;

    if (cfg.source == SourceKind::Synth) {
        switch (cfg.drum) {
            case DrumKind::Kick:
                u8_signal = drumrom::synth::synthesize_kick_custom(cfg.params, rng, num_samples);
                break;
            case DrumKind::Snare:
                u8_signal = drumrom::synth::synthesize_snare_custom(cfg.params, rng, num_samples);
                break;
            case DrumKind::Hihat:
                u8_signal = drumrom::synth::synthesize_hihat_custom(cfg.params, rng, num_samples);
                break;
            case DrumKind::Tom:
                u8_signal = drumrom::synth::synthesize_tom_custom(cfg.params, rng, num_samples);
                break;
            case DrumKind::Clap:
                u8_signal = drumrom::synth::synthesize_clap_custom(cfg.params, rng, num_samples);
                break;
            case DrumKind::Elements:
                u8_signal = drumrom::synth::synthesize_elements(kSampleRate, num_samples, cfg.elements_params, rng);
                break;
            case DrumKind::ElementsExact:
                u8_signal = drumrom::synth::synthesize_elements_exact(kSampleRate, num_samples, cfg.elements_exact_params, rng);
                break;
        }
    } else {
        // Sample path
        if (cfg.sample.amp_envelope_mode == AmpEnvelopeMode::PreFit) {
            // Old behavior: apply ADSR before fitting (compressed envelope effect)
            auto f = process_sample_preview(cfg.sample);
            if (f.empty()) {
                return {};
            }
            f = apply_loop_window(f, num_samples, cfg.sample.loop_start_pct, cfg.sample.loop_end_pct, cfg.sample.loop_increment_pct);
            auto encoded = encode_signed8(f);
            u8_signal = drumrom::synth::fit_slot(encoded, num_samples, 0, kSampleRate);
        } else if (cfg.sample.amp_envelope_mode == AmpEnvelopeMode::Output) {
            // New behavior: apply ADSR after fitting (correct timing)
            auto f = process_sample_for_render(cfg.sample);
            if (f.empty()) {
                return {};
            }
            f = apply_loop_window(f, num_samples, cfg.sample.loop_start_pct, cfg.sample.loop_end_pct, cfg.sample.loop_increment_pct);
            auto encoded = encode_signed8(f);
            u8_signal = drumrom::synth::fit_slot(encoded, num_samples, 0, kSampleRate);
            
            // Convert back to float for ADSR application (now at final slot size)
            std::vector<float> f_final(u8_signal.size());
            for (std::size_t i = 0; i < u8_signal.size(); ++i) {
                const std::int8_t signed_sample = static_cast<std::int8_t>(u8_signal[i]);
                f_final[i] = static_cast<float>(signed_sample) / 127.0f;
            }
            
            // Apply ADSR based on final slot size
            apply_adsr(f_final, cfg.sample.amp_attack_s, cfg.sample.amp_decay_s, cfg.sample.amp_sustain, cfg.sample.amp_release_s);
            
            // Convert back to int8
            for (std::size_t i = 0; i < f_final.size(); ++i) {
                const float fv = std::clamp(f_final[i], -1.0f, 1.0f);
                u8_signal[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::round(fv * 127.0f)));
            }
        } else {
            // Off: no envelope
            auto f = process_sample_for_render(cfg.sample);
            if (f.empty()) {
                return {};
            }
            f = apply_loop_window(f, num_samples, cfg.sample.loop_start_pct, cfg.sample.loop_end_pct, cfg.sample.loop_increment_pct);
            auto encoded = encode_signed8(f);
            u8_signal = drumrom::synth::fit_slot(encoded, num_samples, 0, kSampleRate);
        }
    }

    // Apply reverb effect if enabled (before output shaping)
    if (cfg.params.reverb.enabled > 0.5f) {
        // Convert to float
        std::vector<float> float_signal(u8_signal.size());
        for (std::size_t i = 0; i < u8_signal.size(); ++i) {
            const std::int8_t signed_sample = static_cast<std::int8_t>(u8_signal[i]);
            float_signal[i] = static_cast<float>(signed_sample) / 127.0f;
        }
        
        // Apply reverb
        drumrom::synth::ReverbParams reverb_params;
        reverb_params.decay_time_ms = cfg.params.reverb.decay_time_ms;
        reverb_params.damping = cfg.params.reverb.damping;
        reverb_params.width = cfg.params.reverb.width;
        reverb_params.early_level = cfg.params.reverb.early_level;
        reverb_params.early_spread = cfg.params.reverb.early_spread;
        reverb_params.diffusion = cfg.params.reverb.diffusion;
        reverb_params.tone = cfg.params.reverb.tone;
        reverb_params.late_mix = cfg.params.reverb.late_mix;
        reverb_params.size = cfg.params.reverb.size;
        reverb_params.decay_shape = cfg.params.reverb.decay_shape;
        reverb_params.wet_level = cfg.params.reverb.wet_level;
        reverb_params.dry_level = cfg.params.reverb.dry_level;
        reverb_params.pre_delay_ms = cfg.params.reverb.pre_delay_ms;
        
        float_signal = drumrom::synth::apply_freeverb(float_signal, reverb_params, kSampleRate);
        
        // Convert back to u8
        for (std::size_t i = 0; i < u8_signal.size(); ++i) {
            const float fv = std::clamp(float_signal[i], -1.0f, 1.0f);
            u8_signal[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::round(fv * 127.0f)));
        }
    }

    const bool is_elements_exact =
        cfg.source == SourceKind::Synth &&
        cfg.drum == DrumKind::ElementsExact;

    // Apply output gain/final shaping.
    for (auto& s : u8_signal) {
        const std::int8_t signed_sample = static_cast<std::int8_t>(s);
        const float in = static_cast<float>(signed_sample) / 127.0f;
        float fv = 0.0f;
        if (is_elements_exact) {
            const float linear = std::pow(10.0f, cfg.output_gain_db / 20.0f);
            fv = std::clamp(in * linear, -1.0f, 1.0f);
        } else {
            fv = apply_output_shaper_sample(in, cfg.output_gain_db, cfg.limiter_ceiling, cfg.output_shaper_mode, cfg.output_saturation);
        }
        const int q = static_cast<int>(std::round(std::clamp(fv, -1.0f, 1.0f) * 127.0f));
        s = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
    }

    if (is_elements_exact && !u8_signal.empty()) {
        // Guarantee clean hardware playback boundaries for ElementsExact:
        // short fade to silence, then hard-zero tail samples.
        const std::size_t fade_len = std::min<std::size_t>(std::max<std::size_t>(8, u8_signal.size() / 64), u8_signal.size());
        const std::size_t fade_start = u8_signal.size() - fade_len;
        for (std::size_t i = 0; i < fade_len; ++i) {
            const float t = (fade_len > 1)
                ? static_cast<float>(i) / static_cast<float>(fade_len - 1)
                : 1.0f;
            const float k = 1.0f - t;
            const std::int8_t s = static_cast<std::int8_t>(u8_signal[fade_start + i]);
            const int v = static_cast<int>(std::round(static_cast<float>(s) * k));
            u8_signal[fade_start + i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(v, -128, 127)));
        }

        const std::size_t hard_zero_tail = std::min<std::size_t>(8, u8_signal.size());
        for (std::size_t i = 0; i < hard_zero_tail; ++i) {
            u8_signal[u8_signal.size() - 1u - i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(0));
        }
    }

    std::vector<std::int8_t> result;
    result.reserve(u8_signal.size());
    for (std::uint8_t s : u8_signal) {
        result.push_back(static_cast<std::int8_t>(s));
    }
    return result;
}

// ============================================================================
// Randomizer
// ============================================================================

void randomize_fm(drumrom::synth::FmToneParams& fm, std::mt19937& rng) {
    auto rf = [&rng](float mn, float mx) {
        std::uniform_real_distribution<float> d(mn, mx);
        return d(rng);
    };
    
    fm.mod_freq_hz = rf(30.0f, 400.0f);
    fm.mod_freq_end_hz = rf(30.0f, 400.0f);
    fm.mod_pitch_decay_rate = rf(0.0f, 30.0f);
    fm.mod_index = rf(0.0f, 5.0f);
    fm.mod_index_end = rf(0.0f, 5.0f);
    fm.mod_index_decay_rate = rf(0.0f, 40.0f);
    fm.amp_osc_hz = rf(0.0f, 60.0f);
    fm.amp_osc_end_hz = rf(0.0f, 60.0f);
    fm.amp_osc_pitch_decay_rate = rf(0.0f, 40.0f);
    fm.amp_osc_depth = rf(0.0f, 0.5f);
    fm.amp_osc_depth_end = rf(0.0f, 0.5f);
    fm.amp_osc_depth_decay_rate = rf(0.0f, 40.0f);
}

void randomize_slot(SlotConfig& cfg) {
    static std::mt19937 rng{std::random_device{}()};
    auto rf = [](float mn, float mx) {
        static thread_local std::mt19937 local_rng{std::random_device{}()};
        std::uniform_real_distribution<float> d(mn, mx);
        return d(local_rng);
    };
    auto rs = [](std::mt19937& eng) {
        std::uniform_int_distribution<int> d(0, 2);
        int choice = d(eng);
        if (choice == 0) return EnvelopeShape::Exponential;
        if (choice == 1) return EnvelopeShape::Linear;
        return EnvelopeShape::Logarithmic;
    };

    cfg.seed = static_cast<std::uint32_t>(rng());
    cfg.params.sample_rate = kSampleRate;
    cfg.output_gain_db = rf(-6.0f, 10.0f);
    cfg.limiter_ceiling = rf(0.55f, 1.0f);
    {
        std::uniform_int_distribution<int> mode_dist(0, 2);
        cfg.output_shaper_mode = mode_dist(rng);
    }
    cfg.output_saturation = rf(0.25f, 0.9f);

    if (cfg.source == SourceKind::Synth && cfg.drum == DrumKind::ElementsExact) {
        // ElementsExact intentionally bypasses limiter/saturation in render path.
        // Keep UI/state neutral so randomize cannot reintroduce shaping controls.
        cfg.limiter_ceiling = 1.0f;
        cfg.output_shaper_mode = 0;
        cfg.output_saturation = 0.0f;
    }

    if (is_sample_based_source(cfg.source)) {
        cfg.sample.start_pct = static_cast<int>(std::round(rf(0.0f, 70.0f)));
        cfg.sample.end_pct = std::clamp(cfg.sample.start_pct + static_cast<int>(std::round(rf(12.0f, 100.0f - cfg.sample.start_pct))), cfg.sample.start_pct + 1, 100);
        cfg.sample.loop_start_pct = static_cast<int>(std::round(rf(0.0f, 80.0f)));
        cfg.sample.loop_end_pct = std::clamp(cfg.sample.loop_start_pct + static_cast<int>(std::round(rf(8.0f, 100.0f - cfg.sample.loop_start_pct))), cfg.sample.loop_start_pct + 1, 100);
        cfg.sample.loop_increment_pct = rf(-20.0f, 20.0f);
        cfg.sample.tune_semitones = rf(-12.0f, 12.0f);
        cfg.sample.filter_cutoff_hz = rf(80.0f, 12000.0f);
        cfg.sample.filter_cutoff_end_hz = rf(80.0f, 12000.0f);
        cfg.sample.filter_env_decay_s = rf(0.01f, 1.6f);
        cfg.sample.filter_resonance = (rf(0.0f, 1.0f) < 0.8f) ? rf(0.0f, 0.4f) : rf(0.4f, 1.5f);
        cfg.sample.amp_attack_s = rf(0.0f, 0.2f);
        cfg.sample.amp_decay_s = rf(0.01f, 0.6f);
        cfg.sample.amp_sustain = rf(0.0f, 1.0f);
        cfg.sample.amp_release_s = rf(0.0f, 0.6f);
        {
            std::uniform_int_distribution<int> env_dist(0, 2);
            cfg.sample.amp_envelope_mode = static_cast<AmpEnvelopeMode>(env_dist(rng));
        }
        g_params_dirty = true;
        return;
    }

    switch (cfg.drum) {
        case DrumKind::Kick:
            cfg.params.kick.duration_s = rf(0.10f, 0.40f);
            cfg.params.kick.pitch_start_hz = rf(80.0f, 260.0f);
            cfg.params.kick.pitch_end_hz = rf(25.0f, 90.0f);
            cfg.params.kick.pitch_decay_rate = rf(2.0f, 30.0f);
            cfg.params.kick.pitch_env_shape = rs(rng);
            cfg.params.kick.env_decay_rate = rf(4.0f, 30.0f);
            cfg.params.kick.attack_rate = rf(0.0f, 10.0f);
            cfg.params.kick.amp_decay_shape = rs(rng);
            cfg.params.kick.amp_attack_shape = rs(rng);
            cfg.params.kick.tone_decay_rate = rf(0.0f, 20.0f);
            cfg.params.kick.tone_env_shape = rs(rng);
            randomize_fm(cfg.params.kick.fm, rng);
            break;
        case DrumKind::Snare:
            cfg.params.snare.duration_s = rf(0.08f, 0.30f);
            cfg.params.snare.tone_freq_hz = rf(120.0f, 550.0f);
            cfg.params.snare.pitch_decay_rate = rf(0.0f, 20.0f);
            cfg.params.snare.pitch_env_shape = rs(rng);
            cfg.params.snare.tone_decay_rate = rf(4.0f, 40.0f);
            cfg.params.snare.tone_env_shape = rs(rng);
            cfg.params.snare.noise_decay_rate = rf(6.0f, 55.0f);
            cfg.params.snare.tone_mix = rf(0.05f, 0.9f);
            cfg.params.snare.noise_mix = rf(0.2f, 1.0f);
            cfg.params.snare.attack_rate = rf(0.0f, 10.0f);
            cfg.params.snare.amp_attack_shape = rs(rng);
            cfg.params.snare.amp_decay_rate = rf(0.0f, 35.0f);
            cfg.params.snare.amp_decay_shape = rs(rng);
            randomize_fm(cfg.params.snare.fm, rng);
            break;
        case DrumKind::Hihat:
            cfg.params.hihat.duration_s = rf(0.03f, 0.20f);
            cfg.params.hihat.tone_freq_hz = rf(180.0f, 1200.0f);
            cfg.params.hihat.hp_cutoff_hz = rf(1200.0f, 9000.0f);
            cfg.params.hihat.hp_resonance = (rf(0.0f, 1.0f) < 0.8f) ? rf(0.35f, 0.7f) : rf(0.7f, 1.6f);
            cfg.params.hihat.tone_decay_rate = rf(0.0f, 30.0f);
            cfg.params.hihat.tone_env_shape = rs(rng);
            cfg.params.hihat.decay_rate = rf(5.0f, 85.0f);
            cfg.params.hihat.amp_decay_shape = rs(rng);
            cfg.params.hihat.attack_rate = rf(0.0f, 10.0f);
            cfg.params.hihat.amp_attack_shape = rs(rng);
            randomize_fm(cfg.params.hihat.fm, rng);
            break;
        case DrumKind::Tom:
            cfg.params.tom.duration_s = rf(0.08f, 0.40f);
            cfg.params.tom.pitch_start_hz = rf(70.0f, 240.0f);
            cfg.params.tom.pitch_end_hz = rf(35.0f, 160.0f);
            cfg.params.tom.pitch_decay_rate = rf(0.0f, 30.0f);
            cfg.params.tom.pitch_env_shape = rs(rng);
            cfg.params.tom.env_decay_rate = rf(4.0f, 35.0f);
            cfg.params.tom.amp_decay_shape = rs(rng);
            cfg.params.tom.attack_rate = rf(0.0f, 10.0f);
            cfg.params.tom.amp_attack_shape = rs(rng);
            cfg.params.tom.tone_decay_rate = rf(0.0f, 25.0f);
            cfg.params.tom.tone_env_shape = rs(rng);
            randomize_fm(cfg.params.tom.fm, rng);
            break;
        case DrumKind::Clap:
            cfg.params.clap.duration_s = rf(0.08f, 0.35f);
            cfg.params.clap.tone_freq_hz = rf(400.0f, 2600.0f);
            cfg.params.clap.click_rate = rf(0.5f, 3.0f);
            cfg.params.clap.tone_decay_rate = rf(5.0f, 40.0f);
            cfg.params.clap.tone_env_shape = rs(rng);
            cfg.params.clap.env_decay_rate = rf(5.0f, 40.0f);
            cfg.params.clap.amp_decay_shape = rs(rng);
            cfg.params.clap.attack_rate = rf(0.0f, 10.0f);
            cfg.params.clap.amp_attack_shape = rs(rng);
            randomize_fm(cfg.params.clap.fm, rng);
            break;
        case DrumKind::Elements:
            {
                std::uniform_int_distribution<int> model_dist(0, 5);
                cfg.elements_params.model = static_cast<drumrom::synth::ElementsModel>(model_dist(rng));
            }
            cfg.elements_params.frequency_hz   = rf(30.0f, 800.0f);
            cfg.elements_params.brightness      = rf(0.0f, 1.0f);
            cfg.elements_params.damping         = rf(0.1f, 0.9f);
            cfg.elements_params.position        = rf(0.0f, 1.0f);
            cfg.elements_params.exciter_level   = rf(0.5f, 1.0f);
            cfg.elements_params.exciter_noise   = rf(0.0f, 0.5f);
            cfg.elements_params.exciter_dur_s   = rf(0.002f, 0.025f);
            cfg.elements_params.env_decay_rate  = rf(0.0f, 30.0f);
            cfg.elements_params.env_attack_rate = rf(0.0f, 5.0f);
            break;
            case DrumKind::ElementsExact:
                {
                    std::uniform_int_distribution<int> resonator_model_dist(0, 2);
                    cfg.elements_exact_params.resonator_model =
                        static_cast<drumrom::synth::ElementsExactResonatorModel>(resonator_model_dist(rng));
                }
                cfg.elements_exact_params.note = rf(12.0f, 72.0f);
                cfg.elements_exact_params.modulation = rf(-12.0f, 12.0f);
                cfg.elements_exact_params.strength = rf(0.2f, 1.0f);
                cfg.elements_exact_params.blow_cv = rf(0.0f, 1.0f);
                cfg.elements_exact_params.strike_cv = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_envelope_shape = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_bow_level = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_bow_timbre = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_blow_level = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_blow_meta = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_blow_timbre = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_strike_level = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_strike_meta = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_strike_timbre = rf(0.0f, 1.0f);
                cfg.elements_exact_params.exciter_signature = rf(0.0f, 1.0f);
                cfg.elements_exact_params.resonator_geometry = rf(0.0f, 1.0f);
                cfg.elements_exact_params.resonator_brightness = rf(0.0f, 1.0f);
                cfg.elements_exact_params.resonator_damping = rf(0.0f, 1.0f);
                cfg.elements_exact_params.resonator_position = rf(0.0f, 1.0f);
                cfg.elements_exact_params.reverb_diffusion = rf(0.0f, 1.0f);
                cfg.elements_exact_params.reverb_lp = rf(0.0f, 1.0f);
                cfg.elements_exact_params.space = rf(0.0f, 1.0f);
                cfg.elements_exact_params.resonator_modulation_frequency = rf(0.0f, 0.01f);
                cfg.elements_exact_params.resonator_modulation_offset = rf(0.0f, 1.0f);
                cfg.elements_exact_params.modulation_frequency = rf(0.0f, 1.0f);
                cfg.elements_exact_params.easter_egg = 0.0f;

                {
                    std::uniform_int_distribution<int> cv_target_dist(
                        static_cast<int>(drumrom::synth::ElementsExactCvTarget::None),
                        static_cast<int>(drumrom::synth::ElementsExactCvTarget::Space));
                    cfg.elements_exact_params.cv_env1.target =
                        static_cast<drumrom::synth::ElementsExactCvTarget>(cv_target_dist(rng));
                    cfg.elements_exact_params.cv_env2.target =
                        static_cast<drumrom::synth::ElementsExactCvTarget>(cv_target_dist(rng));
                }

                cfg.elements_exact_params.cv_env1.amount = rf(-1.0f, 1.0f);
                cfg.elements_exact_params.cv_env1.attack_s = rf(0.0005f, 0.25f);
                cfg.elements_exact_params.cv_env1.decay_s = rf(0.002f, 0.8f);
                cfg.elements_exact_params.cv_env1.sustain = rf(0.0f, 1.0f);
                cfg.elements_exact_params.cv_env1.release_s = rf(0.002f, 1.2f);
                cfg.elements_exact_params.cv_env2.amount = rf(-1.0f, 1.0f);
                cfg.elements_exact_params.cv_env2.attack_s = rf(0.0005f, 0.25f);
                cfg.elements_exact_params.cv_env2.decay_s = rf(0.002f, 0.8f);
                cfg.elements_exact_params.cv_env2.sustain = rf(0.0f, 1.0f);
                cfg.elements_exact_params.cv_env2.release_s = rf(0.002f, 1.2f);
                break;
    }

    g_params_dirty = true;
}

void randomize_reverb(SlotConfig& cfg) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    auto rf = [](float mn, float mx) {
        std::uniform_real_distribution<float> d(mn, mx);
        return d(rng);
    };

    cfg.params.reverb.enabled = 1.0f;
    cfg.params.reverb.decay_time_ms = rf(40.0f, 650.0f);
    cfg.params.reverb.damping = rf(0.20f, 0.92f);
    cfg.params.reverb.width = rf(0.40f, 1.0f);
    cfg.params.reverb.early_level = rf(0.08f, 0.70f);
    cfg.params.reverb.early_spread = rf(0.70f, 1.45f);
    cfg.params.reverb.diffusion = rf(0.25f, 0.92f);
    cfg.params.reverb.tone = rf(0.20f, 0.95f);
    cfg.params.reverb.late_mix = rf(0.25f, 0.85f);
    cfg.params.reverb.size = rf(0.70f, 1.35f);
    cfg.params.reverb.decay_shape = rf(0.20f, 0.90f);
    cfg.params.reverb.wet_level = rf(0.08f, 0.50f);
    cfg.params.reverb.dry_level = rf(0.50f, 1.0f);
    cfg.params.reverb.pre_delay_ms = rf(0.0f, 14.0f);

    g_params_dirty = true;
}

// ============================================================================
// UI Rendering
// ============================================================================

const char* drum_kind_name(DrumKind k) {
    switch (k) {
        case DrumKind::Kick: return "Kick";
        case DrumKind::Snare: return "Snare";
        case DrumKind::Hihat: return "Hi-Hat";
        case DrumKind::Tom: return "Tom";
        case DrumKind::Clap: return "Clap";
        case DrumKind::Elements: return "Elements";
        case DrumKind::ElementsExact: return "Elements Exact";
    }
    return "?";
}

std::size_t drum_kind_to_index(DrumKind kind) {
    switch (kind) {
        case DrumKind::Kick: return 0;
        case DrumKind::Snare: return 1;
        case DrumKind::Hihat: return 2;
        case DrumKind::Tom: return 3;
        case DrumKind::Clap: return 4;
        case DrumKind::Elements: return 5;
        case DrumKind::ElementsExact: return 6;
    }
    return 0;
}

DrumKind drum_kind_from_index(int idx) {
    switch (idx) {
        case 0: return DrumKind::Kick;
        case 1: return DrumKind::Snare;
        case 2: return DrumKind::Hihat;
        case 3: return DrumKind::Tom;
        case 4: return DrumKind::Clap;
        case 5: return DrumKind::Elements;
        case 6: return DrumKind::ElementsExact;
        default: return DrumKind::Kick;
    }
}

void ensure_drum_type_textures_loaded() {
    drumrom::main_ui_icons::State state{};
    state.sdl_renderer = g_sdl_renderer;
    state.drum_type_textures_attempted = &g_drum_type_textures_attempted;
    state.drum_type_textures = &g_drum_type_textures;
    drumrom::main_ui_icons::ensure_drum_type_textures_loaded(&state);
}

void ensure_drum_icons_texture_loaded() {
    drumrom::main_ui_icons::State state{};
    state.sdl_renderer = g_sdl_renderer;
    state.drum_icons_texture_attempted = &g_drum_icons_texture_attempted;
    state.drum_icons_texture = &g_drum_icons_texture;
    state.drum_icons_uv_ready = &g_drum_icons_uv_ready;
    state.drum_icons_uvs = &g_drum_icons_uvs;
    drumrom::main_ui_icons::ensure_drum_icons_texture_loaded(&state);
}

int sprite_tile_for_drum_kind(DrumKind kind) {
    switch (kind) {
        case DrumKind::Kick: return 1;
        case DrumKind::Snare: return 2;
        case DrumKind::Tom: return 6;
        case DrumKind::Clap: return 8;
        case DrumKind::Hihat: return 13;
        case DrumKind::Elements: return 6;
        case DrumKind::ElementsExact: return 6;
    }
    return 1;
}

void sprite_uv_for_tile(int tile, ImVec2* uv0, ImVec2* uv1) {
    drumrom::main_ui_icons::State state{};
    state.drum_icons_uv_ready = &g_drum_icons_uv_ready;
    state.drum_icons_uvs = &g_drum_icons_uvs;
    drumrom::main_ui_icons::sprite_uv_for_tile(state, tile, uv0, uv1);
}

bool source_kind_icon_selector(const char* label, SourceKind* source) {
    ImGui::PushID(label);
    bool changed = false;
    const ImVec2 button_size(60.2f, 28.0f);

    auto source_button = [&](const char* text, SourceKind option) {
        const bool selected = (*source == option);
        const ImVec4 base = selected ? kColorGreen : ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        const ImVec4 hov = selected ? kColorGreenLite : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        const ImVec4 act = selected ? kColorGreenDark : ImVec4(0.36f, 0.36f, 0.36f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
        const bool pressed = ImGui::Button(text, button_size);
        ImGui::PopStyleColor(3);
        if (pressed) {
            *source = option;
            changed = true;
        }
    };

    source_button("Synth", SourceKind::Synth);
    ImGui::SameLine(0.0f, 3.0f);
    source_button("Sample", SourceKind::Sample);
    ImGui::SameLine(0.0f, 3.0f);
    source_button("Loop", SourceKind::Loop);

    ImGui::PopID();
    return changed;
}

bool drum_kind_icon_selector(const char* label, DrumKind* drum) {
    struct DrumOption {
        DrumKind kind;
        const char* name;
    };
    static const std::array<DrumOption, 7> options{{
        {DrumKind::Kick, "Kick"},
        {DrumKind::Snare, "Snare"},
        {DrumKind::Hihat, "Hat"},
        {DrumKind::Tom, "Tom"},
        {DrumKind::Clap, "Clap"},
        {DrumKind::Elements, "Elements"},
        {DrumKind::ElementsExact, "Elements*"},
    }};

    ImGui::PushID(label);
    bool changed = false;
    const ImVec2 button_size(50.4f, 28.0f);

    for (std::size_t i = 0; i < options.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool selected = (*drum == options[i].kind);
        const ImVec4 base = selected ? kColorGreen : ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        const ImVec4 hov = selected ? kColorGreenLite : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        const ImVec4 act = selected ? kColorGreenDark : ImVec4(0.36f, 0.36f, 0.36f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
        if (ImGui::Button(options[i].name, button_size)) {
            *drum = options[i].kind;
            changed = true;
        }
        ImGui::PopStyleColor(3);

        if (i + 1 < options.size()) {
            ImGui::SameLine(0.0f, 3.0f);
        }
        ImGui::PopID();
    }

    ImGui::PopID();
    return changed;
}

drumrom::main_ui_midi::State make_main_ui_midi_state() {
    drumrom::main_ui_midi::State state{};
    state.midi_in = &g_midi_in;
    state.available_midi_in_ports = &g_available_midi_in_ports;
    state.selected_midi_in_port = &g_selected_midi_in_port;
    state.midi_in_enabled = &g_midi_in_enabled;
    state.midi_in_status = &g_midi_in_status;

    state.midi_out = &g_midi_out;
    state.available_midi_out_ports = &g_available_midi_out_ports;
    state.selected_midi_out_port = &g_selected_midi_out_port;
    state.midi_out_enabled = &g_midi_out_enabled;
    state.midi_out_status = &g_midi_out_status;

    state.rz1_sysex_channel = &g_rz1_sysex_channel;
    state.rz1_sysex_status = &g_rz1_sysex_status;
    state.rz1_sysex_capture = &g_rz1_sysex_capture;
    state.rz1_sysex_message_count = &g_rz1_sysex_message_count;
    state.rz1_sysex_overflow = &g_rz1_sysex_overflow;
    state.rz1_sysex_dump_delay_ms = &g_rz1_sysex_dump_delay_ms;
    state.rz1_sysex_handshake_byte_delay_ms = &g_rz1_sysex_handshake_byte_delay_ms;
    state.last_rz1_sysex_request = &g_last_rz1_sysex_request;
    state.rz1_sysex_save_path = g_rz1_sysex_save_path;
    state.rz1_sysex_capture_max_bytes = kRz1SysexCaptureMaxBytes;

    state.midi_note_velocity = &g_midi_note_velocity;
    state.selected_slot = &g_selected_slot;
    state.last_midi_note_slot = &g_last_midi_note_slot;
    state.midi_debug_monitor_enabled = &g_midi_debug_monitor_enabled;
    state.midi_debug_monitor_lines = &g_midi_debug_monitor_lines;
    state.midi_debug_monitor_max_lines = kMidiDebugMonitorMaxLines;
    state.sysex_dump_state = &g_sysex_dump_state;
    return state;
}

void refresh_midi_in_ports() {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::refresh_midi_in_ports(&state);
}

void open_midi_in_port(int port_index) {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::open_midi_in_port(&state, port_index);
}

void close_midi_in_port() {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::close_midi_in_port(&state);
}

void refresh_midi_out_ports() {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::refresh_midi_out_ports(&state);
}

void open_midi_out_port(int port_index) {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::open_midi_out_port(&state, port_index);
}

void close_midi_out_port() {
    auto state = make_main_ui_midi_state();
    drumrom::main_ui_midi::close_midi_out_port(&state);
}

bool send_rz1_sysex_command(std::uint8_t command_zz, const char* command_name) {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status};
    return drumrom::main_ui_midi::send_rz1_sysex_command(&state, actions, command_zz, command_name);
}

bool save_rz1_sysex_capture() {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status};
    return drumrom::main_ui_midi::save_rz1_sysex_capture(&state, actions);
}

bool send_rz1_sysex_dump_to_rz1_sample() {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status};
    return drumrom::main_ui_midi::send_rz1_sysex_dump_to_rz1(&state, actions, true);
}

bool send_rz1_sysex_dump_to_rz1_rhythm() {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status};
    return drumrom::main_ui_midi::send_rz1_sysex_dump_to_rz1(&state, actions, false);
}

bool send_manual_rz1_handshake_31() {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status};
    return drumrom::main_ui_midi::send_manual_rz1_handshake_31(&state, actions);
}

std::vector<unsigned char> build_sample_pad_sysex_bytes() {
    constexpr std::array<std::size_t, 4> kSamplePadSlots = {6u, 14u, 7u, 15u};
    constexpr std::size_t kPadBytes = 4096u;
    constexpr std::size_t kTotalSampleBytes = kPadBytes * 4u;
    constexpr std::size_t kBlockNibbleBytes = 256u;

    std::vector<unsigned char> sample_bytes(kTotalSampleBytes, 0x00u);

    struct SegmentWrite {
        std::size_t slot_idx;
        std::size_t byte_offset;
        std::size_t byte_count;
    };

    std::vector<SegmentWrite> writes;
    writes.reserve(4);

    switch (g_ram_sample_layout) {
        case RamSampleLayout::JoinAll:
            writes.push_back({6u, 0u, kTotalSampleBytes});
            break;
        case RamSampleLayout::Join12And34:
            writes.push_back({6u, 0u, kPadBytes * 2u});
            writes.push_back({7u, kPadBytes * 2u, kPadBytes * 2u});
            break;
        case RamSampleLayout::Join12:
            writes.push_back({6u, 0u, kPadBytes * 2u});
            writes.push_back({7u, kPadBytes * 2u, kPadBytes});
            writes.push_back({15u, kPadBytes * 3u, kPadBytes});
            break;
        case RamSampleLayout::Join34:
            writes.push_back({6u, 0u, kPadBytes});
            writes.push_back({14u, kPadBytes, kPadBytes});
            writes.push_back({7u, kPadBytes * 2u, kPadBytes * 2u});
            break;
        case RamSampleLayout::None:
        default:
            writes.push_back({6u, 0u, kPadBytes});
            writes.push_back({14u, kPadBytes, kPadBytes});
            writes.push_back({7u, kPadBytes * 2u, kPadBytes});
            writes.push_back({15u, kPadBytes * 3u, kPadBytes});
            break;
    }

    std::array<std::optional<std::vector<std::int8_t>>, 16> rendered_cache;
    for (const SegmentWrite& w : writes) {
        if (w.slot_idx >= g_slot_cfg.size()) {
            return {};
        }

        if (!rendered_cache[w.slot_idx].has_value()) {
            const SlotConfig& cfg = g_slot_cfg[w.slot_idx];
            if (cfg.source == SourceKind::Sample && cfg.sample.path.empty()) {
                g_last_sample_error = std::string("Sample path is empty for ") + kSlots[w.slot_idx].label;
                return {};
            }

            std::mt19937 rng(cfg.seed);
            auto rendered = render_slot(w.slot_idx, rng);
            if (rendered.empty()) {
                if (g_last_sample_error.empty()) {
                    g_last_sample_error = std::string("Failed to render ") + kSlots[w.slot_idx].label;
                }
                return {};
            }
            rendered_cache[w.slot_idx] = std::move(rendered);
        }

        const std::vector<std::int8_t>& rendered = *rendered_cache[w.slot_idx];
        const std::size_t copy_count = std::min(w.byte_count, rendered.size());
        for (std::size_t i = 0; i < copy_count; ++i) {
            sample_bytes[w.byte_offset + i] = static_cast<unsigned char>(rendered[i]);
        }
    }

    // Last byte carries sample-link metadata bits on hardware.
    unsigned char link_flags = 0x00u;
    switch (g_ram_sample_layout) {
        case RamSampleLayout::JoinAll:      link_flags = 0x20u; break;
        case RamSampleLayout::Join12:       link_flags = 0x80u; break;
        case RamSampleLayout::Join34:       link_flags = 0x40u; break;
        case RamSampleLayout::Join12And34:  link_flags = 0xC0u; break;
        case RamSampleLayout::None:
        default:                            link_flags = 0x00u; break;
    }
    if (!sample_bytes.empty()) {
        sample_bytes.back() = link_flags;
    }

    std::vector<unsigned char> nibble_bytes;
    nibble_bytes.reserve(sample_bytes.size() * 2u);
    for (unsigned char b : sample_bytes) {
        nibble_bytes.push_back(static_cast<unsigned char>((b >> 4) & 0x0Fu));
        nibble_bytes.push_back(static_cast<unsigned char>(b & 0x0Fu));
    }

    const int channel_index = std::clamp(g_rz1_sysex_channel, 1, 16) - 1;
    const unsigned char channel_byte = static_cast<unsigned char>(
        0x70u | static_cast<unsigned char>(channel_index));

    const std::size_t block_count =
        (nibble_bytes.size() + (kBlockNibbleBytes - 1u)) / kBlockNibbleBytes;
    const std::size_t separator_pairs = block_count > 0 ? (block_count - 1u) : 0u;

    std::vector<unsigned char> syx;
    syx.reserve(7u + nibble_bytes.size() + (separator_pairs * 2u) + 1u);
    syx.push_back(0xF0u);
    syx.push_back(0x44u);
    syx.push_back(0x01u);
    syx.push_back(0x00u);
    syx.push_back(channel_byte);
    syx.push_back(0x20u);
    syx.push_back(0x00u);

    std::size_t pos = 0;
    while (pos < nibble_bytes.size()) {
        const std::size_t remaining = nibble_bytes.size() - pos;
        const std::size_t chunk = std::min(kBlockNibbleBytes, remaining);
        syx.insert(
            syx.end(),
            nibble_bytes.begin() + static_cast<std::ptrdiff_t>(pos),
            nibble_bytes.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;
        if (pos < nibble_bytes.size()) {
            syx.push_back(channel_byte);
            syx.push_back(0x32u);
        }
    }

    syx.push_back(0xF7u);
    return syx;
}

bool build_sample_pad_sysex(bool send_after_build) {
    const auto syx = build_sample_pad_sysex_bytes();
    if (syx.empty()) {
        const std::string reason =
            g_last_sample_error.empty() ? "unknown render error" : g_last_sample_error;
        g_rz1_sysex_status = std::string("Failed to build sample pad SysEx: ") + reason;
        set_status(g_rz1_sysex_status);
        return false;
    }

    constexpr std::size_t kExpectedSampleSysexSize = 33030u;
    if (syx.size() != kExpectedSampleSysexSize) {
        g_rz1_sysex_status = "Error: built sample SysEx size mismatch (expected 33030, got " +
                             std::to_string(syx.size()) + ")";
        set_status(g_rz1_sysex_status);
        return false;
    }

    g_rz1_sysex_capture = syx;
    g_rz1_sysex_message_count = 1u;
    g_rz1_sysex_overflow = false;
    g_last_rz1_sysex_request = 0x20u;

    if (!save_sample_rz1_sysex_capture()) {
        return false;
    }

    if (send_after_build) {
        return send_rz1_sysex_dump_to_rz1_sample();
    }

    g_rz1_sysex_status = "Built sample pad SysEx (saved to sample SysEx path)";
    set_status(g_rz1_sysex_status);
    return true;
}

enum class TypedRz1SysexKind {
    Unknown,
    Sample,
    Rhythm,
};

bool save_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind kind);
bool load_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind kind);

TypedRz1SysexKind typed_sysex_kind_from_last_request() {
    switch (g_last_rz1_sysex_request) {
        case 0x10u:
        case 0x20u:
            return TypedRz1SysexKind::Sample;
        case 0x14u:
        case 0x24u:
            return TypedRz1SysexKind::Rhythm;
        default:
            return TypedRz1SysexKind::Unknown;
    }
}

const char* typed_sysex_kind_name(TypedRz1SysexKind kind) {
    switch (kind) {
        case TypedRz1SysexKind::Sample: return "sample";
        case TypedRz1SysexKind::Rhythm: return "rhythm";
        default: break;
    }
    return "unknown";
}

std::filesystem::path typed_sysex_path_for_kind(TypedRz1SysexKind kind) {
    if (kind == TypedRz1SysexKind::Sample) {
        return std::filesystem::path("sysex-samples") / "sample_data.syx";
    }
    if (kind == TypedRz1SysexKind::Rhythm) {
        return std::filesystem::path("sysex-rhythm") / "rhythm_data.syx";
    }
    return {};
}

std::filesystem::path typed_sysex_path_for_kind_from_name(TypedRz1SysexKind kind) {
    const std::filesystem::path root =
        (kind == TypedRz1SysexKind::Sample)
            ? std::filesystem::path("sysex-samples")
            : std::filesystem::path("sysex-rhythm");

    std::string name = g_preset_path_buf;
    const auto first_non_space = name.find_first_not_of(" \t\n\r");
    if (first_non_space == std::string::npos) {
        return typed_sysex_path_for_kind(kind);
    }
    const auto last_non_space = name.find_last_not_of(" \t\n\r");
    name = name.substr(first_non_space, last_non_space - first_non_space + 1);

    std::filesystem::path filename(name);
    if (filename.extension() != ".syx") {
        filename += ".syx";
    }
    return root / filename;
}

bool save_typed_rz1_sysex_capture() {
    const TypedRz1SysexKind kind = typed_sysex_kind_from_last_request();
    if (kind == TypedRz1SysexKind::Unknown) {
        g_rz1_sysex_status = "Cannot determine SysEx type (use Sample/Rhythm specific buttons)";
        set_status(g_rz1_sysex_status);
        return false;
    }
    return save_typed_rz1_sysex_capture_for_kind(kind);
}

bool load_typed_rz1_sysex_capture() {
    const TypedRz1SysexKind kind = typed_sysex_kind_from_last_request();
    if (kind == TypedRz1SysexKind::Unknown) {
        g_rz1_sysex_status = "Cannot determine SysEx type (use Sample/Rhythm specific buttons)";
        set_status(g_rz1_sysex_status);
        return false;
    }
    return load_typed_rz1_sysex_capture_for_kind(kind);
}

bool save_sample_rz1_sysex_capture() {
    return save_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind::Sample);
}

bool load_sample_rz1_sysex_capture() {
    return load_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind::Sample);
}

bool save_rhythm_rz1_sysex_capture() {
    return save_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind::Rhythm);
}

bool load_rhythm_rz1_sysex_capture() {
    return load_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind::Rhythm);
}

std::vector<unsigned char> materialize_framed_rz1_sysex_for_save(
    const std::vector<unsigned char>& capture_bytes,
    int rz1_sysex_channel,
    TypedRz1SysexKind kind)
{
    if (capture_bytes.empty()) {
        return {};
    }

    // If capture already appears to be full SysEx (e.g. loaded from .syx),
    // preserve it verbatim.
    if (capture_bytes.front() == 0xF0u) {
        return capture_bytes;
    }

    const int channel_index = std::clamp(rz1_sysex_channel, 1, 16) - 1;
    const unsigned char channel_byte = static_cast<unsigned char>(
        0x70u | static_cast<unsigned char>(channel_index));

    const bool has_inband_control = std::any_of(
        capture_bytes.begin(),
        capture_bytes.end(),
        [](unsigned char b) {
            return b == 0xF7u || (b & 0xF0u) == 0x70u;
        });

    // Header-stripped capture that already includes in-band handshakes/F7:
    // rewrite to retransmit-ready RECEIVE framing per sysexreference.txt.
    // For samples: F0 44 01 00 7n 20 00
    // For rhythm: F0 44 01 00 7n 24 00
    if (has_inband_control) {
        const unsigned char cmd = (kind == TypedRz1SysexKind::Sample) ? 0x20u : 0x24u;
        std::vector<unsigned char> framed;
        framed.reserve(7u + capture_bytes.size() + 1u);
        framed.push_back(0xF0u);
        framed.push_back(0x44u);
        framed.push_back(0x01u);
        framed.push_back(0x00u);
        framed.push_back(channel_byte);
        framed.push_back(cmd);
        framed.push_back(0x00u);
        framed.insert(framed.end(), capture_bytes.begin(), capture_bytes.end());
        if (framed.back() != 0xF7u) {
            framed.push_back(0xF7u);
        }
        return framed;
    }

    constexpr std::size_t kBlockDataBytes = 256u;
    std::vector<unsigned char> framed;
    const std::size_t block_count =
        (capture_bytes.size() + (kBlockDataBytes - 1u)) / kBlockDataBytes;
    const std::size_t separator_pairs = block_count > 0 ? (block_count - 1u) : 0u;
    framed.reserve(6u + capture_bytes.size() + (separator_pairs * 2u) + 1u);

    framed.push_back(0xF0u);
    framed.push_back(0x44u);
    framed.push_back(0x01u);
    framed.push_back(0x00u);
    framed.push_back(channel_byte);
    framed.push_back(0x30u);

    std::size_t pos = 0;
    while (pos < capture_bytes.size()) {
        const std::size_t remaining = capture_bytes.size() - pos;
        const std::size_t chunk = std::min(kBlockDataBytes, remaining);
        framed.insert(
            framed.end(),
            capture_bytes.begin() + static_cast<std::ptrdiff_t>(pos),
            capture_bytes.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;

        if (pos < capture_bytes.size()) {
            framed.push_back(channel_byte);
            framed.push_back(0x32u);
        }
    }

    framed.push_back(0xF7u);
    return framed;
}

void load_split_samples_callback(const std::array<std::string, 4>& sample_paths) {
    // Load split samples into editor slots 6, 14, 7, 15 (sample1-4)
    // Slots are indexed as: sample1=6, sample2=14, sample3=7, sample4=15
    constexpr std::array<std::size_t, 4> slot_indices = {6, 14, 7, 15};
    
    for (std::size_t i = 0; i < 4; ++i) {
        if (i < slot_indices.size() && i < sample_paths.size() && 
            !sample_paths[i].empty() && slot_indices[i] < g_slot_cfg.size()) {
            // Set the sample path for this slot
            g_slot_cfg[slot_indices[i]].sample.path = sample_paths[i];
            g_slot_cfg[slot_indices[i]].sample.source_rate_hz = 20833.0f;  // RZ-1 sample rate
            g_slot_cfg[slot_indices[i]].sample.start_pct = 0;
            g_slot_cfg[slot_indices[i]].sample.end_pct = 100;
            g_slot_cfg[slot_indices[i]].sample.loop_start_pct = 0;
            g_slot_cfg[slot_indices[i]].sample.loop_end_pct = 100;
            g_slot_cfg[slot_indices[i]].sample.loop_increment_pct = 0.0f;
            g_slot_cfg[slot_indices[i]].source = SourceKind::Sample;
            
            g_params_dirty = true;
            g_wave_preview_dirty = true;
        }
    }
    
    set_status("Loaded split samples into sample pads 1-4");
}

bool save_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind kind) {
    if (g_rz1_sysex_capture.empty()) {
        g_rz1_sysex_status = "No captured SysEx data to save";
        set_status(g_rz1_sysex_status);
        return false;
    }

    const std::filesystem::path out_path = typed_sysex_path_for_kind_from_name(kind);
    if (kind == TypedRz1SysexKind::Unknown || out_path.empty()) {
        g_rz1_sysex_status = "Invalid SysEx type for save";
        set_status(g_rz1_sysex_status);
        return false;
    }

    std::error_code ec;
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out.is_open()) {
        g_rz1_sysex_status = std::string("Failed to open file: ") + out_path.string();
        set_status(g_rz1_sysex_status);
        return false;
    }

    const auto sysex_bytes = materialize_framed_rz1_sysex_for_save(
        g_rz1_sysex_capture,
        g_rz1_sysex_channel,
        kind);
    if (sysex_bytes.empty()) {
        g_rz1_sysex_status = "No SysEx data available to write";
        set_status(g_rz1_sysex_status);
        return false;
    }

    if (kind == TypedRz1SysexKind::Sample) {
        constexpr std::size_t kExpectedSampleSysexSize = 33030u;
        if (sysex_bytes.size() != kExpectedSampleSysexSize) {
            g_rz1_sysex_status = "Error: sample SysEx size mismatch (expected 33030, got " +
                                 std::to_string(sysex_bytes.size()) + ")";
            set_status(g_rz1_sysex_status);
            return false;
        }
    } else if (kind == TypedRz1SysexKind::Rhythm) {
        constexpr std::size_t kExpectedRhythmSysexSize = 33030u;
        if (sysex_bytes.size() != kExpectedRhythmSysexSize) {
            g_rz1_sysex_status = "Error: rhythm SysEx size mismatch (expected 33030, got " +
                                 std::to_string(sysex_bytes.size()) + ")";
            set_status(g_rz1_sysex_status);
            return false;
        }
    }

    out.write(reinterpret_cast<const char*>(sysex_bytes.data()),
              static_cast<std::streamsize>(sysex_bytes.size()));
    if (!out.good()) {
        g_rz1_sysex_status = std::string("Failed to write file: ") + out_path.string();
        set_status(g_rz1_sysex_status);
        return false;
    }

    g_rz1_sysex_status = std::string("Saved ") + typed_sysex_kind_name(kind) + " SysEx: " + out_path.string();
    set_status(g_rz1_sysex_status);
    return true;
}

bool load_typed_rz1_sysex_capture_for_kind(TypedRz1SysexKind kind) {
    const std::filesystem::path in_path = typed_sysex_path_for_kind_from_name(kind);
    if (kind == TypedRz1SysexKind::Unknown || in_path.empty()) {
        g_rz1_sysex_status = "Invalid SysEx type for load";
        set_status(g_rz1_sysex_status);
        return false;
    }

    std::ifstream in(in_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        g_rz1_sysex_status = std::string("Failed to open file: ") + in_path.string();
        set_status(g_rz1_sysex_status);
        return false;
    }

    const std::streamsize file_size = in.tellg();
    if (file_size <= 0) {
        g_rz1_sysex_status = std::string("File is empty: ") + in_path.string();
        set_status(g_rz1_sysex_status);
        return false;
    }
    if (static_cast<std::size_t>(file_size) > kRz1SysexCaptureMaxBytes) {
        g_rz1_sysex_status = "File is too large for SysEx capture buffer";
        set_status(g_rz1_sysex_status);
        return false;
    }

    g_rz1_sysex_capture.resize(static_cast<std::size_t>(file_size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(g_rz1_sysex_capture.data()), file_size);
    if (!in.good()) {
        g_rz1_sysex_status = std::string("Failed to read file: ") + in_path.string();
        set_status(g_rz1_sysex_status);
        return false;
    }

    std::size_t message_count = 0;
    for (unsigned char byte : g_rz1_sysex_capture) {
        if (byte == 0xF0u) {
            ++message_count;
        }
    }
    g_rz1_sysex_message_count = std::max<std::size_t>(message_count, 1u);
    g_rz1_sysex_overflow = false;
    g_rz1_sysex_status = std::string("Loaded ") + typed_sysex_kind_name(kind) + " SysEx: " + in_path.string();
    set_status(g_rz1_sysex_status);
    return true;
}

void poll_midi_input() {
    auto state = make_main_ui_midi_state();
    const drumrom::main_ui_midi::Actions actions{&set_status, &load_split_samples_callback};
    state.actions = const_cast<drumrom::main_ui_midi::Actions*>(&actions);
    drumrom::main_ui_midi::PollContext context{};
    context.editor_page_active = (g_ui_page == UiPage::Editor);
    context.slot_base_midi_note = 36;
    context.slot_count = kSlots.size();
    drumrom::main_ui_midi::poll_midi_input(&state, context);
}

void render_ui();

#include "main_imgui_runtime_bridges.inl"

void render_settings_page() {
    drumrom::ui_settings_page::Model model{};
    model.available_midi_in_ports = &g_available_midi_in_ports;
    model.available_midi_out_ports = &g_available_midi_out_ports;
    model.selected_midi_in_port = &g_selected_midi_in_port;
    model.selected_midi_out_port = &g_selected_midi_out_port;
    model.settings_midi_in_port_index = &g_settings.midi_in_port_index;
    model.settings_midi_out_port_index = &g_settings.midi_out_port_index;
    model.midi_in_status = &g_midi_in_status;
    model.midi_out_status = &g_midi_out_status;
    model.midi_in_enabled = &g_midi_in_enabled;
    model.midi_out_enabled = &g_midi_out_enabled;
    model.available_onerom_serials = &g_available_onerom_serials;
    model.selected_onerom_rom_a = &g_selected_onerom_rom_a;
    model.selected_onerom_rom_b = &g_selected_onerom_rom_b;
    model.onerom_single_device_role = &g_settings.onerom_single_device_role;
    model.onerom_serial_rom_a = g_settings.onerom_serial_rom_a;
    model.onerom_serial_rom_a_capacity = sizeof(g_settings.onerom_serial_rom_a);
    model.onerom_serial_rom_b = g_settings.onerom_serial_rom_b;
    model.onerom_serial_rom_b_capacity = sizeof(g_settings.onerom_serial_rom_b);
    model.onerom_usb_status = &g_onerom_usb_status;
    model.samples_folder = g_settings.samples_folder;
    model.samples_folder_capacity = sizeof(g_settings.samples_folder);
    model.monitor_width = &g_settings.monitor_width;
    model.monitor_height = &g_settings.monitor_height;
    model.window_resize_pending = &g_window_resize_pending;
    model.pending_window_width = &g_pending_window_width;
    model.pending_window_height = &g_pending_window_height;
    model.loop_split_reset_slots = &g_settings.loop_split_reset_slots;
    model.loop_split_target_pads = &g_settings.loop_split_target_pads;
    model.loop_split_autofit = &g_settings.loop_split_autofit;
    model.ui_scale = g_ui_scale;
    model.status_color = kColorGreen;

    const drumrom::ui_settings_page::Actions actions{
        &refresh_midi_in_ports,
        &open_midi_in_port,
        &close_midi_in_port,
        &refresh_midi_out_ports,
        &open_midi_out_port,
        &close_midi_out_port,
        &refresh_onerom_usb_devices,
        &save_settings,
        &restore_default_rz1_kit_file,
        &load_default_rz1_kit_into_editor,
        &set_status,
        &initialize_history_if_needed,
        &maybe_commit_history,
    };

    drumrom::ui_settings_page::render(&model, actions);
}

void render_pin_matrix_page() {
    drumrom::ui_pin_matrix_page::Model model{};
    model.pinbend_matrix = &g_pinbend_matrix;
    model.pin_labels = &kEprom27c256PinLabels;
    model.ui_scale = g_ui_scale;
    model.color_green = kColorGreen;
    model.color_green_dark = kColorGreenDark;
    model.color_green_lite = kColorGreenLite;

    const drumrom::ui_pin_matrix_page::Actions actions{
        &set_status,
        &send_pin_matrix_to_onerom_usb,
    };

    drumrom::ui_pin_matrix_page::render(&model, actions);
}

drumrom::main_ui_overlay_controls::State make_main_ui_overlay_controls_state() {
    drumrom::main_ui_overlay_controls::State state{};
    state.selected_overlay = reinterpret_cast<int*>(&g_overlay_selected);
    state.auto_upload_commit_requested = &g_auto_upload_commit_requested;
    state.auto_play_commit_requested = &g_auto_play_commit_requested;
    return state;
}

bool colored_slider_float(const char* label, float* v, float v_min, float v_max, ImVec4 color, OverlayId envelope_to_select, const char* format = "%.2f") {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_float(
        &state,
        label,
        v,
        v_min,
        v_max,
        color,
        static_cast<int>(envelope_to_select),
        format);
}

bool colored_slider_rate(const char* label, float* internal_rate, float min_rate, float max_rate, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_rate(
        &state,
        label,
        internal_rate,
        min_rate,
        max_rate,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_slider_attack_rate(const char* label, float* rate, float min_rate, float max_rate, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_attack_rate(
        &state,
        label,
        rate,
        min_rate,
        max_rate,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_slider_float_labeled(const char* label, float* v, float v_min, float v_max, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_float_labeled(
        &state,
        label,
        v,
        v_min,
        v_max,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_slider_rate_labeled(const char* label, float* internal_rate, float min_rate, float max_rate, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_rate_labeled(
        &state,
        label,
        internal_rate,
        min_rate,
        max_rate,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_slider_attack_rate_labeled(const char* label, float* internal_rate, float min_rate, float max_rate, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_slider_attack_rate_labeled(
        &state,
        label,
        internal_rate,
        min_rate,
        max_rate,
        color,
        static_cast<int>(envelope_to_select));
}

bool shape_combo(const char* label, EnvelopeShape* shape) {
    return drumrom::main_ui_overlay_controls::shape_combo(label, shape);
}

float compact_shape_combo_width() {
    return drumrom::main_ui_overlay_controls::compact_shape_combo_width();
}

float shape_row_left_label_anchor_width() {
    return drumrom::main_ui_overlay_controls::shape_row_left_label_anchor_width();
}

bool colored_shape_combo(const char* label, EnvelopeShape* shape, ImVec4 color, OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_shape_combo(
        &state,
        label,
        shape,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_rate_with_shape_row(const char* rate_label,
                                 float* rate,
                                 float min_rate,
                                 float max_rate,
                                 const char* shape_label,
                                 EnvelopeShape* shape,
                                 ImVec4 color,
                                 OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_rate_with_shape_row(
        &state,
        rate_label,
        rate,
        min_rate,
        max_rate,
        shape_label,
        shape,
        color,
        static_cast<int>(envelope_to_select));
}

bool colored_attack_with_shape_row(const char* attack_label,
                                   float* attack_rate,
                                   float min_rate,
                                   float max_rate,
                                   const char* shape_label,
                                   EnvelopeShape* shape,
                                   ImVec4 color,
                                   OverlayId envelope_to_select) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::colored_attack_with_shape_row(
        &state,
        attack_label,
        attack_rate,
        min_rate,
        max_rate,
        shape_label,
        shape,
        color,
        static_cast<int>(envelope_to_select));
}

void envelope_group_gap() {
    drumrom::main_ui_overlay_controls::envelope_group_gap();
}

bool render_output_controls(float* output_gain_db, float* limiter_ceiling, int* output_shaper_mode, float* output_saturation) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::render_output_controls(
        &state,
        output_gain_db,
        limiter_ceiling,
        output_shaper_mode,
        output_saturation);
}

bool input_int_with_scroll(const char* label, int* v, int v_min, int v_max) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::input_int_with_scroll(&state, label, v, v_min, v_max);
}

bool slider_float_with_text_input(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::slider_float_with_text_input(&state, label, v, v_min, v_max, format, flags);
}

bool slider_rate_with_text_input(const char* label, float* internal_rate, float min_rate, float max_rate, bool attack_mode, const char* format, ImGuiSliderFlags flags) {
    auto state = make_main_ui_overlay_controls_state();
    return drumrom::main_ui_overlay_controls::slider_rate_with_text_input(
        &state,
        label,
        internal_rate,
        min_rate,
        max_rate,
        attack_mode,
        format,
        flags);
}

#include "main_imgui_overlay_helpers.inl"

void set_ui_page(int page_index) {
    switch (page_index) {
        case 0: g_ui_page = UiPage::Editor; break;
        case 1: g_ui_page = UiPage::PinMatrix; break;
        case 2: g_ui_page = UiPage::Settings; break;
        default: break;
    }
}

void render_page_selector_at_bottom() {
    // Render page selector panel pinned to the absolute bottom-right of the main window.
    // Uses SetCursorPos so the Y position is constant regardless of which page is active.
    constexpr float kPageButtonWidth = 100.0f;
    const float scaled_page_width = kPageButtonWidth * g_ui_scale;

    const float page_controls_width = (scaled_page_width * 3.0f) + (ImGui::GetStyle().ItemSpacing.x * 2.0f);
    const float pad_x = ImGui::GetStyle().WindowPadding.x;
    const float pad_y = ImGui::GetStyle().WindowPadding.y;
    const float panel_w = page_controls_width + pad_x * 2.0f;
    const float panel_h = ImGui::GetFrameHeight() + pad_y * 2.0f;

    const float panel_x = ImGui::GetWindowContentRegionMax().x - panel_w;
    const float panel_y = ImGui::GetWindowHeight() - panel_h - pad_y;

    ImGui::SetCursorPos(ImVec2(panel_x, std::max(0.0f, panel_y)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    if (ImGui::BeginChild("PageSelectorPanel", ImVec2(panel_w, panel_h), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::Button("Editor", ImVec2(scaled_page_width, 0.0f))) {
            set_ui_page(0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Pin Matrix", ImVec2(scaled_page_width, 0.0f))) {
            set_ui_page(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Settings", ImVec2(scaled_page_width, 0.0f))) {
            set_ui_page(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void render_bottom_toolbar() {
    static std::vector<drumrom::ui_bottom_toolbar::SlotItem> slot_items;
    if (slot_items.size() != kSlots.size()) {
        slot_items.clear();
        slot_items.reserve(kSlots.size());
        for (const auto& slot : kSlots) {
            drumrom::ui_bottom_toolbar::SlotItem item;
            item.label = slot.label;
            item.is_sample_pad = slot.is_ram_sample;
            slot_items.push_back(item);
        }
    }
    for (std::size_t i = 0; i < slot_items.size(); ++i) {
        slot_items[i].is_enabled = is_slot_enabled(i);
    }

    int current_page = 0;
    switch (g_ui_page) {
        case UiPage::Editor: current_page = 0; break;
        case UiPage::PinMatrix: current_page = 1; break;
        case UiPage::Settings: current_page = 2; break;
    }

    drumrom::ui_bottom_toolbar::Model model{};
    model.slots = &slot_items;
    model.selected_slot = g_selected_slot;
    model.show_slot_selection = (g_ui_page == UiPage::Editor);
    model.ui_scale = g_ui_scale;
    model.current_page = current_page;
    model.ram_sample_layout = g_ram_sample_layout;

    const drumrom::ui_bottom_toolbar::Actions actions{
        &select_slot,
        &set_ui_page,
        &set_ram_sample_layout,
    };

    drumrom::ui_bottom_toolbar::render(&model, actions);
}

#include "main_imgui_editor_helpers.inl"

void render_ui() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("RZ-1 Drum ROM Editor", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::End();
        return;
    }

    // Apply theme
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColorGreenDark);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, kColorGreen);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kColorGreenLite);
    ImGui::PushStyleColor(ImGuiCol_Button, kColorGreenDark);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColorGreenLite);
    ImGui::PushStyleColor(ImGuiCol_Header, kColorGreenDark);

    drumrom::main_ui_orchestration::NonEditorPageModel non_editor_model{};
    non_editor_model.ui_page = static_cast<int>(g_ui_page);
    non_editor_model.editor_page = static_cast<int>(UiPage::Editor);
    non_editor_model.pin_matrix_page = static_cast<int>(UiPage::PinMatrix);
    non_editor_model.settings_page = static_cast<int>(UiPage::Settings);
    non_editor_model.status_expire_time = g_status_expire_time;
    non_editor_model.status = &g_status;
    non_editor_model.status_color = kColorGreen;
    non_editor_model.ui_scale = g_ui_scale;

    const drumrom::main_ui_orchestration::NonEditorPageActions non_editor_actions{
        &render_settings_page,
        &render_pin_matrix_page,
        &set_ui_page,
    };

    if (drumrom::main_ui_orchestration::render_non_editor_page_if_active(non_editor_model, non_editor_actions)) {
        render_page_selector_at_bottom();
        ImGui::PopStyleColor(9);
        ImGui::End();
        return;
    }

    SlotConfig& cfg = g_slot_cfg[g_selected_slot];
    sync_sample_path_from_slot();

    // Clear any leftover frame-specific state
    // (Note: g_focused_listbox_id persists until a new listbox is clicked)

    bool changed = false;

    drumrom::main_ui_editor_dispatch::Model editor_model{};
    editor_model.ui_scale = g_ui_scale;
    editor_model.waveform_pane_height = g_layout_cfg.waveform_pane_height;
    editor_model.status = &g_status;
    editor_model.status_expire_time = g_status_expire_time;
    editor_model.status_color = kColorGreen;

    drumrom::main_ui_editor_dispatch::State editor_state{};
    editor_state.slot_config = &cfg;
    editor_state.changed = &changed;

    const drumrom::main_ui_editor_dispatch::Actions editor_actions{
        &render_editor_left_pane_bridge,
        &render_action_pane_bridge,
        &render_bottom_toolbar,
    };

    drumrom::main_ui_editor_dispatch::render(editor_model, &editor_state, editor_actions);

    drumrom::main_ui_orchestration::ChangeClampState clamp_state{};
    clamp_state.start_pct = &cfg.sample.start_pct;
    clamp_state.end_pct = &cfg.sample.end_pct;
    clamp_state.loop_start_pct = &cfg.sample.loop_start_pct;
    clamp_state.loop_end_pct = &cfg.sample.loop_end_pct;
    clamp_state.params_dirty = &g_params_dirty;
    clamp_state.wave_preview_dirty = &g_wave_preview_dirty;
    clamp_state.history_commit_pending = &g_history_commit_pending;
    drumrom::main_ui_orchestration::apply_editor_change_flags(changed, &clamp_state);

    drumrom::main_ui_orchestration::CommitCycleState commit_state{};
    commit_state.auto_upload_commit_requested = &g_auto_upload_commit_requested;
    commit_state.auto_play_commit_requested = &g_auto_play_commit_requested;
    commit_state.history_commit_pending = &g_history_commit_pending;

    const drumrom::main_ui_orchestration::CommitCycleActions commit_actions{
        &maybe_commit_history,
        &maybe_auto_upload_current_slot,
        &maybe_auto_play_current_slot,
    };
    drumrom::main_ui_orchestration::finalize_editor_commit_cycle(&commit_state, commit_actions);

    // Render page selector pinned to absolute bottom of window (all pages)
    render_page_selector_at_bottom();

    ImGui::PopStyleColor(9);
    ImGui::End();
}

}  // namespace

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    SDL_Renderer* renderer = nullptr;
    auto bootstrap_state = make_runtime_bootstrap_state(&renderer);
    if (!drumrom::main_ui_runtime_bootstrap::initialize(&bootstrap_state)) {
        return 1;
    }

    auto startup_state = make_runtime_startup_state();
    drumrom::main_ui_runtime_startup::perform_startup(&startup_state);
    refresh_onerom_usb_devices();

    update_ui_scale_for_window();

    const auto event_actions = make_runtime_event_actions();
    const auto frame_actions = make_runtime_frame_actions();

    bool running = true;
    while (running) {
        auto event_state = make_runtime_event_state(&running);
        drumrom::main_ui_runtime_events::process_events(&event_state, event_actions);
        drumrom::main_ui_runtime_frame::run(renderer, frame_actions);
    }

    auto cleanup_state = make_runtime_cleanup_state(renderer);
    drumrom::main_ui_runtime_cleanup::shutdown(&cleanup_state);

    return 0;
}
