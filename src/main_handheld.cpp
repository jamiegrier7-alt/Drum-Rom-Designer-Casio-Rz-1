#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "drumrom/synth.h"
#include "drumrom/synth_elements.h"
#include "drumrom/synth_elements_exact.h"
#include "drumrom/sample_dsp_shared.h"
#include "drumrom/sample_param_schema.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

enum class HandheldPage {
    SlotSelect = 0,
    Edit = 1,
    Library = 2,
    Export = 3,
    Settings = 4,
    Status = 5,
};

constexpr std::array<const char*, 6> kPageTitles = {{"Slots", "Edit", "Library", "Export", "Settings", "Status"}};
constexpr std::array<const char*, 16> kSlotFileNames = {{
    "tom1", "tom3", "rimshot", "open_hihat", "clap", "cowbell", "sample1", "sample3",
    "tom2", "kick", "snare", "closed_hihat", "ride", "crash", "sample2", "sample4",
}};
constexpr std::array<std::size_t, 16> kDefaultSlotCapacities = {{
    3791, 4087, 1303, 12009, 2511, 1951, 4096, 4096,
    3844, 1627, 3224, 1223, 13935, 14371, 4096, 4096,
}};

enum class SlotSourceKind {
    Synth = 0,
    Sample = 1,
    Loop = 2,
};

enum class SynthType {
    Kick = 0,
    Snare = 1,
    Hihat = 2,
    Tom = 3,
    Clap = 4,
    Elements = 5,
    ElementsExact = 6,
};

struct SampleParams {
    float source_rate_hz = 20833.0f;
    int start_pct = 0;
    int end_pct = 100;
    int loop_start_pct = 0;
    int loop_end_pct = 100;
    float loop_increment_pct = 0.0f;
    float tune_semitones = 0.0f;
    float filter_cutoff_hz = 9000.0f;
    float filter_cutoff_end_hz = 1800.0f;
    float filter_env_decay_s = 0.18f;
    float filter_resonance = 0.2f;
    float amp_attack_s = 0.001f;
    float amp_decay_s = 0.06f;
    float amp_sustain = 0.9f;
    float amp_release_s = 0.02f;
    int amp_mode = 2;  // 0=Off, 1=PreFit, 2=Output
    float output_gain_db = 0.0f;
    float limiter_ceiling = 1.0f;
    int output_shaper_mode = 2;
    float output_saturation = 0.65f;
};

struct EditSnapshot {
    std::array<SlotSourceKind, 16> slot_source_kinds{};
    std::array<SynthType, 16> slot_synth_types{};
    std::array<std::string, 16> slot_sample_paths{};
    std::array<std::array<int, 24>, 16> slot_param_values{};
    std::array<drumrom::synth::DrumParams, 16> slot_drum_params{};
    std::array<SampleParams, 16> slot_sample_params{};
    std::array<drumrom::synth::ElementsParams, 16> slot_elements_params{};
    std::array<drumrom::synth::ElementsExactParams, 16> slot_elements_exact_params{};
};

struct AppState {
    HandheldPage page = HandheldPage::SlotSelect;
    std::size_t selected_slot = 0;
    std::size_t selected_param = 0;
    bool edit_focus_on_slot_column = false;
    bool edit_focus_on_type_bar = false;
    bool page_menu_open = false;
    int page_menu_index = 0;
    int selected_type_token = 0;
    std::array<SlotSourceKind, 16> slot_source_kinds{};
    std::array<SynthType, 16> slot_synth_types{};
    std::array<std::string, 16> slot_sample_paths{};
    std::array<std::size_t, 16> slot_capacities{};
    std::array<std::array<int, 24>, 16> slot_param_values{};
    std::array<drumrom::synth::DrumParams, 16> slot_drum_params{};
    std::array<SampleParams, 16> slot_sample_params{};
    std::array<drumrom::synth::ElementsParams, 16> slot_elements_params{};
    std::array<drumrom::synth::ElementsExactParams, 16> slot_elements_exact_params{};
    bool param_adjust_held = false;
    bool waveform_dirty = true;
    std::vector<float> selected_waveform;
    std::size_t selected_waveform_slot = static_cast<std::size_t>(-1);
    std::vector<EditSnapshot> undo_stack;
    bool default_kit_loaded = false;
    int default_kit_loaded_slots = 0;
    bool running = true;
};

void clamp_selected_param_to_view(AppState* state);
void apply_drum_params_to_slot_values(AppState* state, std::size_t slot, const drumrom::synth::DrumParams& params);
void sync_slot_values_from_sample_params(AppState* state, std::size_t slot);
void sync_sample_params_from_slot_values(AppState* state, std::size_t slot);

constexpr std::size_t kMaxUndoDepth = 128;

void push_undo_snapshot(AppState* state) {
    if (state == nullptr) {
        return;
    }
    EditSnapshot snap{};
    snap.slot_source_kinds = state->slot_source_kinds;
    snap.slot_synth_types = state->slot_synth_types;
    snap.slot_sample_paths = state->slot_sample_paths;
    snap.slot_param_values = state->slot_param_values;
    snap.slot_drum_params = state->slot_drum_params;
    snap.slot_sample_params = state->slot_sample_params;
    snap.slot_elements_params = state->slot_elements_params;
    snap.slot_elements_exact_params = state->slot_elements_exact_params;
    if (state->undo_stack.size() >= kMaxUndoDepth) {
        state->undo_stack.erase(state->undo_stack.begin());
    }
    state->undo_stack.push_back(std::move(snap));
}

bool pop_undo_snapshot(AppState* state) {
    if (state == nullptr || state->undo_stack.empty()) {
        return false;
    }
    const EditSnapshot snap = std::move(state->undo_stack.back());
    state->undo_stack.pop_back();
    state->slot_source_kinds = snap.slot_source_kinds;
    state->slot_synth_types = snap.slot_synth_types;
    state->slot_sample_paths = snap.slot_sample_paths;
    state->slot_param_values = snap.slot_param_values;
    state->slot_drum_params = snap.slot_drum_params;
    state->slot_sample_params = snap.slot_sample_params;
    state->slot_elements_params = snap.slot_elements_params;
    state->slot_elements_exact_params = snap.slot_elements_exact_params;
    state->waveform_dirty = true;
    clamp_selected_param_to_view(state);
    return true;
}

constexpr std::array<const char*, 24> kEditorParamNames = {{
    "TUNE", "LVL", "PAN", "ATK", "DEC", "SUS",
    "REL", "FLT", "RES", "DRV", "PCH", "MOD",
    "TON", "SPD", "LEN", "STA", "END", "LOOP",
    "SHP", "NOI", "FM", "AM", "AUX1", "AUX2",
}};

struct ParamViewEntry {
    const char* label;
    int value_index;
};

struct ParamView {
    const ParamViewEntry* entries;
    std::size_t count;
};

constexpr std::array<ParamViewEntry, 20> kParamViewSample = {{
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::StartPct).handheld_label, 0},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::EndPct).handheld_label, 1},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::LoopStartPct).handheld_label, 2},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::LoopEndPct).handheld_label, 3},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::LoopIncrementPct).handheld_label, 4},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::TuneSemitones).handheld_label, 5},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::FilterCutoffStartHz).handheld_label, 6},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::FilterCutoffEndHz).handheld_label, 7},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::FilterEnvDecayS).handheld_label, 8},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::FilterResonance).handheld_label, 9},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::AmpAttackS).handheld_label, 10},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::AmpDecayS).handheld_label, 11},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::AmpSustain).handheld_label, 12},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::AmpReleaseS).handheld_label, 13},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::AmpMode).handheld_label, 14},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::OutputGainDb).handheld_label, 15},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::LimiterCeiling).handheld_label, 16},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::OutputShaperMode).handheld_label, 17},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::OutputSaturation).handheld_label, 18},
    {drumrom::sample_schema::sample_param_spec(drumrom::sample_schema::SampleParamId::SourceRateHz).handheld_label, 19}
}};

constexpr std::array<const char*, 10> kElementsParamLabels = {{
    "Model", "Frequency", "Brightness", "Damping", "Position",
    "Exciter Level", "Exciter Noise", "Exciter Dur", "Env Decay", "Env Attack",
}};

constexpr std::array<const char*, 39> kElementsExactParamLabels = {{
    "Note", "Modulation", "Strength", "Blow CV", "Strike CV",
    "Exciter Env Shape", "Bow Level", "Bow Timbre", "Blow Level", "Blow Meta",
    "Blow Timbre", "Strike Level", "Strike Meta", "Strike Timbre", "Signature",
    "Res Geometry", "Res Brightness", "Res Damping", "Res Position", "Res Mod Freq",
    "Res Mod Offset", "Reverb Diffusion", "Reverb LP", "Space", "Mod Frequency",
    "Res Model", "Easter Egg", "Env1 Target", "Env1 Amount", "Env1 Attack",
    "Env1 Decay", "Env1 Sustain", "Env1 Release", "Env2 Target", "Env2 Amount",
    "Env2 Attack", "Env2 Decay", "Env2 Sustain", "Env2 Release",
}};

void normalize_sample_slot_params(AppState* state, std::size_t slot);

constexpr std::array<ParamViewEntry, 24> kParamViewKick = {{
    {"Pitch Start", 0}, {"Amp Decay", 1}, {"Harmonic Mix", 2}, {"Attack", 3},
    {"Pitch Decay", 4}, {"Snap", 5}, {"Tail", 6}, {"Filter Cut", 7},
    {"Filter Res", 8}, {"Tone Decay", 9}, {"Pitch", 10}, {"Pitch Mod", 11},
    {"Tone", 12}, {"Width", 13}, {"Length", 14}, {"Pre Delay", 15},
    {"Reverb", 16}, {"Wet", 17}, {"Rev Shape", 18}, {"Noise", 19},
    {"FM Mix", 20}, {"AM Depth", 21}, {"Early", 22}, {"Diffusion", 23},
}};

constexpr std::array<ParamViewEntry, 24> kParamViewSnare = {{
    {"Body", 0}, {"Amp Decay", 1}, {"Ring", 2}, {"Attack", 3},
    {"Bend", 4}, {"Tone Mix", 5}, {"Noise Decay", 6}, {"Filter Cut", 7},
    {"Filter Res", 8}, {"Tone Decay", 9}, {"Pitch", 10}, {"Pitch Mod", 11},
    {"Tone", 12}, {"Width", 13}, {"Length", 14}, {"Pre Delay", 15},
    {"Reverb", 16}, {"Wet", 17}, {"Rev Shape", 18}, {"Noise", 19},
    {"FM Mix", 20}, {"AM Depth", 21}, {"Early", 22}, {"Diffusion", 23},
}};

constexpr std::array<ParamViewEntry, 24> kParamViewHihat = {{
    {"Body", 0}, {"Decay", 1}, {"Mix", 2}, {"Attack", 3},
    {"Tail", 4}, {"Tone Mix", 5}, {"Noise Decay", 6}, {"Filter Cut", 7},
    {"Filter Res", 8}, {"Tone Decay", 9}, {"Pitch", 10}, {"Pitch Mod", 11},
    {"Tone", 12}, {"Width", 13}, {"Length", 14}, {"Pre Delay", 15},
    {"Reverb", 16}, {"Wet", 17}, {"Rev Shape", 18}, {"Noise", 19},
    {"FM Mix", 20}, {"AM Depth", 21}, {"Early", 22}, {"Diffusion", 23},
}};

constexpr std::array<ParamViewEntry, 24> kParamViewTom = {{
    {"Pitch Start", 0}, {"Amp Decay", 1}, {"Mix", 2}, {"Attack", 3},
    {"Pitch Decay", 4}, {"Tone Mix", 5}, {"Noise Decay", 6}, {"Filter Cut", 7},
    {"Filter Res", 8}, {"Tone Decay", 9}, {"Pitch", 10}, {"Pitch Drop", 11},
    {"Tone", 12}, {"Width", 13}, {"Duration", 14}, {"Pre Delay", 15},
    {"Reverb", 16}, {"Wet", 17}, {"Rev Shape", 18}, {"Noise", 19},
    {"FM Mix", 20}, {"AM Depth", 21}, {"Early", 22}, {"Diffusion", 23},
}};

constexpr std::array<ParamViewEntry, 24> kParamViewClap = {{
    {"Body", 0}, {"Amp Decay", 1}, {"Mix", 2}, {"Attack", 3},
    {"Decay", 4}, {"Tone Mix", 5}, {"Noise Decay", 6}, {"Filter Cut", 7},
    {"Filter Res", 8}, {"Tone Decay", 9}, {"Pitch", 10}, {"Pitch Mod", 11},
    {"Tone", 12}, {"Width", 13}, {"Click Rate", 14}, {"Pre Delay", 15},
    {"Reverb", 16}, {"Wet", 17}, {"Rev Shape", 18}, {"Noise", 19},
    {"FM Mix", 20}, {"AM Depth", 21}, {"Early", 22}, {"Diffusion", 23},
}};

constexpr std::array<ParamViewEntry, 9> kParamViewElements = {{
    {"Model", 23},
    {"Freq", 10},
    {"Bright", 12},
    {"Damping", 4},
    {"Position", 2},
    {"Exciter", 1},
    {"Noise", 19},
    {"Exc Dur", 14},
    {"Attack", 3},
}};

constexpr std::array<ParamViewEntry, 11> kParamViewElementsExact = {{
    {"Note", 10},
    {"Mod", 11},
    {"Strength", 1},
    {"Blow", 19},
    {"Strike", 20},
    {"Geom", 2},
    {"Bright", 12},
    {"Damping", 4},
    {"Pos", 7},
    {"Space", 17},
    {"Diff", 23},
}};

ParamView current_param_view(const AppState* state) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return {kParamViewSample.data(), kParamViewSample.size()};
    }
    if (state->slot_source_kinds[state->selected_slot] == SlotSourceKind::Sample ||
        state->slot_source_kinds[state->selected_slot] == SlotSourceKind::Loop) {
        return {kParamViewSample.data(), kParamViewSample.size()};
    }

    if (state->slot_synth_types[state->selected_slot] == SynthType::Elements ||
        state->slot_synth_types[state->selected_slot] == SynthType::ElementsExact) {
        return {nullptr, 0};
    }

    switch (state->slot_synth_types[state->selected_slot]) {
        case SynthType::Kick: return {kParamViewKick.data(), kParamViewKick.size()};
        case SynthType::Snare: return {kParamViewSnare.data(), kParamViewSnare.size()};
        case SynthType::Hihat: return {kParamViewHihat.data(), kParamViewHihat.size()};
        case SynthType::Tom: return {kParamViewTom.data(), kParamViewTom.size()};
        case SynthType::Clap: return {kParamViewClap.data(), kParamViewClap.size()};
        case SynthType::Elements: return {kParamViewElements.data(), kParamViewElements.size()};
        case SynthType::ElementsExact: return {kParamViewElementsExact.data(), kParamViewElementsExact.size()};
    }
    return {kParamViewTom.data(), kParamViewTom.size()};
}

int encode_linear(float v, float min_v, float max_v) {
    if (max_v <= min_v) {
        return 0;
    }
    const float t = std::clamp((v - min_v) / (max_v - min_v), 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(t * 127.0f)), 0, 127);
}

float decode_linear(int v, float min_v, float max_v) {
    const float t = std::clamp(v, 0, 127) / 127.0f;
    return min_v + (max_v - min_v) * t;
}

void adjust_linear(float* v, float min_v, float max_v, int delta) {
    if (v == nullptr) {
        return;
    }
    const float step = (max_v - min_v) / 127.0f;
    *v = std::clamp(*v + (step * static_cast<float>(delta)), min_v, max_v);
}

