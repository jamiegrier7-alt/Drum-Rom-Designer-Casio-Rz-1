#pragma once

#include <vector>

namespace drumrom::sample_dsp {

// Shared 24 dB low-pass filter with exponential cutoff envelope.
// Resonance is expected in [0, 1] to match desktop sample editor behavior.
void apply_filter24_with_env(std::vector<float>* in,
                             float sample_rate,
                             float cutoff_start_hz,
                             float cutoff_end_hz,
                             float env_decay_s,
                             float resonance);

}  // namespace drumrom::sample_dsp
