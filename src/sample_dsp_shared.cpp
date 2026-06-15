#include "drumrom/sample_dsp_shared.h"

#include <algorithm>
#include <cmath>

namespace drumrom::sample_dsp {

void apply_filter24_with_env(std::vector<float>* in,
                             float sample_rate,
                             float cutoff_start_hz,
                             float cutoff_end_hz,
                             float env_decay_s,
                             float resonance) {
    if (in == nullptr || in->empty()) {
        return;
    }

    const float sr = std::max(1.0f, sample_rate);
    const float c0 = std::clamp(cutoff_start_hz, 20.0f, 12000.0f);
    const float c1 = std::clamp(cutoff_end_hz, 20.0f, 12000.0f);
    const float env_s = std::max(0.001f, env_decay_s);
    const float res = std::clamp(resonance, 0.0f, 1.0f) * 4.25f;

    float y1 = 0.0f;
    float y2 = 0.0f;
    float y3 = 0.0f;
    float y4 = 0.0f;

    for (std::size_t i = 0; i < in->size(); ++i) {
        const float t = static_cast<float>(i) / sr;
        const float x = 1.0f - std::exp(-t / env_s);
        const float cutoff = c0 + ((c1 - c0) * x);
        const float g = std::min(0.99f, 2.0f * std::sin(3.14159265f * cutoff / sr));

        float u = in->at(i) - (res * y4);
        if (resonance >= 0.999f) {
            u += 1e-5f;
        }

        y1 += g * (std::tanh(u) - std::tanh(y1));
        y2 += g * (std::tanh(y1) - std::tanh(y2));
        y3 += g * (std::tanh(y2) - std::tanh(y3));
        y4 += g * (std::tanh(y3) - std::tanh(y4));
        in->at(i) = y4;
    }
}

}  // namespace drumrom::sample_dsp