int current_synth_param_display_value(const AppState* state, std::size_t slot, std::size_t idx) {
    if (state == nullptr || slot >= state->slot_drum_params.size()) {
        return 0;
    }
    auto clamp_u7 = [](float v) {
        return std::clamp(static_cast<int>(v), 0, 127);
    };
    const auto& params = state->slot_drum_params[slot];
    switch (idx) {
        case 0: return clamp_u7(params.kick.pitch_start_hz / 2.0f);
        case 1: return clamp_u7(params.kick.env_decay_rate * 4.0f);
        case 2: return clamp_u7(params.hihat.tone_mix * 127.0f);
        case 3: return clamp_u7(params.kick.attack_rate * 16.0f);
        case 4: return clamp_u7(params.kick.pitch_decay_rate * 8.0f);
        case 5: return clamp_u7(params.snare.tone_mix * 127.0f);
        case 6: return clamp_u7(params.snare.amp_decay_rate * 8.0f);
        case 7: return clamp_u7(params.hihat.hp_cutoff_hz / 80.0f);
        case 8: return clamp_u7(params.hihat.hp_resonance * 127.0f);
        case 9: return clamp_u7(params.kick.tone_decay_rate * 10.0f);
        case 10: return clamp_u7(params.tom.pitch_start_hz / 2.0f);
        case 11: return clamp_u7(params.tom.pitch_decay_rate * 10.0f);
        case 12: return clamp_u7(params.clap.tone_mix * 127.0f);
        case 13: return clamp_u7(params.reverb.width * 127.0f);
        case 14:
            if (state->slot_synth_types[slot] == SynthType::Clap) {
                return encode_linear(params.clap.click_rate, 0.25f, 8.0f);
            }
            return clamp_u7(params.tom.duration_s * 255.0f);
        case 15: return clamp_u7(params.reverb.pre_delay_ms * 2.0f);
        case 16: return clamp_u7(params.reverb.decay_time_ms / 15.0f);
        case 17: return clamp_u7(params.reverb.wet_level * 127.0f);
        case 18: return clamp_u7(params.reverb.decay_shape * 127.0f);
        case 19: return clamp_u7(params.snare.noise_mix * 127.0f);
        case 20: return clamp_u7(params.kick.fm.mod_index * 32.0f);
        case 21: return clamp_u7(params.kick.fm.amp_osc_depth * 127.0f);
        case 22: return clamp_u7(params.reverb.early_level * 127.0f);
        case 23: return clamp_u7(params.reverb.diffusion * 127.0f);
        default: return 0;
    }
}

void adjust_synth_param_value(AppState* state, std::size_t slot, std::size_t idx, int delta) {
    if (state == nullptr || slot >= state->slot_drum_params.size()) {
        return;
    }
    auto& params = state->slot_drum_params[slot];
    switch (idx) {
        case 0: adjust_linear(&params.kick.pitch_start_hz, 20.0f, 400.0f, delta); break;
        case 1: adjust_linear(&params.kick.env_decay_rate, 0.0f, 60.0f, delta); break;
        case 2: adjust_linear(&params.hihat.tone_mix, 0.0f, 1.0f, delta); break;
        case 3: adjust_linear(&params.kick.attack_rate, 0.0f, 20.0f, delta); break;
        case 4: adjust_linear(&params.kick.pitch_decay_rate, 0.0f, 80.0f, delta); break;
        case 5: adjust_linear(&params.snare.tone_mix, 0.0f, 1.0f, delta); break;
        case 6: adjust_linear(&params.snare.amp_decay_rate, 0.0f, 80.0f, delta); break;
        case 7: adjust_linear(&params.hihat.hp_cutoff_hz, 40.0f, 12000.0f, delta); break;
        case 8: adjust_linear(&params.hihat.hp_resonance, 0.0f, 1.0f, delta); break;
        case 9: adjust_linear(&params.kick.tone_decay_rate, 0.0f, 80.0f, delta); break;
        case 10: adjust_linear(&params.tom.pitch_start_hz, 20.0f, 400.0f, delta); break;
        case 11: adjust_linear(&params.tom.pitch_decay_rate, 0.0f, 80.0f, delta); break;
        case 12: adjust_linear(&params.clap.tone_mix, 0.0f, 1.0f, delta); break;
        case 13: adjust_linear(&params.reverb.width, 0.0f, 1.0f, delta); break;
        case 14:
            if (state->slot_synth_types[slot] == SynthType::Clap) {
                adjust_linear(&params.clap.click_rate, 0.25f, 8.0f, delta);
            } else {
                adjust_linear(&params.tom.duration_s, 0.04f, 1.0f, delta);
            }
            break;
        case 15: adjust_linear(&params.reverb.pre_delay_ms, 0.0f, 50.0f, delta); break;
        case 16: adjust_linear(&params.reverb.decay_time_ms, 20.0f, 2000.0f, delta); break;
        case 17: adjust_linear(&params.reverb.wet_level, 0.0f, 1.0f, delta); break;
        case 18: adjust_linear(&params.reverb.decay_shape, 0.0f, 1.0f, delta); break;
        case 19: adjust_linear(&params.snare.noise_mix, 0.0f, 1.0f, delta); break;
        case 20: adjust_linear(&params.kick.fm.mod_index, 0.0f, 12.0f, delta); break;
        case 21: adjust_linear(&params.kick.fm.amp_osc_depth, 0.0f, 1.0f, delta); break;
        case 22: adjust_linear(&params.reverb.early_level, 0.0f, 1.0f, delta); break;
        case 23: adjust_linear(&params.reverb.diffusion, 0.0f, 1.0f, delta); break;
        default: break;
    }
}

std::size_t current_param_count(const AppState* state) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return 0;
    }
    if (state->slot_source_kinds[state->selected_slot] == SlotSourceKind::Synth) {
        if (state->slot_synth_types[state->selected_slot] == SynthType::Elements) {
            return kElementsParamLabels.size();
        }
        if (state->slot_synth_types[state->selected_slot] == SynthType::ElementsExact) {
            return kElementsExactParamLabels.size();
        }
    }
    const ParamView view = current_param_view(state);
    return view.count;
}

const char* current_param_label(const AppState* state, std::size_t idx) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return "Param";
    }
    if (state->slot_source_kinds[state->selected_slot] == SlotSourceKind::Synth) {
        if (state->slot_synth_types[state->selected_slot] == SynthType::Elements) {
            const std::size_t i = std::min<std::size_t>(idx, kElementsParamLabels.size() - 1);
            return kElementsParamLabels[i];
        }
        if (state->slot_synth_types[state->selected_slot] == SynthType::ElementsExact) {
            const std::size_t i = std::min<std::size_t>(idx, kElementsExactParamLabels.size() - 1);
            return kElementsExactParamLabels[i];
        }
    }
    const ParamView view = current_param_view(state);
    if (view.count == 0) {
        return "Param";
    }
    return view.entries[std::min<std::size_t>(idx, view.count - 1)].label;
}

int current_param_display_value(const AppState* state, std::size_t idx) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return 0;
    }
    const std::size_t slot = state->selected_slot;
    if (state->slot_source_kinds[slot] != SlotSourceKind::Synth) {
        const auto& s = state->slot_sample_params[slot];
        auto cutoff_to_u7 = [](float hz) {
            const float min_hz = 40.0f;
            const float max_hz = 12000.0f;
            const float c = std::clamp(hz, min_hz, max_hz);
            const float t = (std::log(c) - std::log(min_hz)) / std::log(max_hz / min_hz);
            return std::clamp(static_cast<int>(std::lround(t * 127.0f)), 0, 127);
        };
        switch (idx) {
            case 0: return s.start_pct;
            case 1: return s.end_pct;
            case 2: return s.loop_start_pct;
            case 3: return s.loop_end_pct;
            case 4: return encode_linear(s.loop_increment_pct, -50.0f, 50.0f);
            case 5: return encode_linear(s.tune_semitones, -24.0f, 24.0f);
            case 6: return cutoff_to_u7(s.filter_cutoff_hz);
            case 7: return cutoff_to_u7(s.filter_cutoff_end_hz);
            case 8: return encode_linear(s.filter_env_decay_s, 0.01f, 2.0f);
            case 9: return encode_linear(s.filter_resonance, 0.0f, 2.0f);
            case 10: return encode_linear(s.amp_attack_s, 0.0f, 0.3f);
            case 11: return encode_linear(s.amp_decay_s, 0.0f, 0.6f);
            case 12: return encode_linear(s.amp_sustain, 0.0f, 1.0f);
            case 13: return encode_linear(s.amp_release_s, 0.0f, 0.6f);
            case 14: return std::clamp((std::clamp(s.amp_mode, 0, 2) * 127) / 2, 0, 127);
            case 15: return encode_linear(s.output_gain_db, -24.0f, 24.0f);
            case 16: return encode_linear(s.limiter_ceiling, 0.4f, 1.0f);
            case 17: return std::clamp((std::clamp(s.output_shaper_mode, 0, 2) * 127) / 2, 0, 127);
            case 18: return encode_linear(s.output_saturation, 0.0f, 1.0f);
            case 19: return encode_linear(s.source_rate_hz, 1000.0f, 96000.0f);
            default: return 0;
        }
    }
    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth &&
        state->slot_synth_types[slot] != SynthType::Elements &&
        state->slot_synth_types[slot] != SynthType::ElementsExact) {
        return current_synth_param_display_value(state, slot, idx);
    }
    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth && state->slot_synth_types[slot] == SynthType::Elements) {
        const auto& ep = state->slot_elements_params[slot];
        switch (idx) {
            case 0: return std::clamp(static_cast<int>(ep.model) * 25, 0, 127);
            case 1: return encode_linear(ep.frequency_hz, 20.0f, 1200.0f);
            case 2: return encode_linear(ep.brightness, 0.0f, 1.0f);
            case 3: return encode_linear(ep.damping, 0.0f, 1.0f);
            case 4: return encode_linear(ep.position, 0.0f, 1.0f);
            case 5: return encode_linear(ep.exciter_level, 0.0f, 1.0f);
            case 6: return encode_linear(ep.exciter_noise, 0.0f, 1.0f);
            case 7: return encode_linear(ep.exciter_dur_s, 0.001f, 0.06f);
            case 8: return encode_linear(ep.env_decay_rate, 0.0f, 30.0f);
            case 9: return encode_linear(ep.env_attack_rate, 0.0f, 20.0f);
            default: return 0;
        }
    }
    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth && state->slot_synth_types[slot] == SynthType::ElementsExact) {
        const auto& ex = state->slot_elements_exact_params[slot];
        switch (idx) {
            case 0: return encode_linear(ex.note, 0.0f, 96.0f);
            case 1: return encode_linear(ex.modulation, 0.0f, 1.0f);
            case 2: return encode_linear(ex.strength, 0.0f, 1.0f);
            case 3: return encode_linear(ex.blow_cv, 0.0f, 1.0f);
            case 4: return encode_linear(ex.strike_cv, 0.0f, 1.0f);
            case 5: return encode_linear(ex.exciter_envelope_shape, 0.0f, 1.0f);
            case 6: return encode_linear(ex.exciter_bow_level, 0.0f, 1.0f);
            case 7: return encode_linear(ex.exciter_bow_timbre, 0.0f, 1.0f);
            case 8: return encode_linear(ex.exciter_blow_level, 0.0f, 1.0f);
            case 9: return encode_linear(ex.exciter_blow_meta, 0.0f, 1.0f);
            case 10: return encode_linear(ex.exciter_blow_timbre, 0.0f, 1.0f);
            case 11: return encode_linear(ex.exciter_strike_level, 0.0f, 1.0f);
            case 12: return encode_linear(ex.exciter_strike_meta, 0.0f, 1.0f);
            case 13: return encode_linear(ex.exciter_strike_timbre, 0.0f, 1.0f);
            case 14: return encode_linear(ex.exciter_signature, 0.0f, 1.0f);
            case 15: return encode_linear(ex.resonator_geometry, 0.0f, 1.0f);
            case 16: return encode_linear(ex.resonator_brightness, 0.0f, 1.0f);
            case 17: return encode_linear(ex.resonator_damping, 0.0f, 1.0f);
            case 18: return encode_linear(ex.resonator_position, 0.0f, 1.0f);
            case 19: return encode_linear(ex.resonator_modulation_frequency, 0.0f, 0.01f);
            case 20: return encode_linear(ex.resonator_modulation_offset, 0.0f, 1.0f);
            case 21: return encode_linear(ex.reverb_diffusion, 0.0f, 1.0f);
            case 22: return encode_linear(ex.reverb_lp, 0.0f, 1.0f);
            case 23: return encode_linear(ex.space, 0.0f, 1.0f);
            case 24: return encode_linear(ex.modulation_frequency, 0.0f, 1.0f);
            case 25: return std::clamp(static_cast<int>(ex.resonator_model) * 63, 0, 127);
            case 26: return encode_linear(ex.easter_egg, 0.0f, 1.0f);
            case 27: return std::clamp(static_cast<int>(ex.cv_env1.target) * 9, 0, 127);
            case 28: return encode_linear(ex.cv_env1.amount, -1.0f, 1.0f);
            case 29: return encode_linear(ex.cv_env1.attack_s, 0.0f, 2.0f);
            case 30: return encode_linear(ex.cv_env1.decay_s, 0.0f, 2.0f);
            case 31: return encode_linear(ex.cv_env1.sustain, 0.0f, 1.0f);
            case 32: return encode_linear(ex.cv_env1.release_s, 0.0f, 2.0f);
            case 33: return std::clamp(static_cast<int>(ex.cv_env2.target) * 9, 0, 127);
            case 34: return encode_linear(ex.cv_env2.amount, -1.0f, 1.0f);
            case 35: return encode_linear(ex.cv_env2.attack_s, 0.0f, 2.0f);
            case 36: return encode_linear(ex.cv_env2.decay_s, 0.0f, 2.0f);
            case 37: return encode_linear(ex.cv_env2.sustain, 0.0f, 1.0f);
            case 38: return encode_linear(ex.cv_env2.release_s, 0.0f, 2.0f);
            default: return 0;
        }
    }

    const ParamView view = current_param_view(state);
    if (view.count == 0) {
        return 0;
    }
    const std::size_t i = std::min<std::size_t>(idx, view.count - 1);
    return state->slot_param_values[slot][view.entries[i].value_index];
}

