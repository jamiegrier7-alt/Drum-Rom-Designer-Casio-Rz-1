#pragma once

#include "drumrom/synth.h"

#include "imgui.h"

namespace drumrom::main_ui_overlay_controls {

struct State {
    int* selected_overlay = nullptr;
    bool* auto_upload_commit_requested = nullptr;
    bool* auto_play_commit_requested = nullptr;
};

bool input_int_with_scroll(State* state, const char* label, int* v, int v_min, int v_max);

bool slider_float_with_text_input(State* state,
                                  const char* label,
                                  float* v,
                                  float v_min,
                                  float v_max,
                                  const char* format,
                                  ImGuiSliderFlags flags = 0);

bool slider_rate_with_text_input(State* state,
                                 const char* label,
                                 float* internal_rate,
                                 float min_rate,
                                 float max_rate,
                                 bool attack_mode,
                                 const char* format,
                                 ImGuiSliderFlags flags = 0);

bool colored_slider_float(State* state,
                          const char* label,
                          float* v,
                          float v_min,
                          float v_max,
                          ImVec4 color,
                          int overlay_to_select,
                          const char* format = "%.2f");

bool colored_slider_rate(State* state,
                         const char* label,
                         float* internal_rate,
                         float min_rate,
                         float max_rate,
                         ImVec4 color,
                         int overlay_to_select);

bool colored_slider_attack_rate(State* state,
                                const char* label,
                                float* rate,
                                float min_rate,
                                float max_rate,
                                ImVec4 color,
                                int overlay_to_select);

bool colored_slider_float_labeled(State* state,
                                  const char* label,
                                  float* v,
                                  float v_min,
                                  float v_max,
                                  ImVec4 color,
                                  int overlay_to_select);

bool colored_slider_rate_labeled(State* state,
                                 const char* label,
                                 float* internal_rate,
                                 float min_rate,
                                 float max_rate,
                                 ImVec4 color,
                                 int overlay_to_select);

bool colored_slider_attack_rate_labeled(State* state,
                                        const char* label,
                                        float* internal_rate,
                                        float min_rate,
                                        float max_rate,
                                        ImVec4 color,
                                        int overlay_to_select);

bool shape_combo(const char* label, drumrom::synth::EnvelopeShape* shape);

float compact_shape_combo_width();

float shape_row_left_label_anchor_width();

bool colored_shape_combo(State* state,
                         const char* label,
                         drumrom::synth::EnvelopeShape* shape,
                         ImVec4 color,
                         int overlay_to_select);

bool colored_rate_with_shape_row(State* state,
                                 const char* rate_label,
                                 float* rate,
                                 float min_rate,
                                 float max_rate,
                                 const char* shape_label,
                                 drumrom::synth::EnvelopeShape* shape,
                                 ImVec4 color,
                                 int overlay_to_select);

bool colored_attack_with_shape_row(State* state,
                                   const char* attack_label,
                                   float* attack_rate,
                                   float min_rate,
                                   float max_rate,
                                   const char* shape_label,
                                   drumrom::synth::EnvelopeShape* shape,
                                   ImVec4 color,
                                   int overlay_to_select);

void envelope_group_gap();

bool render_output_controls(State* state,
                            float* output_gain_db,
                            float* limiter_ceiling,
                            int* output_shaper_mode,
                            float* output_saturation);

}  // namespace drumrom::main_ui_overlay_controls
