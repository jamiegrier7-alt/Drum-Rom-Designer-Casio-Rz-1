#pragma once

#include <array>
#include <cstddef>

namespace drumrom::sample_schema {

enum class SampleParamId : std::size_t {
    StartPct = 0,
    EndPct,
    LoopStartPct,
    LoopEndPct,
    LoopIncrementPct,
    TuneSemitones,
    FilterCutoffStartHz,
    FilterCutoffEndHz,
    FilterEnvDecayS,
    FilterResonance,
    AmpAttackS,
    AmpDecayS,
    AmpSustain,
    AmpReleaseS,
    AmpMode,
    OutputGainDb,
    LimiterCeiling,
    OutputShaperMode,
    OutputSaturation,
    SourceRateHz,
    Count,
};

struct SampleParamSpec {
    const char* handheld_label;
    const char* desktop_label;
    float min_v;
    float max_v;
    bool integral;
    const char* format;
};

constexpr std::array<SampleParamSpec, static_cast<std::size_t>(SampleParamId::Count)> kSampleParamSpecs = {{
    {"Start", "Start %", 0.0f, 100.0f, true, "%d"},
    {"End", "End %", 0.0f, 100.0f, true, "%d"},
    {"Loop Start", "Loop Start %", 0.0f, 100.0f, true, "%d"},
    {"Loop End", "Loop End %", 0.0f, 100.0f, true, "%d"},
    {"Loop Inc", "Loop Increment %", -50.0f, 50.0f, false, "%.2f"},
    {"Tune", "Tune (semitones)", -24.0f, 24.0f, false, "%.2f"},
    {"Filter Start", "Filter Cutoff Start Hz", 40.0f, 12000.0f, false, "%.0f"},
    {"Filter End", "Filter Cutoff End Hz", 40.0f, 12000.0f, false, "%.0f"},
    {"Filter Decay", "Filter Env Decay s", 0.01f, 2.0f, false, "%.3f"},
    {"Filter Res", "Filter Resonance", 0.0f, 2.0f, false, "%.3f"},
    {"Amp Attack", "Amp Attack s", 0.0f, 0.3f, false, "%.3f"},
    {"Amp Decay", "Amp Decay s", 0.0f, 0.6f, false, "%.3f"},
    {"Amp Sustain", "Amp Sustain", 0.0f, 1.0f, false, "%.3f"},
    {"Amp Release", "Amp Release s", 0.0f, 0.6f, false, "%.3f"},
    {"Amp Mode", "Amp Mode", 0.0f, 2.0f, true, "%d"},
    {"Output Gain", "Output Gain (dB)", -24.0f, 24.0f, false, "%.2f"},
    {"Limiter", "Limiter", 0.4f, 1.0f, false, "%.3f"},
    {"Shaper", "Shaper", 0.0f, 2.0f, true, "%d"},
    {"Saturation", "Saturation", 0.0f, 1.0f, false, "%.3f"},
    {"Source Rate", "Source Rate Hz (.raw)", 1000.0f, 96000.0f, false, "%.0f"},
}};

constexpr const SampleParamSpec& sample_param_spec(SampleParamId id) {
    return kSampleParamSpecs[static_cast<std::size_t>(id)];
}

}  // namespace drumrom::sample_schema