std::string current_param_display_text(const AppState* state, std::size_t idx) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return "0";
    }
    const std::size_t slot = state->selected_slot;
    char buf[32]{};

    if (state->slot_source_kinds[slot] != SlotSourceKind::Synth) {
        const auto& s = state->slot_sample_params[slot];
        switch (idx) {
            case 0: std::snprintf(buf, sizeof(buf), "%d", s.start_pct); break;
            case 1: std::snprintf(buf, sizeof(buf), "%d", s.end_pct); break;
            case 2: std::snprintf(buf, sizeof(buf), "%d", s.loop_start_pct); break;
            case 3: std::snprintf(buf, sizeof(buf), "%d", s.loop_end_pct); break;
            case 4: std::snprintf(buf, sizeof(buf), "%.2f", s.loop_increment_pct); break;
            case 5: std::snprintf(buf, sizeof(buf), "%.2f", s.tune_semitones); break;
            case 6: std::snprintf(buf, sizeof(buf), "%.0f", s.filter_cutoff_hz); break;
            case 7: std::snprintf(buf, sizeof(buf), "%.0f", s.filter_cutoff_end_hz); break;
            case 8: std::snprintf(buf, sizeof(buf), "%.3f", s.filter_env_decay_s); break;
            case 9: std::snprintf(buf, sizeof(buf), "%.3f", s.filter_resonance); break;
            case 10: std::snprintf(buf, sizeof(buf), "%.3f", s.amp_attack_s); break;
            case 11: std::snprintf(buf, sizeof(buf), "%.3f", s.amp_decay_s); break;
            case 12: std::snprintf(buf, sizeof(buf), "%.3f", s.amp_sustain); break;
            case 13: std::snprintf(buf, sizeof(buf), "%.3f", s.amp_release_s); break;
            case 14: std::snprintf(buf, sizeof(buf), "%d", s.amp_mode); break;
            case 15: std::snprintf(buf, sizeof(buf), "%.2f", s.output_gain_db); break;
            case 16: std::snprintf(buf, sizeof(buf), "%.3f", s.limiter_ceiling); break;
            case 17: std::snprintf(buf, sizeof(buf), "%d", s.output_shaper_mode); break;
            case 18: std::snprintf(buf, sizeof(buf), "%.3f", s.output_saturation); break;
            case 19: std::snprintf(buf, sizeof(buf), "%.0f", s.source_rate_hz); break;
            default: std::snprintf(buf, sizeof(buf), "0"); break;
        }
        return std::string(buf);
    }

    if (state->slot_synth_types[slot] != SynthType::Elements &&
        state->slot_synth_types[slot] != SynthType::ElementsExact) {
        const auto& p = state->slot_drum_params[slot];
        switch (idx) {
            case 0: std::snprintf(buf, sizeof(buf), "%.1f", p.kick.pitch_start_hz); break;
            case 1: std::snprintf(buf, sizeof(buf), "%.2f", p.kick.env_decay_rate); break;
            case 2: std::snprintf(buf, sizeof(buf), "%.3f", p.hihat.tone_mix); break;
            case 3: std::snprintf(buf, sizeof(buf), "%.2f", p.kick.attack_rate); break;
            case 4: std::snprintf(buf, sizeof(buf), "%.2f", p.kick.pitch_decay_rate); break;
            case 5: std::snprintf(buf, sizeof(buf), "%.3f", p.snare.tone_mix); break;
            case 6: std::snprintf(buf, sizeof(buf), "%.2f", p.snare.amp_decay_rate); break;
            case 7: std::snprintf(buf, sizeof(buf), "%.1f", p.hihat.hp_cutoff_hz); break;
            case 8: std::snprintf(buf, sizeof(buf), "%.3f", p.hihat.hp_resonance); break;
            case 9: std::snprintf(buf, sizeof(buf), "%.2f", p.kick.tone_decay_rate); break;
            case 10: std::snprintf(buf, sizeof(buf), "%.1f", p.tom.pitch_start_hz); break;
            case 11: std::snprintf(buf, sizeof(buf), "%.2f", p.tom.pitch_decay_rate); break;
            case 12: std::snprintf(buf, sizeof(buf), "%.3f", p.clap.tone_mix); break;
            case 13: std::snprintf(buf, sizeof(buf), "%.3f", p.reverb.width); break;
            case 14:
                if (state->slot_synth_types[slot] == SynthType::Clap) {
                    std::snprintf(buf, sizeof(buf), "%.3f", p.clap.click_rate);
                } else {
                    std::snprintf(buf, sizeof(buf), "%.3f", p.tom.duration_s);
                }
                break;
            case 15: std::snprintf(buf, sizeof(buf), "%.2f", p.reverb.pre_delay_ms); break;
            case 16: std::snprintf(buf, sizeof(buf), "%.1f", p.reverb.decay_time_ms); break;
            case 17: std::snprintf(buf, sizeof(buf), "%.3f", p.reverb.wet_level); break;
            case 18: std::snprintf(buf, sizeof(buf), "%.3f", p.reverb.decay_shape); break;
            case 19: std::snprintf(buf, sizeof(buf), "%.3f", p.snare.noise_mix); break;
            case 20: std::snprintf(buf, sizeof(buf), "%.3f", p.kick.fm.mod_index); break;
            case 21: std::snprintf(buf, sizeof(buf), "%.3f", p.kick.fm.amp_osc_depth); break;
            case 22: std::snprintf(buf, sizeof(buf), "%.3f", p.reverb.early_level); break;
            case 23: std::snprintf(buf, sizeof(buf), "%.3f", p.reverb.diffusion); break;
            default: std::snprintf(buf, sizeof(buf), "0"); break;
        }
        return std::string(buf);
    }

    std::snprintf(buf, sizeof(buf), "%03d", current_param_display_value(state, idx));
    return std::string(buf);
}

void adjust_current_param_value(AppState* state, int delta) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return;
    }
    const std::size_t slot = state->selected_slot;
    const std::size_t idx = state->selected_param;

    push_undo_snapshot(state);

    if (state->slot_source_kinds[slot] != SlotSourceKind::Synth) {
        auto& s = state->slot_sample_params[slot];
        switch (idx) {
            case 0: s.start_pct = std::clamp(s.start_pct + delta, 0, 99); break;
            case 1: s.end_pct = std::clamp(s.end_pct + delta, 1, 100); break;
            case 2: s.loop_start_pct = std::clamp(s.loop_start_pct + delta, 0, 99); break;
            case 3: s.loop_end_pct = std::clamp(s.loop_end_pct + delta, 1, 100); break;
            case 4: adjust_linear(&s.loop_increment_pct, -50.0f, 50.0f, delta); break;
            case 5: adjust_linear(&s.tune_semitones, -24.0f, 24.0f, delta); break;
            case 6: adjust_linear(&s.filter_cutoff_hz, 40.0f, 12000.0f, delta); break;
            case 7: adjust_linear(&s.filter_cutoff_end_hz, 40.0f, 12000.0f, delta); break;
            case 8: adjust_linear(&s.filter_env_decay_s, 0.01f, 2.0f, delta); break;
            case 9: adjust_linear(&s.filter_resonance, 0.0f, 2.0f, delta); break;
            case 10: adjust_linear(&s.amp_attack_s, 0.0f, 0.3f, delta); break;
            case 11: adjust_linear(&s.amp_decay_s, 0.0f, 0.6f, delta); break;
            case 12: adjust_linear(&s.amp_sustain, 0.0f, 1.0f, delta); break;
            case 13: adjust_linear(&s.amp_release_s, 0.0f, 0.6f, delta); break;
            case 14: s.amp_mode = std::clamp(s.amp_mode + ((delta > 0) ? 1 : -1), 0, 2); break;
            case 15: adjust_linear(&s.output_gain_db, -24.0f, 24.0f, delta); break;
            case 16: adjust_linear(&s.limiter_ceiling, 0.4f, 1.0f, delta); break;
            case 17: s.output_shaper_mode = std::clamp(s.output_shaper_mode + ((delta > 0) ? 1 : -1), 0, 2); break;
            case 18: adjust_linear(&s.output_saturation, 0.0f, 1.0f, delta); break;
            case 19: adjust_linear(&s.source_rate_hz, 1000.0f, 96000.0f, delta); break;
            default: break;
        }
        normalize_sample_slot_params(state, slot);
        return;
    }

    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth &&
        state->slot_synth_types[slot] != SynthType::Elements &&
        state->slot_synth_types[slot] != SynthType::ElementsExact) {
        adjust_synth_param_value(state, slot, idx, delta);
        apply_drum_params_to_slot_values(state, slot, state->slot_drum_params[slot]);
        return;
    }

    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth && state->slot_synth_types[slot] == SynthType::Elements) {
        auto& ep = state->slot_elements_params[slot];
        switch (idx) {
            case 0: {
                int v = std::clamp(static_cast<int>(ep.model) + ((delta > 0) ? 1 : -1), 0, 5);
                ep.model = static_cast<drumrom::synth::ElementsModel>(v);
                break;
            }
            case 1: adjust_linear(&ep.frequency_hz, 20.0f, 1200.0f, delta); break;
            case 2: adjust_linear(&ep.brightness, 0.0f, 1.0f, delta); break;
            case 3: adjust_linear(&ep.damping, 0.0f, 1.0f, delta); break;
            case 4: adjust_linear(&ep.position, 0.0f, 1.0f, delta); break;
            case 5: adjust_linear(&ep.exciter_level, 0.0f, 1.0f, delta); break;
            case 6: adjust_linear(&ep.exciter_noise, 0.0f, 1.0f, delta); break;
            case 7: adjust_linear(&ep.exciter_dur_s, 0.001f, 0.06f, delta); break;
            case 8: adjust_linear(&ep.env_decay_rate, 0.0f, 30.0f, delta); break;
            case 9: adjust_linear(&ep.env_attack_rate, 0.0f, 20.0f, delta); break;
            default: break;
        }
        return;
    }

    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth && state->slot_synth_types[slot] == SynthType::ElementsExact) {
        auto& ex = state->slot_elements_exact_params[slot];
        auto adjust_enum = [&](int current, int min_v, int max_v) {
            return std::clamp(current + ((delta > 0) ? 1 : -1), min_v, max_v);
        };
        switch (idx) {
            case 0: adjust_linear(&ex.note, 0.0f, 96.0f, delta); break;
            case 1: adjust_linear(&ex.modulation, 0.0f, 1.0f, delta); break;
            case 2: adjust_linear(&ex.strength, 0.0f, 1.0f, delta); break;
            case 3: adjust_linear(&ex.blow_cv, 0.0f, 1.0f, delta); break;
            case 4: adjust_linear(&ex.strike_cv, 0.0f, 1.0f, delta); break;
            case 5: adjust_linear(&ex.exciter_envelope_shape, 0.0f, 1.0f, delta); break;
            case 6: adjust_linear(&ex.exciter_bow_level, 0.0f, 1.0f, delta); break;
            case 7: adjust_linear(&ex.exciter_bow_timbre, 0.0f, 1.0f, delta); break;
            case 8: adjust_linear(&ex.exciter_blow_level, 0.0f, 1.0f, delta); break;
            case 9: adjust_linear(&ex.exciter_blow_meta, 0.0f, 1.0f, delta); break;
            case 10: adjust_linear(&ex.exciter_blow_timbre, 0.0f, 1.0f, delta); break;
            case 11: adjust_linear(&ex.exciter_strike_level, 0.0f, 1.0f, delta); break;
            case 12: adjust_linear(&ex.exciter_strike_meta, 0.0f, 1.0f, delta); break;
            case 13: adjust_linear(&ex.exciter_strike_timbre, 0.0f, 1.0f, delta); break;
            case 14: adjust_linear(&ex.exciter_signature, 0.0f, 1.0f, delta); break;
            case 15: adjust_linear(&ex.resonator_geometry, 0.0f, 1.0f, delta); break;
            case 16: adjust_linear(&ex.resonator_brightness, 0.0f, 1.0f, delta); break;
            case 17: adjust_linear(&ex.resonator_damping, 0.0f, 1.0f, delta); break;
            case 18: adjust_linear(&ex.resonator_position, 0.0f, 1.0f, delta); break;
            case 19: adjust_linear(&ex.resonator_modulation_frequency, 0.0f, 0.01f, delta); break;
            case 20: adjust_linear(&ex.resonator_modulation_offset, 0.0f, 1.0f, delta); break;
            case 21: adjust_linear(&ex.reverb_diffusion, 0.0f, 1.0f, delta); break;
            case 22: adjust_linear(&ex.reverb_lp, 0.0f, 1.0f, delta); break;
            case 23: adjust_linear(&ex.space, 0.0f, 1.0f, delta); break;
            case 24: adjust_linear(&ex.modulation_frequency, 0.0f, 1.0f, delta); break;
            case 25: ex.resonator_model = static_cast<drumrom::synth::ElementsExactResonatorModel>(adjust_enum(static_cast<int>(ex.resonator_model), 0, 2)); break;
            case 26: adjust_linear(&ex.easter_egg, 0.0f, 1.0f, delta); break;
            case 27: ex.cv_env1.target = static_cast<drumrom::synth::ElementsExactCvTarget>(adjust_enum(static_cast<int>(ex.cv_env1.target), 0, 13)); break;
            case 28: adjust_linear(&ex.cv_env1.amount, -1.0f, 1.0f, delta); break;
            case 29: adjust_linear(&ex.cv_env1.attack_s, 0.0f, 2.0f, delta); break;
            case 30: adjust_linear(&ex.cv_env1.decay_s, 0.0f, 2.0f, delta); break;
            case 31: adjust_linear(&ex.cv_env1.sustain, 0.0f, 1.0f, delta); break;
            case 32: adjust_linear(&ex.cv_env1.release_s, 0.0f, 2.0f, delta); break;
            case 33: ex.cv_env2.target = static_cast<drumrom::synth::ElementsExactCvTarget>(adjust_enum(static_cast<int>(ex.cv_env2.target), 0, 13)); break;
            case 34: adjust_linear(&ex.cv_env2.amount, -1.0f, 1.0f, delta); break;
            case 35: adjust_linear(&ex.cv_env2.attack_s, 0.0f, 2.0f, delta); break;
            case 36: adjust_linear(&ex.cv_env2.decay_s, 0.0f, 2.0f, delta); break;
            case 37: adjust_linear(&ex.cv_env2.sustain, 0.0f, 1.0f, delta); break;
            case 38: adjust_linear(&ex.cv_env2.release_s, 0.0f, 2.0f, delta); break;
            default: break;
        }
        return;
    }

    const ParamView view = current_param_view(state);
    if (view.count == 0) {
        return;
    }
    const std::size_t i = std::min<std::size_t>(idx, view.count - 1);
    int& value = state->slot_param_values[slot][view.entries[i].value_index];
    value = std::clamp(value + delta, 0, 127);
    normalize_sample_slot_params(state, slot);
}

int selected_param_value_index(const AppState* state) {
    const ParamView view = current_param_view(state);
    if (view.count == 0 || state == nullptr) {
        return 0;
    }
    const std::size_t idx = std::clamp<std::size_t>(state->selected_param, 0, view.count - 1);
    return view.entries[idx].value_index;
}

void clamp_selected_param_to_view(AppState* state) {
    if (state == nullptr) {
        return;
    }
    const std::size_t count = current_param_count(state);
    if (count == 0) {
        state->selected_param = 0;
        return;
    }
    state->selected_param = std::clamp<std::size_t>(state->selected_param, 0, count - 1);
}

constexpr int kParamGridColumns = 4;
constexpr std::uint32_t kPresetAllMagic = 0x525A3141U;  // RZ1A
constexpr std::uint32_t kPresetVersion = 10;
constexpr std::size_t kLegacyPresetDrumParamsV2Size = 588u;
constexpr std::size_t kLegacyPresetDrumParamsV7Size = 600u;
constexpr std::size_t kLegacyPresetDrumParamsV8Size = sizeof(drumrom::synth::DrumParams) - (7u * sizeof(float));

