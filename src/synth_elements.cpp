// Elements-style modal synthesis implementation for the Elements drum mode.
#include "drumrom/synth_elements.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace drumrom::synth {

namespace {

constexpr float kElPi = 3.14159265358979323846f;
constexpr int   kNumModes = 8;

// ─── Resonator model frequency ratios ────────────────────────────────────────
// Each entry is a set of 8 partial frequency ratios (relative to fundamental).

// Circular membrane: Bessel-function zeros j_{m,n} / j_{0,1}
constexpr std::array<float, kNumModes> kMembraneRatios = {
    1.000f, 1.593f, 2.136f, 2.296f, 2.653f, 2.917f, 3.156f, 3.501f
};

// Plate: inharmonic flat-plate partials (approximated)
constexpr std::array<float, kNumModes> kPlateRatios = {
    1.000f, 1.414f, 1.914f, 2.296f, 2.830f, 3.390f, 4.035f, 4.634f
};

// Free bar (marimba / xylophone):  (2n-1)^2 / 1^2  (n=1..8)
constexpr std::array<float, kNumModes> kBarRatios = {
    1.000f, 2.756f, 5.404f, 8.933f, 13.344f, 18.640f, 24.820f, 31.890f
};

// Inharmonic bell partials (handbell / tubular bell approximation)
constexpr std::array<float, kNumModes> kBellRatios = {
    1.000f, 2.000f, 3.173f, 4.523f, 5.999f, 7.556f, 9.173f, 10.850f
};

// Harmonic string: f_n = n * f_1
constexpr std::array<float, kNumModes> kStringRatios = {
    1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

// Closed tube: odd harmonics only
constexpr std::array<float, kNumModes> kTubeRatios = {
    1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f, 13.0f, 15.0f
};

const std::array<float, kNumModes>& ratios_for_model(ElementsModel m) {
    switch (m) {
        case ElementsModel::Membrane: return kMembraneRatios;
        case ElementsModel::Plate:    return kPlateRatios;
        case ElementsModel::Bar:      return kBarRatios;
        case ElementsModel::Bell:     return kBellRatios;
        case ElementsModel::String:   return kStringRatios;
        case ElementsModel::Tube:     return kTubeRatios;
        default:                      return kMembraneRatios;
    }
}

// ─── Biquad resonator (RBJ bandpass, 0 dB peak gain) ─────────────────────────

struct BpCoeffs {
    float b0 = 0.0f;   // b1=0, b2=-b0
    float a1 = 0.0f;
    float a2 = 0.0f;
};

struct BpState {
    float z1 = 0.0f;
    float z2 = 0.0f;
};

// Build RBJ bandpass coefficients (0 dB peak gain at centre).
BpCoeffs make_bandpass(float freq_hz, float q, float sample_rate) {
    const float sr   = std::max(1.0f, sample_rate);
    const float fc   = std::clamp(freq_hz, 1.0f, sr * 0.499f);
    const float qv   = std::max(0.1f, q);
    const float w0   = 2.0f * kElPi * fc / sr;
    const float sw   = std::sin(w0);
    const float cw   = std::cos(w0);
    const float alpha = sw / (2.0f * qv);
    const float a0   = 1.0f + alpha;
    BpCoeffs c;
    c.b0 = alpha / a0;          // b1 = 0, b2 = -b0
    c.a1 = (-2.0f * cw) / a0;
    c.a2 = (1.0f - alpha) / a0;
    return c;
}

// Direct-form-2 transposed tick (b1=0, b2=-b0).
inline float tick_bp(float x, const BpCoeffs& c, BpState& s) {
    const float y = c.b0 * x + s.z1;
    s.z1 = -c.a1 * y + s.z2;
    s.z2 = -c.b0 * x - c.a2 * y;
    return y;
}

// ─── Amplitude envelope helpers ───────────────────────────────────────────────

inline float exp_decay(float t, float rate) {
    if (rate <= 0.0f) return 1.0f;
    return std::exp(-t * rate);
}

inline float attack_ramp(float t, float rate) {
    if (rate <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-t * rate * 5.0f);
}

}  // namespace

// ─── Public synthesizer ───────────────────────────────────────────────────────

std::vector<std::uint8_t> synthesize_elements(int sample_rate,
                                              std::size_t num_samples,
                                              const ElementsParams& params,
                                              std::mt19937& rng) {
    if (num_samples == 0 || sample_rate <= 0) {
        return {};
    }

    const float sr = static_cast<float>(sample_rate);
    const auto& ratios = ratios_for_model(params.model);

    // ── Per-mode amplitude weights ────────────────────────────────────────────
    // Combines three independent controls:
    //   position  : struck position (0=centre suppresses high modes, 1=edge amplifies them)
    //   brightness: spectral tilt  (0=dark rolloff, 1=bright boost)
    //   implicit  : modes normalised so peak output is roughly unity

    std::array<float, kNumModes> mode_amp{};
    float weight_sum = 0.0f;
    for (int n = 0; n < kNumModes; ++n) {
        const float ratio = ratios[static_cast<std::size_t>(n)];
        // Position: high modes suppressed when striking near centre.
        const float pos_w = 1.0f / (1.0f + (1.0f - params.position) * std::sqrt(ratio) * 0.4f);
        // Brightness: spectral tilt.
        const float br_w  = std::pow(ratio, params.brightness * 2.0f - 1.0f);
        mode_amp[n] = pos_w * br_w;
        weight_sum += mode_amp[n];
    }
    if (weight_sum > 0.0f) {
        const float inv = 1.0f / weight_sum;
        for (int n = 0; n < kNumModes; ++n) mode_amp[n] *= inv;
    }

    // ── Per-mode Q factors ────────────────────────────────────────────────────
    // Q_base falls from ~200 (damping=0, very resonant) to ~2 (damping=1, dry).
    // Higher partials damp faster (∝ 1/√ratio), matching physical behaviour.
    const float q_base = 2.0f + (1.0f - std::clamp(params.damping, 0.0f, 1.0f)) *
                                (1.0f - std::clamp(params.damping, 0.0f, 1.0f)) * 198.0f;

    // ── Build resonators ──────────────────────────────────────────────────────
    const float base_freq = std::clamp(params.frequency_hz, 10.0f, sr * 0.4f);
    std::array<BpCoeffs, kNumModes> coeffs;
    std::array<BpState,  kNumModes> states{};
    for (int n = 0; n < kNumModes; ++n) {
        const float ratio = ratios[static_cast<std::size_t>(n)];
        const float freq  = std::min(base_freq * ratio, sr * 0.499f);
        const float q_n   = std::max(0.5f, q_base / std::sqrt(ratio));
        coeffs[n] = make_bandpass(freq, q_n, sr);
    }

    // ── Exciter parameters ────────────────────────────────────────────────────
    const float sigma      = std::max(1.0f / sr, params.exciter_dur_s / 3.0f);
    const float noise_amt  = std::clamp(params.exciter_noise, 0.0f, 1.0f);
    const float clean_amt  = 1.0f - noise_amt;
    const float ex_level   = std::clamp(params.exciter_level, 0.0f, 4.0f);

    // Uniform noise helper ([-1,+1])
    std::uniform_real_distribution<float> noise_dist(-1.0f, 1.0f);

    // ── Synthesis loop ────────────────────────────────────────────────────────
    std::vector<float> output(num_samples, 0.0f);
    float peak = 0.0f;

    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / sr;

        // Exciter: Gaussian pulse (± noise blend)
        const float pulse_env = std::exp(-0.5f * (t * t) / (sigma * sigma));
        const float exciter   = ex_level *
            (clean_amt * pulse_env + noise_amt * noise_dist(rng) * pulse_env);

        // Drive all modes and sum weighted outputs
        float mix = 0.0f;
        for (int n = 0; n < kNumModes; ++n) {
            mix += mode_amp[n] * tick_bp(exciter, coeffs[n], states[n]);
        }

        // Output amplitude envelope (optional extra shaping on top of resonator decay)
        float amp = 1.0f;
        if (params.env_decay_rate > 0.0f) {
            amp *= exp_decay(t, params.env_decay_rate);
        }
        if (params.env_attack_rate > 0.0f) {
            amp *= attack_ramp(t, params.env_attack_rate);
        }

        output[i] = mix * amp;
        const float abs_val = std::abs(output[i]);
        if (abs_val > peak) peak = abs_val;
    }

    // ── Normalise and convert to signed-8-bit in uint8 ────────────────────────
    const float norm = (peak > 1e-6f) ? (1.0f / peak) : 1.0f;
    std::vector<std::uint8_t> result(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float s = std::clamp(output[i] * norm, -1.0f, 1.0f);
        const int q   = static_cast<int>(std::round(s * 127.0f));
        result[i]     = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
    }
    return result;
}

}  // namespace drumrom::synth
