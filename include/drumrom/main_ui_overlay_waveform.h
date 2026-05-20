#pragma once

#include "drumrom/synth.h"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace drumrom::main_ui_overlay_waveform {

struct PreviewState {
    bool* wave_preview_dirty = nullptr;
    std::size_t* wave_preview_slot = nullptr;
    std::size_t* selected_slot = nullptr;
    std::vector<float>* wave_preview = nullptr;
    std::size_t* wave_preview_sample_length = nullptr;
};

using RenderSlotFn = std::vector<std::int8_t> (*)(std::size_t slot_index, std::mt19937& rng);
using IsSampleBasedSlotFn = bool (*)(std::size_t slot_index);
using ProcessedSampleLengthFn = std::size_t (*)(std::size_t slot_index);

void refresh_wave_preview_if_needed(PreviewState* state,
                                    RenderSlotFn render_slot_fn,
                                    IsSampleBasedSlotFn is_sample_based_slot_fn,
                                    ProcessedSampleLengthFn processed_sample_length_fn);

struct FmSectionStyle {
    float ui_scale = 1.0f;
    ImVec4 color_fm_pitch = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 color_fm_index = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 color_am_pitch = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 color_am_depth = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    int overlay_fm_mod_pitch = 0;
    int overlay_fm_index = 0;
    int overlay_am_pitch = 0;
    int overlay_am_depth = 0;
};

struct FmSectionActions {
    bool (*colored_slider_float)(const char* label,
                                 float* v,
                                 float v_min,
                                 float v_max,
                                 ImVec4 color,
                                 int overlay_to_select,
                                 const char* format) = nullptr;
    bool (*colored_rate_with_shape_row)(const char* rate_label,
                                        float* rate,
                                        float min_rate,
                                        float max_rate,
                                        const char* shape_label,
                                        drumrom::synth::EnvelopeShape* shape,
                                        ImVec4 color,
                                        int overlay_to_select) = nullptr;
    void (*envelope_group_gap)() = nullptr;
};

bool render_fm_section(drumrom::synth::FmToneParams* fm,
                       const FmSectionStyle& style,
                       const FmSectionActions& actions);

float cutoff_to_norm(float cutoff_hz);
float norm_to_cutoff(float t);

struct OverlaySelectorIds {
    int amp = 0;
    int pitch = 0;
    int tone_or_filter = 0;
    int fm_mod_pitch = 0;
    int fm_index = 0;
    int am_pitch = 0;
    int am_depth = 0;
};

struct OverlaySelectorState {
    bool sample_based_source = false;
    int* selected_overlay = nullptr;
    int* overlay_drag_point = nullptr;
    float ui_scale = 1.0f;
    OverlaySelectorIds ids{};
};

void render_overlay_selector(OverlaySelectorState* state);

std::vector<ImVec2> build_shaped_segment_points(const ImVec2& p0,
                                                const ImVec2& p1,
                                                drumrom::synth::EnvelopeShape shape,
                                                bool is_attack_segment,
                                                int steps = 28);

int pick_handle_at_point(const std::vector<ImVec2>& screen_handles,
                         const ImVec2& mouse,
                         float hit_radius);

void draw_handles(ImDrawList* draw_list,
                  const std::vector<ImVec2>& screen_handles,
                  float fill_radius,
                  float outline_radius,
                  ImU32 fill_color,
                  ImU32 outline_color,
                  float outline_thickness = 2.0f);

struct SampleDragParams {
    float* amp_attack_s = nullptr;
    float* amp_decay_s = nullptr;
    float* amp_sustain = nullptr;
    float* amp_release_s = nullptr;
    float* filter_cutoff_hz = nullptr;
    float* filter_cutoff_end_hz = nullptr;
    float* filter_env_decay_s = nullptr;
    float* tune_semitones = nullptr;
};

bool apply_sample_drag_update(int selected_overlay,
                              int drag_point,
                              float nx,
                              float ny,
                              const OverlaySelectorIds& ids,
                              SampleDragParams* params);

struct SynthDragMathFns {
    float (*x_to_rate)(float x, float min_rate, float max_rate) = nullptr;
    float (*x_to_rate_attack)(float x, float min_rate, float max_rate) = nullptr;
    float (*rate_to_x_attack)(float internal_rate, float min_rate, float max_rate) = nullptr;
    float (*norm_to_freq_range)(float t, float min_freq, float max_freq) = nullptr;
};

struct SynthDragParams {
    bool has_tone_freq_filter = false;
    float* synth_amp_attack = nullptr;
    float* synth_amp_decay = nullptr;
    float* synth_pitch_decay = nullptr;
    float* synth_pitch_start = nullptr;
    float* synth_pitch_end = nullptr;
    float* synth_filter_decay = nullptr;
    float* synth_filter_start = nullptr;
    float* synth_filter_end = nullptr;
    float synth_pitch_min = 20.0f;
    float synth_pitch_max = 4000.0f;
    float synth_filter_min = 20.0f;
    float synth_filter_max = 12000.0f;
    drumrom::synth::FmToneParams* fm = nullptr;
};

bool apply_synth_drag_update(int selected_overlay,
                             int drag_point,
                             float nx,
                             float ny,
                             const OverlaySelectorIds& ids,
                             const SynthDragMathFns& math,
                             SynthDragParams* params);

struct OverlayCurvePoints {
    std::vector<ImVec2> amp;
    std::vector<ImVec2> filter;
    std::vector<ImVec2> pitch;
    std::vector<ImVec2> fm_mod_pitch;
    std::vector<ImVec2> fm_index;
    std::vector<ImVec2> am_pitch;
    std::vector<ImVec2> am_depth;
};

struct OverlayPointMathFns {
    float (*rate_to_x)(float internal_rate, float min_rate, float max_rate) = nullptr;
    float (*rate_to_x_attack)(float internal_rate, float min_rate, float max_rate) = nullptr;
    float (*freq_to_norm_range)(float f, float min_freq, float max_freq) = nullptr;
};

struct SampleOverlayPointParams {
    float amp_attack_s = 0.0f;
    float amp_decay_s = 0.0f;
    float amp_sustain = 0.0f;
    float amp_release_s = 0.0f;
    float filter_cutoff_hz = 40.0f;
    float filter_cutoff_end_hz = 40.0f;
    float filter_env_decay_s = 0.01f;
    float tune_semitones = 0.0f;
};

struct SynthOverlayPointParams {
    bool has_tone_freq_filter = false;
    float* synth_amp_attack = nullptr;
    float* synth_amp_decay = nullptr;
    float* synth_pitch_decay = nullptr;
    float* synth_pitch_start = nullptr;
    float* synth_pitch_end = nullptr;
    float* synth_filter_decay = nullptr;
    float* synth_filter_start = nullptr;
    float* synth_filter_end = nullptr;
    float synth_pitch_min = 20.0f;
    float synth_pitch_max = 4000.0f;
    float synth_filter_min = 20.0f;
    float synth_filter_max = 12000.0f;
    drumrom::synth::FmToneParams* fm = nullptr;
};

bool build_sample_overlay_points(const SampleOverlayPointParams& params,
                                 OverlayCurvePoints* out_points);

bool build_synth_overlay_points(const SynthOverlayPointParams& params,
                                const OverlayPointMathFns& math,
                                OverlayCurvePoints* out_points);

std::vector<ImVec2> build_overlay_handles(int selected_overlay,
                                          bool sample_based_source,
                                          bool has_tone_freq_filter,
                                          const OverlaySelectorIds& ids,
                                          const OverlayCurvePoints& points);

}  // namespace drumrom::main_ui_overlay_waveform