bool read_legacy_drum_params(std::istream& in, std::uint32_t version, drumrom::synth::DrumParams& params) {
    auto bytes_after_path_for_version = [&](std::uint32_t ver) -> std::size_t {
        std::size_t n = 0;
        n += sizeof(std::uint32_t);  // source_rate_hz (legacy)
        n += sizeof(float);          // start_pct
        n += sizeof(float);          // end_pct
        if (ver >= 6) {
            n += sizeof(float) * 3;  // loop start/end/increment
        }
        n += sizeof(float) * 9;      // tune/filter/filter_end/filter_decay/resonance/amp ADSR
        if (ver >= 4) {
            n += sizeof(float) * 2;  // output gain + limiter
        }
        if (ver >= 5) {
            n += sizeof(float) * 2;  // shaper mode + saturation (legacy float payload)
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
            in.seekg(after_path_len + static_cast<std::streamoff>(sample_path_len));
            float source_rate_hz = 0.0f;
            float start_pct = 0.0f;
            float end_pct = 0.0f;
            in.read(reinterpret_cast<char*>(&source_rate_hz), sizeof(source_rate_hz));
            in.read(reinterpret_cast<char*>(&start_pct), sizeof(start_pct));
            in.read(reinterpret_cast<char*>(&end_pct), sizeof(end_pct));
            if (!in || !std::isfinite(source_rate_hz) || source_rate_hz < 1000.0f || source_rate_hz > 500000.0f ||
                start_pct < -1.0f || start_pct > 1000.0f || end_pct < -1.0f || end_pct > 1000.0f) {
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

        params = drumrom::synth::DrumParams{};
        std::memcpy(&params, legacy_params.data(), legacy_size);
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
        return try_read_with_size(kLegacyPresetDrumParamsV8Size) ||
               try_read_with_size(kLegacyPresetDrumParamsV7Size) ||
               try_read_with_size(kLegacyPresetDrumParamsV2Size);
    }
    if (version >= 4) {
        return try_read_with_size(kLegacyPresetDrumParamsV7Size);
    }
    return try_read_with_size(kLegacyPresetDrumParamsV2Size);
}

std::size_t map_legacy_slot_index(std::uint32_t slot_index, std::uint32_t count) {
    if (count == 10) {
        switch (slot_index) {
            case 0: return 0;   // tom1
            case 1: return 8;   // tom2
            case 2: return 1;   // tom3
            case 3: return 9;   // kick
            case 4: return 10;  // snare
            case 5: return 2;   // rimshot
            case 6: return 11;  // closed_hihat
            case 7: return 3;   // open_hihat
            default: return static_cast<std::size_t>(-1);
        }
    }
    if (slot_index >= 16u) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(slot_index);
}

void init_slot_type_defaults(AppState* state) {
    if (state == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < state->slot_source_kinds.size(); ++i) {
        state->slot_source_kinds[i] = SlotSourceKind::Sample;
        state->slot_synth_types[i] = SynthType::Tom;
        state->slot_sample_paths[i] = std::string("samples/") + kSlotFileNames[i] + ".raw";
        state->slot_capacities[i] = kDefaultSlotCapacities[i];
        state->slot_drum_params[i] = drumrom::synth::DrumParams{};
        state->slot_sample_params[i] = SampleParams{};
        state->slot_elements_params[i] = drumrom::synth::ElementsParams{};
        state->slot_elements_exact_params[i] = drumrom::synth::ElementsExactParams{};
        sync_slot_values_from_sample_params(state, i);
    }
    state->default_kit_loaded = false;
    state->default_kit_loaded_slots = 0;
    state->waveform_dirty = true;
}

void load_slot_capacities_from_map_file(const std::filesystem::path& map_path,
                                        std::unordered_map<std::string, std::size_t>* sizes_out) {
    if (sizes_out == nullptr) {
        return;
    }
    std::ifstream in(map_path);
    if (!in) {
        return;
    }

    const std::regex slot_line_re("\\\"name\\\"\\s*:\\s*\\\"([^\\\"]+)\\\".*\\\"start\\\"\\s*:\\s*(\\d+).*\\\"end\\\"\\s*:\\s*(\\d+)");
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, slot_line_re)) {
            continue;
        }
        if (m.size() < 4) {
            continue;
        }
        const std::string name = m[1].str();
        const std::size_t start = static_cast<std::size_t>(std::stoul(m[2].str()));
        const std::size_t end = static_cast<std::size_t>(std::stoul(m[3].str()));
        if (end >= start) {
            (*sizes_out)[name] = (end - start) + 1;
        }
    }
}

void load_slot_capacities_from_configs(AppState* state) {
    if (state == nullptr) {
        return;
    }

    std::unordered_map<std::string, std::size_t> sizes;
    load_slot_capacities_from_map_file(std::filesystem::path("configs") / "rz1_rom_a_map.json", &sizes);
    load_slot_capacities_from_map_file(std::filesystem::path("configs") / "rz1_rom_b_map.json", &sizes);

    for (std::size_t i = 0; i < state->slot_capacities.size(); ++i) {
        const auto it = sizes.find(kSlotFileNames[i]);
        if (it != sizes.end() && it->second > 0) {
            state->slot_capacities[i] = it->second;
        }
    }
}

void apply_drum_params_to_slot_values(AppState* state, std::size_t slot, const drumrom::synth::DrumParams& params) {
    if (state == nullptr || slot >= state->slot_param_values.size()) {
        return;
    }
    state->slot_drum_params[slot] = params;

    auto clamp_u7 = [](float v) {
        return std::clamp(static_cast<int>(v), 0, 127);
    };

    auto& out = state->slot_param_values[slot];
    out[0] = clamp_u7(params.kick.pitch_start_hz / 2.0f);
    out[1] = clamp_u7(params.kick.env_decay_rate * 4.0f);
    out[2] = clamp_u7(params.hihat.tone_mix * 127.0f);
    out[3] = clamp_u7(params.kick.attack_rate * 16.0f);
    out[4] = clamp_u7(params.kick.pitch_decay_rate * 8.0f);
    out[5] = clamp_u7(params.snare.tone_mix * 127.0f);
    out[6] = clamp_u7(params.snare.amp_decay_rate * 8.0f);
    out[7] = clamp_u7(params.hihat.hp_cutoff_hz / 80.0f);
    out[8] = clamp_u7(params.hihat.hp_resonance * 127.0f);
    out[9] = clamp_u7(params.kick.tone_decay_rate * 10.0f);
    out[10] = clamp_u7(params.tom.pitch_start_hz / 2.0f);
    out[11] = clamp_u7(params.tom.pitch_decay_rate * 10.0f);
    out[12] = clamp_u7(params.clap.tone_mix * 127.0f);
    out[13] = clamp_u7(params.reverb.width * 127.0f);
    if (slot < state->slot_synth_types.size() && state->slot_synth_types[slot] == SynthType::Clap) {
        const float t = std::clamp((params.clap.click_rate - 0.25f) / (8.0f - 0.25f), 0.0f, 1.0f);
        out[14] = clamp_u7(std::lround(t * 127.0f));
    } else {
        out[14] = clamp_u7(params.tom.duration_s * 255.0f);
    }
    out[15] = clamp_u7(params.reverb.pre_delay_ms * 2.0f);
    out[16] = clamp_u7(params.reverb.decay_time_ms / 15.0f);
    out[17] = clamp_u7(params.reverb.wet_level * 127.0f);
    out[18] = clamp_u7(params.reverb.decay_shape * 127.0f);
    out[19] = clamp_u7(params.snare.noise_mix * 127.0f);
    out[20] = clamp_u7(params.kick.fm.mod_index * 32.0f);
    out[21] = clamp_u7(params.kick.fm.amp_osc_depth * 127.0f);
    out[22] = clamp_u7(params.reverb.early_level * 127.0f);
    out[23] = clamp_u7(params.reverb.diffusion * 127.0f);
}

std::filesystem::path resolved_slot_sample_path(const AppState* state, std::size_t slot) {
    if (state == nullptr || slot >= state->slot_sample_paths.size() || slot >= kSlotFileNames.size()) {
        return {};
    }

    std::filesystem::path sample_path = state->slot_sample_paths[slot];
    if (!sample_path.empty()) {
        if (sample_path.is_absolute() && std::filesystem::exists(sample_path)) {
            return sample_path;
        }
        if (std::filesystem::exists(sample_path)) {
            return sample_path;
        }
        const std::filesystem::path under_samples = std::filesystem::path("samples") / sample_path;
        if (std::filesystem::exists(under_samples)) {
            return under_samples;
        }
    }

    const std::filesystem::path fallback = std::filesystem::path("samples") / (std::string(kSlotFileNames[slot]) + ".raw");
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }
    return {};
}

bool build_synth_preview_audio(const AppState* state, std::size_t slot, std::vector<std::int16_t>* out_pcm16);
bool build_sample_preview_audio(const AppState* state, std::size_t slot, std::vector<std::int16_t>* out_pcm16);
void normalize_sample_slot_params(AppState* state, std::size_t slot);

float u7_to_linear(int value, float min_value, float max_value) {
    const float t = std::clamp(value / 127.0f, 0.0f, 1.0f);
    return min_value + (max_value - min_value) * t;
}

int linear_to_u7(float value, float min_value, float max_value) {
    if (max_value <= min_value) {
        return 0;
    }
    const float t = std::clamp((value - min_value) / (max_value - min_value), 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(t * 127.0f)), 0, 127);
}

float u7_to_signed(int value, float max_abs) {
    const float centered = (std::clamp(value, 0, 127) - 64) / 63.0f;
    return std::clamp(centered * max_abs, -max_abs, max_abs);
}

int signed_to_u7(float value, float max_abs) {
    if (max_abs <= 0.0f) {
        return 64;
    }
    const float t = std::clamp((value / max_abs), -1.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround((t * 63.0f) + 64.0f)), 0, 127);
}

float u7_to_cutoff_hz(int value) {
    const float min_hz = 40.0f;
    const float max_hz = 12000.0f;
    const float t = std::clamp(value / 127.0f, 0.0f, 1.0f);
    const float ln_hz = std::log(min_hz) + (std::log(max_hz / min_hz) * t);
    return std::exp(ln_hz);
}

int cutoff_hz_to_u7(float hz) {
    const float min_hz = 40.0f;
    const float max_hz = 12000.0f;
    const float c = std::clamp(hz, min_hz, max_hz);
    const float t = (std::log(c) - std::log(min_hz)) / std::log(max_hz / min_hz);
    return std::clamp(static_cast<int>(std::lround(t * 127.0f)), 0, 127);
}

void normalize_percent_window(int* start_pct, int* end_pct) {
    if (start_pct == nullptr || end_pct == nullptr) {
        return;
    }
    const int s = std::clamp(*start_pct, 0, 99);
    const int e = std::clamp(*end_pct, s + 1, 100);
    *start_pct = s;
    *end_pct = e;
}

void normalize_sample_slot_params(AppState* state, std::size_t slot) {
    if (state == nullptr || slot >= state->slot_sample_params.size() || slot >= state->slot_source_kinds.size()) {
        return;
    }
    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth) {
        return;
    }

    auto& s = state->slot_sample_params[slot];
    normalize_percent_window(&s.start_pct, &s.end_pct);
    normalize_percent_window(&s.loop_start_pct, &s.loop_end_pct);
    s.amp_mode = std::clamp(s.amp_mode, 0, 2);
    s.output_shaper_mode = std::clamp(s.output_shaper_mode, 0, 2);
    s.loop_increment_pct = std::clamp(s.loop_increment_pct, -50.0f, 50.0f);
    s.tune_semitones = std::clamp(s.tune_semitones, -24.0f, 24.0f);
    s.filter_cutoff_hz = std::clamp(s.filter_cutoff_hz, 40.0f, 12000.0f);
    s.filter_cutoff_end_hz = std::clamp(s.filter_cutoff_end_hz, 40.0f, 12000.0f);
    s.filter_env_decay_s = std::clamp(s.filter_env_decay_s, 0.01f, 2.0f);
    s.filter_resonance = std::clamp(s.filter_resonance, 0.0f, 2.0f);
    s.amp_attack_s = std::clamp(s.amp_attack_s, 0.0f, 0.3f);
    s.amp_decay_s = std::clamp(s.amp_decay_s, 0.0f, 0.6f);
    s.amp_sustain = std::clamp(s.amp_sustain, 0.0f, 1.0f);
    s.amp_release_s = std::clamp(s.amp_release_s, 0.0f, 0.6f);
    s.output_gain_db = std::clamp(s.output_gain_db, -24.0f, 24.0f);
    s.limiter_ceiling = std::clamp(s.limiter_ceiling, 0.4f, 1.0f);
    s.output_saturation = std::clamp(s.output_saturation, 0.0f, 1.0f);
    s.source_rate_hz = std::clamp(s.source_rate_hz, 1000.0f, 96000.0f);

    sync_slot_values_from_sample_params(state, slot);
}

void sync_slot_values_from_sample_params(AppState* state, std::size_t slot) {
    if (state == nullptr || slot >= state->slot_sample_params.size() || slot >= state->slot_param_values.size()) {
        return;
    }
    const auto& s = state->slot_sample_params[slot];
    auto& p = state->slot_param_values[slot];
    p[0] = s.start_pct;
    p[1] = s.end_pct;
    p[2] = s.loop_start_pct;
    p[3] = s.loop_end_pct;
    p[4] = signed_to_u7(s.loop_increment_pct, 50.0f);
    p[5] = signed_to_u7(s.tune_semitones, 24.0f);
    p[6] = cutoff_hz_to_u7(s.filter_cutoff_hz);
    p[7] = cutoff_hz_to_u7(s.filter_cutoff_end_hz);
    p[8] = linear_to_u7(s.filter_env_decay_s, 0.01f, 2.0f);
    p[9] = linear_to_u7(s.filter_resonance, 0.0f, 2.0f);
    p[10] = linear_to_u7(s.amp_attack_s, 0.0f, 0.3f);
    p[11] = linear_to_u7(s.amp_decay_s, 0.0f, 0.6f);
    p[12] = linear_to_u7(s.amp_sustain, 0.0f, 1.0f);
    p[13] = linear_to_u7(s.amp_release_s, 0.0f, 0.6f);
    p[14] = std::clamp((std::clamp(s.amp_mode, 0, 2) * 127) / 2, 0, 127);
    p[15] = signed_to_u7(s.output_gain_db, 24.0f);
    p[16] = linear_to_u7(s.limiter_ceiling, 0.4f, 1.0f);
    p[17] = std::clamp((std::clamp(s.output_shaper_mode, 0, 2) * 127) / 2, 0, 127);
    p[18] = linear_to_u7(s.output_saturation, 0.0f, 1.0f);
    p[19] = linear_to_u7(s.source_rate_hz, 1000.0f, 96000.0f);
}

void sync_sample_params_from_slot_values(AppState* state, std::size_t slot) {
    if (state == nullptr || slot >= state->slot_sample_params.size() || slot >= state->slot_param_values.size()) {
        return;
    }
    const auto& p = state->slot_param_values[slot];
    auto& s = state->slot_sample_params[slot];
    s.start_pct = std::clamp(p[0], 0, 99);
    s.end_pct = std::clamp(p[1], s.start_pct + 1, 100);
    s.loop_start_pct = std::clamp(p[2], 0, 99);
    s.loop_end_pct = std::clamp(p[3], s.loop_start_pct + 1, 100);
    s.loop_increment_pct = u7_to_signed(p[4], 50.0f);
    s.tune_semitones = u7_to_signed(p[5], 24.0f);
    s.filter_cutoff_hz = u7_to_cutoff_hz(p[6]);
    s.filter_cutoff_end_hz = u7_to_cutoff_hz(p[7]);
    s.filter_env_decay_s = u7_to_linear(p[8], 0.01f, 2.0f);
    s.filter_resonance = u7_to_linear(p[9], 0.0f, 2.0f);
    s.amp_attack_s = u7_to_linear(p[10], 0.0f, 0.3f);
    s.amp_decay_s = u7_to_linear(p[11], 0.0f, 0.6f);
    s.amp_sustain = u7_to_linear(p[12], 0.0f, 1.0f);
    s.amp_release_s = u7_to_linear(p[13], 0.0f, 0.6f);
    s.amp_mode = (p[14] < 42) ? 0 : ((p[14] < 84) ? 1 : 2);
    s.output_gain_db = u7_to_signed(p[15], 24.0f);
    s.limiter_ceiling = u7_to_linear(p[16], 0.4f, 1.0f);
    s.output_shaper_mode = (p[17] < 42) ? 0 : ((p[17] < 84) ? 1 : 2);
    s.output_saturation = u7_to_linear(p[18], 0.0f, 1.0f);
    s.source_rate_hz = u7_to_linear(p[19], 1000.0f, 96000.0f);
    normalize_sample_slot_params(state, slot);
}

void apply_sample_settings_to_slot_values(AppState* state,
                                          std::size_t slot,
                                          int sample_start_pct,
                                          int sample_end_pct,
                                          int sample_loop_start_pct,
                                          int sample_loop_end_pct,
                                          float sample_loop_increment_pct,
                                          float sample_tune_semitones,
                                          float sample_filter_cutoff_hz,
                                          float sample_filter_cutoff_end_hz,
                                          float sample_filter_env_decay_s,
                                          float sample_filter_resonance,
                                          float sample_amp_attack_s,
                                          float sample_amp_decay_s,
                                          float sample_amp_sustain,
                                          float sample_amp_release_s,
                                          float sample_source_rate_hz,
                                          float output_gain_db,
                                          float limiter_ceiling,
                                          int output_shaper_mode,
                                          float output_saturation,
                                          std::uint32_t amp_env_mode) {
    if (state == nullptr || slot >= state->slot_param_values.size()) {
        return;
    }

    auto& s = state->slot_sample_params[slot];
    s.start_pct = sample_start_pct;
    s.end_pct = sample_end_pct;
    s.loop_start_pct = sample_loop_start_pct;
    s.loop_end_pct = sample_loop_end_pct;
    s.loop_increment_pct = sample_loop_increment_pct;
    s.tune_semitones = sample_tune_semitones;
    s.filter_cutoff_hz = sample_filter_cutoff_hz;
    s.filter_cutoff_end_hz = sample_filter_cutoff_end_hz;
    s.filter_env_decay_s = sample_filter_env_decay_s;
    s.filter_resonance = sample_filter_resonance;
    s.amp_attack_s = sample_amp_attack_s;
    s.amp_decay_s = sample_amp_decay_s;
    s.amp_sustain = sample_amp_sustain;
    s.amp_release_s = sample_amp_release_s;
    s.source_rate_hz = sample_source_rate_hz;
    s.output_gain_db = output_gain_db;
    s.limiter_ceiling = limiter_ceiling;
    s.output_shaper_mode = output_shaper_mode;
    s.output_saturation = output_saturation;
    s.amp_mode = static_cast<int>(std::clamp<std::uint32_t>(amp_env_mode, 0u, 2u));
    normalize_sample_slot_params(state, slot);
}

void load_slot_waveform_from_file(AppState* state, std::size_t slot) {
    if (state == nullptr || slot >= state->slot_sample_paths.size()) {
        return;
    }

    state->selected_waveform.clear();
    if (slot < state->slot_source_kinds.size() && state->slot_source_kinds[slot] == SlotSourceKind::Synth) {
        std::vector<std::int16_t> synth_pcm16;
        if (build_synth_preview_audio(state, slot, &synth_pcm16) && !synth_pcm16.empty()) {
            const std::size_t target_points = 512;
            const std::size_t step = std::max<std::size_t>(1, synth_pcm16.size() / target_points);
            state->selected_waveform.reserve((synth_pcm16.size() + step - 1) / step);
            for (std::size_t i = 0; i < synth_pcm16.size(); i += step) {
                const float v = static_cast<float>(synth_pcm16[i]) / 32768.0f;
                state->selected_waveform.push_back(std::clamp(v, -1.0f, 1.0f));
            }
        }
        state->selected_waveform_slot = slot;
        state->waveform_dirty = false;
        return;
    }

    std::vector<std::int16_t> sample_pcm16;
    if (build_sample_preview_audio(state, slot, &sample_pcm16) && !sample_pcm16.empty()) {
        const std::size_t target_points = 512;
        const std::size_t step = std::max<std::size_t>(1, sample_pcm16.size() / target_points);
        state->selected_waveform.reserve((sample_pcm16.size() + step - 1) / step);
        for (std::size_t i = 0; i < sample_pcm16.size(); i += step) {
            const float v = static_cast<float>(sample_pcm16[i]) / 32768.0f;
            state->selected_waveform.push_back(std::clamp(v, -1.0f, 1.0f));
        }
        state->selected_waveform_slot = slot;
        state->waveform_dirty = false;
        return;
    }

    const std::filesystem::path sample_path = resolved_slot_sample_path(state, slot);
    std::ifstream in(sample_path, std::ios::binary);
    if (!in) {
        state->selected_waveform_slot = slot;
        state->waveform_dirty = false;
        return;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        state->selected_waveform_slot = slot;
        state->waveform_dirty = false;
        return;
    }

    const std::size_t target_size = (slot < state->slot_capacities.size()) ? state->slot_capacities[slot] : bytes.size();
    if (target_size > 0) {
        if (bytes.size() > target_size) {
            bytes.resize(target_size);
        } else if (bytes.size() < target_size) {
            bytes.resize(target_size, 128u);
        }
    }

    const std::size_t target_points = 512;
    const std::size_t step = std::max<std::size_t>(1, bytes.size() / target_points);
    state->selected_waveform.reserve((bytes.size() + step - 1) / step);
    for (std::size_t i = 0; i < bytes.size(); i += step) {
        const float v = static_cast<float>(static_cast<std::int8_t>(bytes[i])) / 128.0f;
        state->selected_waveform.push_back(v);
    }

    state->selected_waveform_slot = slot;
    state->waveform_dirty = false;
}

bool load_slot_audio_for_preview(const AppState* state, std::size_t slot, std::vector<std::int8_t>* out_audio) {
    if (state == nullptr || out_audio == nullptr || slot >= state->slot_sample_paths.size()) {
        return false;
    }

    const std::filesystem::path sample_path = resolved_slot_sample_path(state, slot);
    std::ifstream in(sample_path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return false;
    }

    const std::size_t target_size = (slot < state->slot_capacities.size()) ? state->slot_capacities[slot] : bytes.size();
    if (target_size > 0) {
        if (bytes.size() > target_size) {
            bytes.resize(target_size);
        } else if (bytes.size() < target_size) {
            bytes.resize(target_size, 128u);
        }
    }

    out_audio->resize(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out_audio->at(i) = static_cast<std::int8_t>(bytes[i]);
    }
    return true;
}

bool build_synth_preview_audio(const AppState* state, std::size_t slot, std::vector<std::int16_t>* out_pcm16) {
    if (state == nullptr || out_pcm16 == nullptr || slot >= state->slot_source_kinds.size()) {
        return false;
    }
    if (state->slot_source_kinds[slot] != SlotSourceKind::Synth) {
        return false;
    }

    const std::size_t num_samples = (slot < state->slot_capacities.size() && state->slot_capacities[slot] > 0)
        ? state->slot_capacities[slot]
        : 4096u;
    std::mt19937 rng(static_cast<std::uint32_t>(0xA17E3u + (slot * 131u)));

    std::vector<std::uint8_t> rendered;
    if (state->slot_synth_types[slot] == SynthType::Elements) {
        rendered = drumrom::synth::synthesize_elements(20833, num_samples, state->slot_elements_params[slot], rng);
    } else if (state->slot_synth_types[slot] == SynthType::ElementsExact) {
        rendered = drumrom::synth::synthesize_elements_exact(20833, num_samples, state->slot_elements_exact_params[slot], rng);
    } else {
        drumrom::synth::DrumParams params = state->slot_drum_params[slot];
        params.sample_rate = 20833;
        switch (state->slot_synth_types[slot]) {
            case SynthType::Kick:
                rendered = drumrom::synth::synthesize_kick_custom(params, rng, num_samples);
                break;
            case SynthType::Snare:
                rendered = drumrom::synth::synthesize_snare_custom(params, rng, num_samples);
                break;
            case SynthType::Hihat:
                rendered = drumrom::synth::synthesize_hihat_custom(params, rng, num_samples);
                break;
            case SynthType::Tom:
                rendered = drumrom::synth::synthesize_tom_custom(params, rng, num_samples);
                break;
            case SynthType::Clap:
                rendered = drumrom::synth::synthesize_clap_custom(params, rng, num_samples);
                break;
            case SynthType::Elements:
            case SynthType::ElementsExact:
                break;
        }
    }

    if (rendered.empty()) {
        return false;
    }

    out_pcm16->resize(rendered.size());
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        const std::int8_t s8 = static_cast<std::int8_t>(rendered[i]);
        out_pcm16->at(i) = static_cast<std::int16_t>(s8) << 8;
    }
    return true;
}

bool build_sample_preview_audio(const AppState* state, std::size_t slot, std::vector<std::int16_t>* out_pcm16) {
    if (state == nullptr || out_pcm16 == nullptr || slot >= state->slot_source_kinds.size()) {
        return false;
    }
    if (state->slot_source_kinds[slot] == SlotSourceKind::Synth) {
        return false;
    }

    std::vector<std::int8_t> src;
    if (!load_slot_audio_for_preview(state, slot, &src) || src.empty()) {
        return false;
    }

    const auto& s = state->slot_sample_params[slot];
    int start_pct = s.start_pct;
    int end_pct = s.end_pct;
    int loop_start_pct = s.loop_start_pct;
    int loop_end_pct = s.loop_end_pct;
    normalize_percent_window(&start_pct, &end_pct);
    normalize_percent_window(&loop_start_pct, &loop_end_pct);
    const float loop_increment = s.loop_increment_pct / 100.0f;
    const float tune_semitones = s.tune_semitones;
    const float filter_cutoff_start = s.filter_cutoff_hz;
    const float filter_cutoff_end = s.filter_cutoff_end_hz;
    const float filter_env_decay_s = s.filter_env_decay_s;
    const float filter_resonance = s.filter_resonance;
    const float amp_attack_s = s.amp_attack_s;
    const float amp_decay_s = s.amp_decay_s;
    const float amp_sustain = s.amp_sustain;
    const float amp_release_s = s.amp_release_s;
    const int amp_mode = s.amp_mode;
    const float output_gain_db = s.output_gain_db;
    const float limiter_ceiling = s.limiter_ceiling;
    const int output_shaper_mode = s.output_shaper_mode;
    const float output_saturation = s.output_saturation;
    const float source_rate_hz = s.source_rate_hz;

    const std::size_t src_len = src.size();
    const std::size_t start_i = std::min<std::size_t>((static_cast<std::size_t>(start_pct) * (src_len - 1)) / 100u, src_len - 1);
    const std::size_t end_i = std::max<std::size_t>(start_i + 1, std::min<std::size_t>((static_cast<std::size_t>(end_pct) * (src_len - 1)) / 100u, src_len - 1));
    const std::size_t loop_start_i = std::min<std::size_t>(start_i + ((static_cast<std::size_t>(loop_start_pct) * (end_i - start_i)) / 100u), end_i - 1);
    const std::size_t loop_end_i = std::max<std::size_t>(loop_start_i + 1, std::min<std::size_t>(start_i + ((static_cast<std::size_t>(loop_end_pct) * (end_i - start_i)) / 100u), end_i));

    const std::size_t out_len = (slot < state->slot_capacities.size() && state->slot_capacities[slot] > 0)
        ? state->slot_capacities[slot]
        : (end_i - start_i + 1);
    if (out_len == 0) {
        return false;
    }

    const float playback_rate = 20833.0f;
    const float pitch_ratio = std::pow(2.0f, tune_semitones / 12.0f);
    float step = std::max(0.1f, (source_rate_hz / playback_rate) * pitch_ratio);
    step *= (1.0f + loop_increment);
    step = std::clamp(step, 0.05f, 8.0f);

    std::vector<float> preview;
    preview.reserve(out_len);
    float read_pos = static_cast<float>(start_i);
    const float output_gain = std::pow(10.0f, output_gain_db / 20.0f);
    const float region_duration_s = static_cast<float>(end_i - start_i + 1) / std::max(1.0f, source_rate_hz);

    for (std::size_t i = 0; i < out_len; ++i) {
        if (read_pos >= static_cast<float>(end_i)) {
            if (loop_end_i > loop_start_i + 1) {
                const float loop_len = std::max(1.0f, static_cast<float>(loop_end_i - loop_start_i));
                while (read_pos >= static_cast<float>(loop_end_i)) {
                    read_pos -= loop_len;
                }
                if (read_pos < static_cast<float>(loop_start_i)) {
                    read_pos = static_cast<float>(loop_start_i);
                }
            } else {
                break;
            }
        }

        const std::size_t i0 = static_cast<std::size_t>(std::clamp(read_pos, 0.0f, static_cast<float>(src_len - 1)));
        const std::size_t i1 = std::min<std::size_t>(i0 + 1, src_len - 1);
        const float frac = read_pos - static_cast<float>(i0);
        const float s0 = static_cast<float>(src[i0]) / 128.0f;
        const float s1 = static_cast<float>(src[i1]) / 128.0f;
        preview.push_back((s0 * (1.0f - frac)) + (s1 * frac));
        read_pos += step;
    }

    if (preview.empty()) {
        return false;
    }

    drumrom::sample_dsp::apply_filter24_with_env(
        &preview,
        playback_rate,
        filter_cutoff_start,
        filter_cutoff_end,
        filter_env_decay_s,
        filter_resonance);

    out_pcm16->assign(out_len, 0);
    const float processed_duration_s = static_cast<float>(preview.size()) / playback_rate;
    for (std::size_t i = 0; i < preview.size(); ++i) {
        float y = preview[i];
        const float t = static_cast<float>(i) / playback_rate;

        float amp = 1.0f;
        if (amp_mode != 0) {
            float env_t = t;
            if (amp_mode == 1 && region_duration_s > 0.0001f && processed_duration_s > 0.0001f) {
                env_t *= (processed_duration_s / region_duration_s);
            }

            if (amp_attack_s > 0.0001f && env_t < amp_attack_s) {
                amp = env_t / amp_attack_s;
            } else if (amp_decay_s > 0.0001f && env_t < (amp_attack_s + amp_decay_s)) {
                const float td = (env_t - amp_attack_s) / amp_decay_s;
                amp = 1.0f + (amp_sustain - 1.0f) * std::clamp(td, 0.0f, 1.0f);
            } else {
                amp = amp_sustain;
            }

            if (amp_release_s > 0.0001f) {
                const float rel_start = std::max(0.0f, processed_duration_s - amp_release_s);
                if (t >= rel_start) {
                    const float tr = std::clamp((t - rel_start) / amp_release_s, 0.0f, 1.0f);
                    amp *= (1.0f - tr);
                }
            }
        }

        y *= amp * output_gain;

        if (output_shaper_mode == 1) {
            y = std::clamp(y, -limiter_ceiling, limiter_ceiling);
        } else if (output_shaper_mode == 2) {
            const float drive = 1.0f + (output_saturation * 6.0f);
            y = std::tanh(y * drive) / std::max(1.0f, std::tanh(drive));
            y = std::clamp(y, -limiter_ceiling, limiter_ceiling);
        } else {
            y = std::clamp(y, -limiter_ceiling, limiter_ceiling);
        }

        out_pcm16->at(i) = static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
    }

    return !out_pcm16->empty();
}

void trigger_slot_preview(const AppState* state, SDL_AudioDeviceID preview_device) {
    if (state == nullptr || preview_device == 0) {
        return;
    }

    std::vector<std::int16_t> pcm16;
    if (!build_synth_preview_audio(state, state->selected_slot, &pcm16) &&
        !build_sample_preview_audio(state, state->selected_slot, &pcm16)) {
        return;
    }

    SDL_ClearQueuedAudio(preview_device);
    SDL_QueueAudio(preview_device, pcm16.data(), static_cast<Uint32>(pcm16.size() * sizeof(std::int16_t)));
    SDL_PauseAudioDevice(preview_device, 0);
}

void draw_envelope_overlays(AppState* state,
                            std::size_t slot_index,
                            ImDrawList* draw_list,
                            const ImVec2& origin,
                            const ImVec2& max) {
    if (state == nullptr || draw_list == nullptr || slot_index >= state->slot_param_values.size()) {
        return;
    }

    const auto& p = state->slot_param_values[slot_index];
    const float w = std::max(1.0f, max.x - origin.x);
    const float h = std::max(1.0f, max.y - origin.y);
    const float base_y = origin.y + (h * 0.92f);

    auto x_at = [&](float t) { return origin.x + (std::clamp(t, 0.0f, 1.0f) * w); };
    auto y_amp = [&](float v) { return base_y - (std::clamp(v, 0.0f, 1.0f) * h * 0.78f); };

    const bool sample_based = (slot_index < state->slot_source_kinds.size()) &&
                              (state->slot_source_kinds[slot_index] != SlotSourceKind::Synth);

    const int amp_atk_idx = sample_based ? 10 : 3;
    const int amp_dec_idx = sample_based ? 11 : 4;
    const int amp_sus_idx = sample_based ? 12 : 5;
    const int amp_rel_idx = sample_based ? 13 : 6;
    const int filt_start_idx = sample_based ? 6 : 7;
    const int filt_end_idx = sample_based ? 7 : 16;
    const int filt_decay_idx = sample_based ? 8 : 4;
    const int pitch_start_idx = sample_based ? 5 : 10;
    const int pitch_end_idx = sample_based ? 4 : 11;

    float atk = std::clamp(p[amp_atk_idx] / 127.0f, 0.02f, 0.28f);
    float dec = std::clamp(p[amp_dec_idx] / 127.0f, 0.05f, 0.35f);
    float sus = std::clamp(p[amp_sus_idx] / 127.0f, 0.0f, 1.0f);
    float rel = std::clamp(p[amp_rel_idx] / 127.0f, 0.05f, 0.35f);
    float f_start = std::clamp(p[filt_start_idx] / 127.0f, 0.0f, 1.0f);
    float f_end = std::clamp(p[filt_end_idx] / 127.0f, 0.0f, 1.0f);
    float f_decay = std::clamp(p[filt_decay_idx] / 127.0f, 0.05f, 1.0f);
    float p_start = std::clamp(p[pitch_start_idx] / 127.0f, 0.0f, 1.0f);
    float p_end = std::clamp((p[pitch_start_idx] - p[pitch_end_idx]) / 127.0f, 0.0f, 1.0f);
    float pitch_decay_norm = std::clamp(p[pitch_end_idx] / 127.0f, 0.0f, 1.0f);

    if (sample_based && slot_index < state->slot_sample_params.size()) {
        const auto& s = state->slot_sample_params[slot_index];
        atk = std::clamp(s.amp_attack_s / 0.3f, 0.02f, 0.28f);
        dec = std::clamp(s.amp_decay_s / 0.6f, 0.05f, 0.35f);
        sus = std::clamp(s.amp_sustain, 0.0f, 1.0f);
        rel = std::clamp(s.amp_release_s / 0.6f, 0.05f, 0.35f);
        f_start = std::clamp(cutoff_hz_to_u7(s.filter_cutoff_hz) / 127.0f, 0.0f, 1.0f);
        f_end = std::clamp(cutoff_hz_to_u7(s.filter_cutoff_end_hz) / 127.0f, 0.0f, 1.0f);
        f_decay = std::clamp((s.filter_env_decay_s - 0.01f) / (2.0f - 0.01f), 0.05f, 1.0f);
        p_start = std::clamp((s.tune_semitones + 24.0f) / 48.0f, 0.0f, 1.0f);
        p_end = p_start;
        pitch_decay_norm = 0.5f;
    }

    const float amp_s = std::clamp(1.0f - (atk + dec + rel), 0.05f, 0.7f);

    const float x0 = x_at(0.0f);
    const float x1 = x_at(atk);
    const float x2 = x_at(atk + dec);
    const float x3 = x_at(atk + dec + amp_s);
    const float x4 = x_at(1.0f);

    const float y0 = y_amp(0.0f);
    const float y1 = y_amp(1.0f);
    const float y2 = y_amp(sus);
    const float y3 = y_amp(sus);
    const float y4 = y_amp(0.0f);

    draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(230, 80, 80, 220), 1.5f);
    draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(230, 80, 80, 220), 1.5f);
    draw_list->AddLine(ImVec2(x2, y2), ImVec2(x3, y3), IM_COL32(230, 80, 80, 220), 1.5f);
    draw_list->AddLine(ImVec2(x3, y3), ImVec2(x4, y4), IM_COL32(230, 80, 80, 220), 1.5f);

    const float fx1 = x_at(0.15f + (f_decay * 0.45f));
    draw_list->AddLine(ImVec2(x0, y_amp(f_start)), ImVec2(fx1, y_amp(f_end)), IM_COL32(80, 220, 120, 220), 1.5f);
    draw_list->AddLine(ImVec2(fx1, y_amp(f_end)), ImVec2(x4, y_amp(f_end * 0.85f)), IM_COL32(80, 220, 120, 220), 1.5f);

    const float px1 = x_at(0.20f + pitch_decay_norm * 0.35f);
    draw_list->AddLine(ImVec2(x0, y_amp(p_start)), ImVec2(px1, y_amp(p_end)), IM_COL32(80, 140, 255, 220), 1.5f);
    draw_list->AddLine(ImVec2(px1, y_amp(p_end)), ImVec2(x4, y_amp(p_end)), IM_COL32(80, 140, 255, 220), 1.5f);
}

void load_default_kit_slot_types(AppState* state) {
    if (state == nullptr) {
        return;
    }

    init_slot_type_defaults(state);

    const std::filesystem::path kit_path = std::filesystem::path("kits") / "default-rz1-kit.kit";
    std::ifstream in(kit_path, std::ios::binary);
    if (!in) {
        return;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in || magic != kPresetAllMagic || version > kPresetVersion || count == 0 || count > 256) {
        return;
    }

    state->default_kit_loaded = true;
    state->default_kit_loaded_slots = 0;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t slot_index = 0;
        std::uint32_t src = 0;
        std::uint32_t drum = 0;
        std::uint32_t seed = 0;
        drumrom::synth::DrumParams params{};
        std::uint32_t path_len = 0;
        std::string sample_path;
        float sample_source_rate_hz = 0.0f;
        int sample_start_pct = 0;
        int sample_end_pct = 0;
        int sample_loop_start_pct = 0;
        int sample_loop_end_pct = 0;
        float sample_loop_increment_pct = 0.0f;
        float sample_tune_semitones = 0.0f;
        float sample_filter_cutoff_hz = 0.0f;
        float sample_filter_cutoff_end_hz = 0.0f;
        float sample_filter_env_decay_s = 0.0f;
        float sample_filter_resonance = 0.0f;
        float sample_amp_attack_s = 0.0f;
        float sample_amp_decay_s = 0.0f;
        float sample_amp_sustain = 0.0f;
        float sample_amp_release_s = 0.0f;
        float output_gain_db = 0.0f;
        float limiter_ceiling = 0.0f;
        int output_shaper_mode = 0;
        float output_saturation = 0.0f;
        std::uint32_t amp_env_mode = 0;
        drumrom::synth::ElementsParams elements_params{};
        drumrom::synth::ElementsExactParams elements_exact_params{};

        in.read(reinterpret_cast<char*>(&slot_index), sizeof(slot_index));
        in.read(reinterpret_cast<char*>(&src), sizeof(src));
        in.read(reinterpret_cast<char*>(&drum), sizeof(drum));
        in.read(reinterpret_cast<char*>(&seed), sizeof(seed));
        if (version >= 9) {
            in.read(reinterpret_cast<char*>(&params), sizeof(params));
        } else if (!read_legacy_drum_params(in, version, params)) {
            return;
        }
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));

        if (!in || path_len > 100000u) {
            return;
        }

        if (path_len > 0) {
            sample_path.resize(path_len);
            in.read(sample_path.data(), static_cast<std::streamsize>(path_len));
        }

        in.read(reinterpret_cast<char*>(&sample_source_rate_hz), sizeof(sample_source_rate_hz));
        in.read(reinterpret_cast<char*>(&sample_start_pct), sizeof(sample_start_pct));
        in.read(reinterpret_cast<char*>(&sample_end_pct), sizeof(sample_end_pct));
        if (version >= 6) {
            in.read(reinterpret_cast<char*>(&sample_loop_start_pct), sizeof(sample_loop_start_pct));
            in.read(reinterpret_cast<char*>(&sample_loop_end_pct), sizeof(sample_loop_end_pct));
            in.read(reinterpret_cast<char*>(&sample_loop_increment_pct), sizeof(sample_loop_increment_pct));
        } else {
            sample_loop_start_pct = sample_start_pct;
            sample_loop_end_pct = sample_end_pct;
            sample_loop_increment_pct = 0.0f;
        }
        in.read(reinterpret_cast<char*>(&sample_tune_semitones), sizeof(sample_tune_semitones));
        in.read(reinterpret_cast<char*>(&sample_filter_cutoff_hz), sizeof(sample_filter_cutoff_hz));
        in.read(reinterpret_cast<char*>(&sample_filter_cutoff_end_hz), sizeof(sample_filter_cutoff_end_hz));
        in.read(reinterpret_cast<char*>(&sample_filter_env_decay_s), sizeof(sample_filter_env_decay_s));
        in.read(reinterpret_cast<char*>(&sample_filter_resonance), sizeof(sample_filter_resonance));
        in.read(reinterpret_cast<char*>(&sample_amp_attack_s), sizeof(sample_amp_attack_s));
        in.read(reinterpret_cast<char*>(&sample_amp_decay_s), sizeof(sample_amp_decay_s));
        in.read(reinterpret_cast<char*>(&sample_amp_sustain), sizeof(sample_amp_sustain));
        in.read(reinterpret_cast<char*>(&sample_amp_release_s), sizeof(sample_amp_release_s));
        if (version >= 4) {
            in.read(reinterpret_cast<char*>(&output_gain_db), sizeof(output_gain_db));
            in.read(reinterpret_cast<char*>(&limiter_ceiling), sizeof(limiter_ceiling));
        } else {
            output_gain_db = 0.0f;
            limiter_ceiling = 1.0f;
        }

        if (version >= 5) {
            in.read(reinterpret_cast<char*>(&output_shaper_mode), sizeof(output_shaper_mode));
            in.read(reinterpret_cast<char*>(&output_saturation), sizeof(output_saturation));
            if (version < 6) {
                if (output_shaper_mode == 0) {
                    output_shaper_mode = 1;
                } else if (output_shaper_mode == 1) {
                    output_shaper_mode = 2;
                }
            }
        } else {
            output_shaper_mode = 2;
            output_saturation = 0.65f;
        }

        if (version >= 7) {
            in.read(reinterpret_cast<char*>(&amp_env_mode), sizeof(amp_env_mode));
        } else {
            amp_env_mode = 2u;
        }

        if (version >= 8) {
            in.read(reinterpret_cast<char*>(&elements_params), sizeof(elements_params));
        } else {
            elements_params = drumrom::synth::ElementsParams{};
        }

        if (version >= 10) {
            in.read(reinterpret_cast<char*>(&elements_exact_params), sizeof(elements_exact_params));
        } else {
            elements_exact_params = drumrom::synth::ElementsExactParams{};
        }
        if (!in) {
            return;
        }

        const std::size_t mapped_slot = map_legacy_slot_index(slot_index, count);
        if (mapped_slot >= state->slot_source_kinds.size()) {
            continue;
        }

        state->default_kit_loaded_slots += 1;
        if (!sample_path.empty()) {
            state->slot_sample_paths[mapped_slot] = sample_path;
        }

        if (src == 1u) {
            state->slot_source_kinds[mapped_slot] = SlotSourceKind::Sample;
        } else if (src == 2u) {
            state->slot_source_kinds[mapped_slot] = SlotSourceKind::Loop;
        } else {
            state->slot_source_kinds[mapped_slot] = SlotSourceKind::Synth;
        }

        switch (drum) {
            case 0u: state->slot_synth_types[mapped_slot] = SynthType::Kick; break;
            case 1u: state->slot_synth_types[mapped_slot] = SynthType::Snare; break;
            case 2u: state->slot_synth_types[mapped_slot] = SynthType::Hihat; break;
            case 3u: state->slot_synth_types[mapped_slot] = SynthType::Tom; break;
            case 4u: state->slot_synth_types[mapped_slot] = SynthType::Clap; break;
            case 5u: state->slot_synth_types[mapped_slot] = SynthType::Elements; break;
            case 6u: state->slot_synth_types[mapped_slot] = SynthType::ElementsExact; break;
            default: state->slot_synth_types[mapped_slot] = SynthType::Tom; break;
        }

        state->slot_elements_params[mapped_slot] = elements_params;
        state->slot_elements_exact_params[mapped_slot] = elements_exact_params;

        if (state->slot_source_kinds[mapped_slot] == SlotSourceKind::Synth) {
            apply_drum_params_to_slot_values(state, mapped_slot, params);
        } else {
            apply_sample_settings_to_slot_values(
                state,
                mapped_slot,
                sample_start_pct,
                sample_end_pct,
                sample_loop_start_pct,
                sample_loop_end_pct,
                sample_loop_increment_pct,
                sample_tune_semitones,
                sample_filter_cutoff_hz,
                sample_filter_cutoff_end_hz,
                sample_filter_env_decay_s,
                sample_filter_resonance,
                sample_amp_attack_s,
                sample_amp_decay_s,
                sample_amp_sustain,
                sample_amp_release_s,
                sample_source_rate_hz,
                output_gain_db,
                limiter_ceiling,
                output_shaper_mode,
                output_saturation,
                amp_env_mode);
        }
    }
}

int current_type_token_for_slot(const AppState* state) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return 0;
    }

    const SlotSourceKind source = state->slot_source_kinds[state->selected_slot];
    const SynthType synth_type = state->slot_synth_types[state->selected_slot];
    if (source == SlotSourceKind::Sample) {
        return 0;
    }
    if (source == SlotSourceKind::Loop) {
        return 1;
    }
    switch (synth_type) {
        case SynthType::Kick: return 2;
        case SynthType::Snare: return 3;
        case SynthType::Hihat: return 4;
        case SynthType::Tom: return 5;
        case SynthType::Clap: return 6;
        case SynthType::Elements: return 7;
        case SynthType::ElementsExact: return 8;
    }
    return 5;
}

void apply_type_token_to_slot(AppState* state, int token_index) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return;
    }

    const int next_token = std::clamp(token_index, 0, 8);
    if (next_token == current_type_token_for_slot(state)) {
        return;
    }
    push_undo_snapshot(state);

    state->selected_type_token = next_token;
    switch (state->selected_type_token) {
        case 0:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Sample;
            break;
        case 1:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Loop;
            break;
        case 2:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Kick;
            break;
        case 3:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Snare;
            break;
        case 4:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Hihat;
            break;
        case 5:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Tom;
            break;
        case 6:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Clap;
            break;
        case 7:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::Elements;
            break;
        case 8:
            state->slot_source_kinds[state->selected_slot] = SlotSourceKind::Synth;
            state->slot_synth_types[state->selected_slot] = SynthType::ElementsExact;
            break;
        default:
            break;
    }
    if (state->slot_source_kinds[state->selected_slot] == SlotSourceKind::Synth &&
        state->slot_synth_types[state->selected_slot] != SynthType::Elements &&
        state->slot_synth_types[state->selected_slot] != SynthType::ElementsExact) {
        apply_drum_params_to_slot_values(state, state->selected_slot, state->slot_drum_params[state->selected_slot]);
    }
    if (state->slot_source_kinds[state->selected_slot] != SlotSourceKind::Synth) {
        normalize_sample_slot_params(state, state->selected_slot);
    }
    state->waveform_dirty = true;
    clamp_selected_param_to_view(state);
}

int page_index(HandheldPage page) {
    return static_cast<int>(page);
}

HandheldPage page_from_index(int index) {
    index = std::clamp(index, 0, static_cast<int>(kPageTitles.size()) - 1);
    if (index == 0) {
        return HandheldPage::SlotSelect;
    }
    if (index == 1) {
        return HandheldPage::Edit;
    }
    if (index == 2) {
        return HandheldPage::Library;
    }
    if (index == 3) {
        return HandheldPage::Export;
    }
    if (index == 4) {
        return HandheldPage::Settings;
    }
    return HandheldPage::Status;
}

void set_page(AppState* state, HandheldPage page) {
    if (state == nullptr) {
        return;
    }
    state->page = page;
    state->edit_focus_on_slot_column = false;
    state->edit_focus_on_type_bar = false;
    state->page_menu_index = page_index(page);
    clamp_selected_param_to_view(state);
}

void return_to_page_root(AppState* state) {
    if (state == nullptr) {
        return;
    }
    state->edit_focus_on_slot_column = false;
    state->edit_focus_on_type_bar = false;
}

void open_page_menu(AppState* state) {
    if (state == nullptr) {
        return;
    }
    state->page_menu_open = true;
    state->page_menu_index = page_index(state->page);
}

void close_page_menu(AppState* state) {
    if (state == nullptr) {
        return;
    }
    state->page_menu_open = false;
}

void move_page_menu_selection(AppState* state, int delta) {
    if (state == nullptr) {
        return;
    }
    state->page_menu_index = std::clamp(state->page_menu_index + delta, 0, static_cast<int>(kPageTitles.size()) - 1);
}

void commit_page_menu_selection(AppState* state) {
    if (state == nullptr) {
        return;
    }
    set_page(state, page_from_index(state->page_menu_index));
    close_page_menu(state);
}

void move_slot(AppState* state, int delta) {
    if (state == nullptr) {
        return;
    }
    const int prev = static_cast<int>(state->selected_slot);
    const int next = std::clamp(prev + delta, 0, 15);
    state->selected_slot = static_cast<std::size_t>(next);
    if (next != prev) {
        state->waveform_dirty = true;
        clamp_selected_param_to_view(state);
    }
}

void move_slot_vertical(AppState* state, int delta_rows) {
    move_slot(state, delta_rows);
}

void move_param(AppState* state, int delta) {
    if (state == nullptr) {
        return;
    }
    const std::size_t count = current_param_count(state);
    const int max_index = (count > 0) ? static_cast<int>(count) - 1 : 0;
    const int next = std::clamp(static_cast<int>(state->selected_param) + delta, 0, max_index);
    state->selected_param = static_cast<std::size_t>(next);
}

void randomize_selected_slot_params(AppState* state) {
    if (state == nullptr || state->selected_slot >= state->slot_param_values.size()) {
        return;
    }

    push_undo_snapshot(state);

    std::random_device rd;
    std::mt19937 rng(rd());

    const bool sample_based = state->slot_source_kinds[state->selected_slot] != SlotSourceKind::Synth;
    if (sample_based) {
        auto& s = state->slot_sample_params[state->selected_slot];
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_real_distribution<float> pct01(0.0f, 99.0f);
        std::uniform_real_distribution<float> tune(-24.0f, 24.0f);
        std::uniform_real_distribution<float> loop_inc(-50.0f, 50.0f);
        std::uniform_real_distribution<float> cutoff(40.0f, 12000.0f);
        std::uniform_real_distribution<float> decay_s(0.01f, 2.0f);
        std::uniform_real_distribution<float> atk_s(0.0f, 0.3f);
        std::uniform_real_distribution<float> dcy_s(0.0f, 0.6f);
        std::uniform_real_distribution<float> rel_s(0.0f, 0.6f);
        std::uniform_real_distribution<float> out_db(-24.0f, 24.0f);
        std::uniform_real_distribution<float> lim(0.4f, 1.0f);
        std::uniform_real_distribution<float> sat(0.0f, 1.0f);
        std::uniform_real_distribution<float> rate(1000.0f, 96000.0f);

        s.start_pct = static_cast<int>(pct01(rng));
        s.end_pct = std::uniform_int_distribution<int>(s.start_pct + 1, 100)(rng);
        s.loop_start_pct = static_cast<int>(pct01(rng));
        s.loop_end_pct = std::uniform_int_distribution<int>(s.loop_start_pct + 1, 100)(rng);
        s.loop_increment_pct = loop_inc(rng);
        s.tune_semitones = tune(rng);
        s.filter_cutoff_hz = cutoff(rng);
        s.filter_cutoff_end_hz = cutoff(rng);
        s.filter_env_decay_s = decay_s(rng);
        s.filter_resonance = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
        s.amp_attack_s = atk_s(rng);
        s.amp_decay_s = dcy_s(rng);
        s.amp_sustain = unit(rng);
        s.amp_release_s = rel_s(rng);
        s.amp_mode = std::uniform_int_distribution<int>(0, 2)(rng);
        s.output_gain_db = out_db(rng);
        s.limiter_ceiling = lim(rng);
        s.output_shaper_mode = std::uniform_int_distribution<int>(0, 2)(rng);
        s.output_saturation = sat(rng);
        s.source_rate_hz = rate(rng);
        normalize_sample_slot_params(state, state->selected_slot);
    } else {
        const SynthType t = state->slot_synth_types[state->selected_slot];
        if (t == SynthType::Elements) {
            auto& e = state->slot_elements_params[state->selected_slot];
            e.model = static_cast<drumrom::synth::ElementsModel>(std::uniform_int_distribution<int>(0, 5)(rng));
            e.frequency_hz = std::uniform_real_distribution<float>(20.0f, 1200.0f)(rng);
            e.brightness = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            e.damping = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            e.position = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            e.exciter_level = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            e.exciter_noise = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            e.exciter_dur_s = std::uniform_real_distribution<float>(0.001f, 0.06f)(rng);
            e.env_decay_rate = std::uniform_real_distribution<float>(0.0f, 30.0f)(rng);
            e.env_attack_rate = std::uniform_real_distribution<float>(0.0f, 20.0f)(rng);
        } else if (t == SynthType::ElementsExact) {
            auto& ex = state->slot_elements_exact_params[state->selected_slot];
            auto u01 = std::uniform_real_distribution<float>(0.0f, 1.0f);
            ex.note = std::uniform_real_distribution<float>(0.0f, 96.0f)(rng);
            ex.modulation = u01(rng);
            ex.strength = u01(rng);
            ex.blow_cv = u01(rng);
            ex.strike_cv = u01(rng);
            ex.exciter_envelope_shape = u01(rng);
            ex.exciter_bow_level = u01(rng);
            ex.exciter_bow_timbre = u01(rng);
            ex.exciter_blow_level = u01(rng);
            ex.exciter_blow_meta = u01(rng);
            ex.exciter_blow_timbre = u01(rng);
            ex.exciter_strike_level = u01(rng);
            ex.exciter_strike_meta = u01(rng);
            ex.exciter_strike_timbre = u01(rng);
            ex.exciter_signature = u01(rng);
            ex.resonator_geometry = u01(rng);
            ex.resonator_brightness = u01(rng);
            ex.resonator_damping = u01(rng);
            ex.resonator_position = u01(rng);
            ex.resonator_modulation_frequency = std::uniform_real_distribution<float>(0.0f, 0.01f)(rng);
            ex.resonator_modulation_offset = u01(rng);
            ex.reverb_diffusion = u01(rng);
            ex.reverb_lp = u01(rng);
            ex.space = u01(rng);
            ex.modulation_frequency = u01(rng);
            ex.resonator_model = static_cast<drumrom::synth::ElementsExactResonatorModel>(std::uniform_int_distribution<int>(0, 2)(rng));
            ex.easter_egg = u01(rng);
            ex.cv_env1.target = static_cast<drumrom::synth::ElementsExactCvTarget>(std::uniform_int_distribution<int>(0, 13)(rng));
            ex.cv_env1.amount = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
            ex.cv_env1.attack_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
            ex.cv_env1.decay_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
            ex.cv_env1.sustain = u01(rng);
            ex.cv_env1.release_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
            ex.cv_env2.target = static_cast<drumrom::synth::ElementsExactCvTarget>(std::uniform_int_distribution<int>(0, 13)(rng));
            ex.cv_env2.amount = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
            ex.cv_env2.attack_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
            ex.cv_env2.decay_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
            ex.cv_env2.sustain = u01(rng);
            ex.cv_env2.release_s = std::uniform_real_distribution<float>(0.0f, 2.0f)(rng);
        } else {
            auto& d = state->slot_drum_params[state->selected_slot];
            auto unit = std::uniform_real_distribution<float>(0.0f, 1.0f);
            d.reverb.width = unit(rng);
            d.reverb.pre_delay_ms = std::uniform_real_distribution<float>(0.0f, 50.0f)(rng);
            d.reverb.decay_time_ms = std::uniform_real_distribution<float>(20.0f, 2000.0f)(rng);
            d.reverb.wet_level = unit(rng);
            d.reverb.decay_shape = unit(rng);
            d.reverb.early_level = unit(rng);
            d.reverb.diffusion = unit(rng);
            d.kick.fm.mod_index = std::uniform_real_distribution<float>(0.0f, 12.0f)(rng);
            d.kick.fm.amp_osc_depth = unit(rng);

            if (t == SynthType::Kick) {
                d.kick.pitch_start_hz = std::uniform_real_distribution<float>(40.0f, 320.0f)(rng);
                d.kick.pitch_end_hz = std::uniform_real_distribution<float>(20.0f, 120.0f)(rng);
                d.kick.pitch_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.kick.env_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.kick.attack_rate = std::uniform_real_distribution<float>(0.0f, 20.0f)(rng);
                d.kick.tone_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
            } else if (t == SynthType::Snare) {
                d.snare.tone_freq_hz = std::uniform_real_distribution<float>(60.0f, 2000.0f)(rng);
                d.snare.tone_freq_end_hz = std::uniform_real_distribution<float>(60.0f, 2000.0f)(rng);
                d.snare.pitch_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.snare.tone_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.snare.noise_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.snare.tone_mix = unit(rng);
                d.snare.noise_mix = unit(rng);
                d.snare.attack_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.snare.amp_decay_rate = std::uniform_real_distribution<float>(0.0f, 120.0f)(rng);
            } else if (t == SynthType::Hihat) {
                d.hihat.tone_freq_hz = std::uniform_real_distribution<float>(100.0f, 2000.0f)(rng);
                d.hihat.tone_freq_end_hz = std::uniform_real_distribution<float>(100.0f, 2000.0f)(rng);
                d.hihat.pitch_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.hihat.tone_mix = unit(rng);
                d.hihat.hp_cutoff_hz = std::uniform_real_distribution<float>(100.0f, 12000.0f)(rng);
                d.hihat.hp_resonance = std::uniform_real_distribution<float>(0.1f, 3.0f)(rng);
                d.hihat.tone_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.hihat.decay_rate = std::uniform_real_distribution<float>(0.0f, 120.0f)(rng);
                d.hihat.attack_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
            } else if (t == SynthType::Tom) {
                d.tom.pitch_start_hz = std::uniform_real_distribution<float>(40.0f, 400.0f)(rng);
                d.tom.pitch_end_hz = std::uniform_real_distribution<float>(20.0f, 300.0f)(rng);
                d.tom.pitch_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.tom.env_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.tom.attack_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.tom.tone_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.tom.duration_s = std::uniform_real_distribution<float>(0.04f, 1.0f)(rng);
            } else if (t == SynthType::Clap) {
                d.clap.tone_freq_hz = std::uniform_real_distribution<float>(30.0f, 4000.0f)(rng);
                d.clap.tone_freq_end_hz = std::uniform_real_distribution<float>(30.0f, 4000.0f)(rng);
                d.clap.click_rate = std::uniform_real_distribution<float>(0.25f, 8.0f)(rng);
                d.clap.pitch_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.clap.tone_mix = unit(rng);
                d.clap.tone_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.clap.duration_s = std::uniform_real_distribution<float>(0.04f, 0.4f)(rng);
                d.clap.env_decay_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
                d.clap.attack_rate = std::uniform_real_distribution<float>(0.0f, 80.0f)(rng);
            }

            apply_drum_params_to_slot_values(state, state->selected_slot, d);
        }
    }

    state->waveform_dirty = true;
    clamp_selected_param_to_view(state);
}

int preview_param_value(std::size_t slot_index, std::size_t param_index) {
    return static_cast<int>((slot_index * 11 + param_index * 7) % 128);
}

void render_source_type_bar(AppState* state) {
    if (state == nullptr || state->selected_slot >= state->slot_source_kinds.size()) {
        return;
    }

    const SlotSourceKind source = state->slot_source_kinds[state->selected_slot];
    const SynthType synth_type = state->slot_synth_types[state->selected_slot];
    const int active_token = current_type_token_for_slot(state);
    if (!state->edit_focus_on_type_bar) {
        state->selected_type_token = active_token;
    }

    ImGui::BeginChild("SourceTypeBar", ImVec2(0.0f, 34.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto draw_source_token = [&](const char* label, int token_index, SlotSourceKind next_source, bool selected) {
        const bool nav_selected = state->edit_focus_on_type_bar && state->selected_type_token == token_index;
        if (selected || nav_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.24f, 1.0f));
        }
        const float width = ImGui::CalcTextSize(label).x + 16.0f;
        if (ImGui::Button(label, ImVec2(width, 24.0f))) {
            (void)next_source;
            apply_type_token_to_slot(state, token_index);
        }
        if (selected || nav_selected) {
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine();
    };

    auto draw_synth_token = [&](const char* label, int token_index, SynthType next_type, bool selected) {
        const bool nav_selected = state->edit_focus_on_type_bar && state->selected_type_token == token_index;
        if (selected || nav_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.24f, 1.0f));
        }
        const float width = ImGui::CalcTextSize(label).x + 16.0f;
        if (ImGui::Button(label, ImVec2(width, 24.0f))) {
            (void)next_type;
            apply_type_token_to_slot(state, token_index);
        }
        if (selected || nav_selected) {
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine();
    };

    draw_source_token("Sample", 0, SlotSourceKind::Sample, source == SlotSourceKind::Sample);
    draw_source_token("Loop", 1, SlotSourceKind::Loop, source == SlotSourceKind::Loop);
    draw_synth_token("Kick", 2, SynthType::Kick, source == SlotSourceKind::Synth && synth_type == SynthType::Kick);
    draw_synth_token("Snare", 3, SynthType::Snare, source == SlotSourceKind::Synth && synth_type == SynthType::Snare);
    draw_synth_token("HiHat", 4, SynthType::Hihat, source == SlotSourceKind::Synth && synth_type == SynthType::Hihat);
    draw_synth_token("Tom", 5, SynthType::Tom, source == SlotSourceKind::Synth && synth_type == SynthType::Tom);
    draw_synth_token("Clap", 6, SynthType::Clap, source == SlotSourceKind::Synth && synth_type == SynthType::Clap);
    draw_synth_token("Elements", 7, SynthType::Elements, source == SlotSourceKind::Synth && synth_type == SynthType::Elements);
    draw_synth_token("ElementsX", 8, SynthType::ElementsExact, source == SlotSourceKind::Synth && synth_type == SynthType::ElementsExact);

    ImGui::EndChild();
}

void draw_waveform_preview(AppState* state, std::size_t slot_index, const ImVec2& size) {
    if (state == nullptr) {
        return;
    }
    if (state->waveform_dirty || state->selected_waveform_slot != slot_index) {
        load_slot_waveform_from_file(state, slot_index);
    }

    ImGui::InvisibleButton("##WaveformCanvas", size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    draw_list->AddRectFilled(origin, max, IM_COL32(24, 24, 24, 255), 6.0f);
    draw_list->AddRect(origin, max, IM_COL32(80, 160, 120, 255), 6.0f, 0, 1.0f);

    const float width = std::max(1.0f, max.x - origin.x);
    const float height = std::max(1.0f, max.y - origin.y);
    const float mid_y = origin.y + (height * 0.5f);
    if (state->selected_waveform.size() > 1) {
        const std::size_t points = state->selected_waveform.size();
        for (std::size_t i = 0; i + 1 < points; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(points - 1);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(points - 1);
            const float x0 = origin.x + (width * t0);
            const float x1 = origin.x + (width * t1);
            const float y0 = mid_y - (state->selected_waveform[i] * height * 0.45f);
            const float y1 = mid_y - (state->selected_waveform[i + 1] * height * 0.45f);
            draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(90, 220, 180, 255), 2.0f);
        }
    } else {
        draw_list->AddLine(ImVec2(origin.x, mid_y), ImVec2(max.x, mid_y), IM_COL32(90, 220, 180, 200), 2.0f);
    }

    draw_envelope_overlays(state, slot_index, draw_list, origin, max);

    const char* slot_name = (slot_index < kSlotFileNames.size()) ? kSlotFileNames[slot_index] : "slot";
    draw_list->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), IM_COL32(220, 220, 220, 255), slot_name);
}

void render_slot_column(AppState* state) {
    if (state == nullptr) {
        return;
    }

    const float column_width = 48.0f;
    ImGui::BeginChild("SlotColumn", ImVec2(column_width, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (std::size_t i = 0; i < 16; ++i) {
        char label[8];
        std::snprintf(label, sizeof(label), "%02zu", i + 1);
        const bool selected = (state->selected_slot == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.60f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.44f, 0.22f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(column_width - 12.0f, 28.0f))) {
            state->selected_slot = i;
            state->waveform_dirty = true;
            clamp_selected_param_to_view(state);
        }
        if (selected) {
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::EndChild();
}

void render_edit_page(AppState* state);

void render_param_grid(AppState* state) {
    if (state == nullptr) {
        return;
    }

    const int columns = kParamGridColumns;
    ImGui::BeginChild("ParamGridScroll", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Columns(columns, "ParamGridColumns", false);

    const std::size_t count = current_param_count(state);
    for (std::size_t i = 0; i < count; ++i) {
        const bool selected = (state->selected_param == i);
        const std::string value = current_param_display_text(state, i);
        const char* label = current_param_label(state, i);
        const float row_start_x = ImGui::GetCursorPosX();
        const float col_width = ImGui::GetColumnWidth();
        const float value_x = row_start_x + std::max(48.0f, col_width - 40.0f);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.90f, 1.0f, 1.0f));
            ImGui::TextUnformatted(">");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(value_x);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(" ");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(value_x);
            ImGui::TextUnformatted(value.c_str());
        }

        if (ImGui::IsItemClicked()) {
            state->selected_param = i;
        }
        if (selected) {
            ImGui::SetScrollHereY(0.5f);
        }

        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::EndChild();
}

void render_slot_select_page(AppState* state) {
    render_edit_page(state);
}

void render_edit_page(AppState* state) {
    ImGui::BeginChild("EditorBody", ImVec2(0.0f, 0.0f), false);
    const float slot_column_width = 48.0f;
    const float spacing_x = ImGui::GetStyle().ItemSpacing.x;
    const float right_width = std::max(80.0f, ImGui::GetContentRegionAvail().x - slot_column_width - spacing_x);
    const float total_height = std::max(180.0f, ImGui::GetContentRegionAvail().y);
    const float waveform_height = std::max(56.0f, total_height / 3.0f);
    const float type_bar_height = 34.0f;
    const float param_height = std::max(80.0f, total_height - waveform_height - type_bar_height - (ImGui::GetStyle().ItemSpacing.y * 2.0f));

    ImGui::BeginChild("EditSlotColumnWrap", ImVec2(slot_column_width, total_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    render_slot_column(state);
    ImGui::EndChild();
    const ImVec2 slot_min = ImGui::GetItemRectMin();
    const ImVec2 slot_max = ImGui::GetItemRectMax();

    ImGui::SameLine();
    ImGui::BeginChild("EditRightPane", ImVec2(right_width, total_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw_waveform_preview(state, state->selected_slot, ImVec2(-1.0f, waveform_height));
    ImGui::Spacing();
    render_source_type_bar(state);
    const ImVec2 type_min = ImGui::GetItemRectMin();
    const ImVec2 type_max = ImGui::GetItemRectMax();
    if (state != nullptr) {
        if (state->default_kit_loaded) {
            ImGui::Text("Kit: default-rz1-kit.kit (%d slots)", state->default_kit_loaded_slots);
        } else {
            ImGui::TextUnformatted("Kit: default-rz1-kit.kit not loaded (fallback defaults)");
        }
    }
    ImGui::Spacing();
    ImGui::BeginChild("ParamGridPanel", ImVec2(0.0f, param_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    render_param_grid(state);
    ImGui::EndChild();
    const ImVec2 param_min = ImGui::GetItemRectMin();
    const ImVec2 param_max = ImGui::GetItemRectMax();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (state != nullptr) {
        if (state->edit_focus_on_slot_column) {
            draw->AddRect(slot_min, slot_max, IM_COL32(120, 220, 140, 220), 4.0f, 0, 1.5f);
        } else if (state->edit_focus_on_type_bar) {
            draw->AddRect(type_min, type_max, IM_COL32(120, 220, 140, 220), 4.0f, 0, 1.5f);
        } else {
            draw->AddRect(param_min, param_max, IM_COL32(120, 220, 140, 220), 4.0f, 0, 1.5f);
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

bool is_shift_down() {
    const SDL_Keymod mods = SDL_GetModState();
    return (mods & KMOD_SHIFT) != 0;
}

void render_handheld_ui(const AppState& state) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Handheld Drum ROM Designer", nullptr, flags)) {
        const HandheldPage shown_page = state.page_menu_open ? page_from_index(state.page_menu_index) : state.page;

        if (shown_page == HandheldPage::SlotSelect) {
            render_slot_select_page(const_cast<AppState*>(&state));
        } else if (shown_page == HandheldPage::Edit) {
            render_edit_page(const_cast<AppState*>(&state));
        } else if (shown_page == HandheldPage::Export) {
            ImGui::TextUnformatted("Export/upload placeholder");
            ImGui::Button("Generate", ImVec2(160.0f, 48.0f));
            ImGui::SameLine();
            ImGui::Button("USB Upload", ImVec2(160.0f, 48.0f));
        } else if (shown_page == HandheldPage::Library) {
            ImGui::TextUnformatted("Library placeholder");
            ImGui::Button("Presets", ImVec2(140.0f, 48.0f));
            ImGui::SameLine();
            ImGui::Button("Kits", ImVec2(140.0f, 48.0f));
            ImGui::Spacing();
            ImGui::Button("Samples", ImVec2(140.0f, 48.0f));
            ImGui::SameLine();
            ImGui::Button("Sysex", ImVec2(140.0f, 48.0f));
        } else if (shown_page == HandheldPage::Settings) {
            ImGui::TextUnformatted("Settings placeholder");
            ImGui::Button("Display", ImVec2(160.0f, 48.0f));
            ImGui::SameLine();
            ImGui::Button("Controls", ImVec2(160.0f, 48.0f));
        } else {
            ImGui::TextUnformatted("Status / System placeholder");
            ImGui::Button("Upload State", ImVec2(160.0f, 48.0f));
            ImGui::SameLine();
            ImGui::Button("Build Info", ImVec2(160.0f, 48.0f));
        }

        if (state.page_menu_open) {
            const ImVec2 screen = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(360.0f, 280.0f), ImGuiCond_Appearing);
            if (ImGui::Begin("##PageSelectCenter",
                             nullptr,
                             ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::TextUnformatted("Page Select");
                ImGui::Separator();
                for (int i = 0; i < static_cast<int>(kPageTitles.size()); ++i) {
                    const bool selected = (state.page_menu_index == i);
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.62f, 0.24f, 1.0f));
                    }
                    if (ImGui::Button(kPageTitles[i], ImVec2(-1.0f, 32.0f))) {
                        const_cast<AppState&>(state).page_menu_index = i;
                        commit_page_menu_selection(const_cast<AppState*>(&state));
                    }
                    if (selected) {
                        ImGui::PopStyleColor(2);
                    }
                }
                ImGui::Separator();
                ImGui::Text("Preview: %s", kPageTitles[state.page_menu_index]);
            }
            ImGui::End();
        }

        ImGui::End();
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Drum Rom Designer Handheld",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640,
        360,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = 20833;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    SDL_AudioDeviceID preview_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (preview_audio_device != 0) {
        SDL_PauseAudioDevice(preview_audio_device, 0);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarSize = 8.0f;
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    AppState state;
    load_slot_capacities_from_configs(&state);
    load_default_kit_slot_types(&state);
    while (state.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                state.running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                state.running = false;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_s) {
                    trigger_slot_preview(&state, preview_audio_device);
                    continue;
                }
                if (event.key.keysym.sym == SDLK_x) {
                    randomize_selected_slot_params(&state);
                    trigger_slot_preview(&state, preview_audio_device);
                    continue;
                }
                if (event.key.keysym.sym == SDLK_b && state.page == HandheldPage::Edit && !state.page_menu_open) {
                    if (pop_undo_snapshot(&state)) {
                        trigger_slot_preview(&state, preview_audio_device);
                    }
                    continue;
                }
                if (event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_SPACE) {
                    state.param_adjust_held = true;
                }
                if (event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_RSHIFT) {
                    if (!state.page_menu_open) {
                        open_page_menu(&state);
                    }
                    continue;
                }

                if (state.page_menu_open) {
                    switch (event.key.keysym.sym) {
                        case SDLK_UP:
                            move_page_menu_selection(&state, -1);
                            break;
                        case SDLK_DOWN:
                            move_page_menu_selection(&state, +1);
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            commit_page_menu_selection(&state);
                            break;
                        case SDLK_BACKSPACE:
                        case SDLK_ESCAPE:
                            close_page_menu(&state);
                            break;
                        default:
                            break;
                    }
                    continue;
                }

                const bool shift_down = is_shift_down();
                const bool adjust_params =
                    state.page == HandheldPage::Edit &&
                    !state.edit_focus_on_slot_column &&
                    !state.edit_focus_on_type_bar &&
                    state.param_adjust_held;
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:
                        if (shift_down) {
                            if (state.page == HandheldPage::Edit) {
                                state.edit_focus_on_slot_column = true;
                                state.edit_focus_on_type_bar = false;
                            }
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, -1);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            apply_type_token_to_slot(&state, state.selected_type_token - 1);
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit && !state.edit_focus_on_slot_column) {
                            if ((state.selected_param % static_cast<std::size_t>(kParamGridColumns)) == 0) {
                                state.edit_focus_on_slot_column = true;
                                state.edit_focus_on_type_bar = false;
                            } else {
                                move_param(&state, -1);
                            }
                        }
                        break;
                    case SDLK_RIGHT:
                        if (shift_down) {
                            if (state.page == HandheldPage::Edit) {
                                state.edit_focus_on_slot_column = false;
                                state.edit_focus_on_type_bar = false;
                            }
                        } else if (state.page == HandheldPage::SlotSelect) {
                            set_page(&state, HandheldPage::Edit);
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            apply_type_token_to_slot(&state, state.selected_type_token + 1);
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, +1);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit && !state.edit_focus_on_slot_column) {
                            move_param(&state, +1);
                        }
                        break;
                    case SDLK_UP:
                        if (shift_down) {
                            if (state.page == HandheldPage::Edit) {
                                state.edit_focus_on_slot_column = !state.edit_focus_on_slot_column;
                                state.edit_focus_on_type_bar = false;
                            }
                        } else if (state.page == HandheldPage::SlotSelect) {
                            move_slot_vertical(&state, -1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            move_slot_vertical(&state, -1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            // Keep type bar focus while left/right chooses type.
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, +10);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit) {
                            if (state.selected_param < static_cast<std::size_t>(kParamGridColumns)) {
                                state.edit_focus_on_type_bar = true;
                                state.selected_type_token = current_type_token_for_slot(&state);
                            } else {
                                move_param(&state, -kParamGridColumns);
                            }
                        }
                        break;
                    case SDLK_DOWN:
                        if (shift_down) {
                            if (state.page == HandheldPage::Edit) {
                                state.edit_focus_on_slot_column = !state.edit_focus_on_slot_column;
                                state.edit_focus_on_type_bar = false;
                            }
                        } else if (state.page == HandheldPage::SlotSelect) {
                            move_slot_vertical(&state, +1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            move_slot_vertical(&state, +1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            state.edit_focus_on_type_bar = false;
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, -10);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit) {
                            move_param(&state, +kParamGridColumns);
                        }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (state.page == HandheldPage::SlotSelect) {
                            set_page(&state, HandheldPage::Edit);
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        } else {
                            state.page = HandheldPage::Edit;
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        }
                        break;
                    case SDLK_BACKSPACE:
                        return_to_page_root(&state);
                        break;
                    case SDLK_TAB:
                        if (state.page == HandheldPage::Edit) {
                            state.edit_focus_on_slot_column = !state.edit_focus_on_slot_column;
                            state.edit_focus_on_type_bar = false;
                        }
                        break;
                    default:
                        break;
                }
            }

            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_SPACE) {
                    state.param_adjust_held = false;
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    trigger_slot_preview(&state, preview_audio_device);
                    continue;
                }
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                    randomize_selected_slot_params(&state);
                    trigger_slot_preview(&state, preview_audio_device);
                    continue;
                }
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    state.param_adjust_held = true;
                }
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    if (state.page_menu_open) {
                        close_page_menu(&state);
                    } else {
                        open_page_menu(&state);
                    }
                    continue;
                }

                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B && state.page == HandheldPage::Edit && !state.page_menu_open) {
                    if (pop_undo_snapshot(&state)) {
                        trigger_slot_preview(&state, preview_audio_device);
                    }
                    continue;
                }

                if (state.page_menu_open) {
                    switch (event.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            move_page_menu_selection(&state, -1);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            move_page_menu_selection(&state, +1);
                            break;
                        case SDL_CONTROLLER_BUTTON_A:
                            commit_page_menu_selection(&state);
                            break;
                        case SDL_CONTROLLER_BUTTON_B:
                            close_page_menu(&state);
                            break;
                        default:
                            break;
                    }
                    continue;
                }

                const bool adjust_params =
                    state.page == HandheldPage::Edit &&
                    !state.edit_focus_on_slot_column &&
                    !state.edit_focus_on_type_bar &&
                    state.param_adjust_held;

                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            apply_type_token_to_slot(&state, state.selected_type_token - 1);
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, -1);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit && !state.edit_focus_on_slot_column) {
                            if ((state.selected_param % static_cast<std::size_t>(kParamGridColumns)) == 0) {
                                state.edit_focus_on_slot_column = true;
                                state.edit_focus_on_type_bar = false;
                            } else {
                                move_param(&state, -1);
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        if (state.page == HandheldPage::SlotSelect) {
                            set_page(&state, HandheldPage::Edit);
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            state.edit_focus_on_slot_column = false;
                            state.edit_focus_on_type_bar = false;
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            apply_type_token_to_slot(&state, state.selected_type_token + 1);
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, +1);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit && !state.edit_focus_on_slot_column) {
                            move_param(&state, +1);
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (state.page == HandheldPage::SlotSelect) {
                            move_slot_vertical(&state, -1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            move_slot_vertical(&state, -1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            // Keep type bar focus while left/right selects type.
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, +10);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit) {
                            if (state.selected_param < static_cast<std::size_t>(kParamGridColumns)) {
                                state.edit_focus_on_type_bar = true;
                                state.selected_type_token = current_type_token_for_slot(&state);
                            } else {
                                move_param(&state, -kParamGridColumns);
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (state.page == HandheldPage::SlotSelect) {
                            move_slot_vertical(&state, +1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_slot_column) {
                            move_slot_vertical(&state, +1);
                        } else if (state.page == HandheldPage::Edit && state.edit_focus_on_type_bar) {
                            state.edit_focus_on_type_bar = false;
                        } else if (adjust_params) {
                            adjust_current_param_value(&state, -10);
                            state.waveform_dirty = true;
                            trigger_slot_preview(&state, preview_audio_device);
                        } else if (state.page == HandheldPage::Edit) {
                            move_param(&state, +kParamGridColumns);
                        }
                        break;
                    default:
                        break;
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONUP) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    state.param_adjust_held = false;
                }
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        render_handheld_ui(state);

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 18, 18, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (preview_audio_device != 0) {
        SDL_CloseAudioDevice(preview_audio_device);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
