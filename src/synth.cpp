// Core drum/sample synthesis engine and shared DSP utility functions.
#include "drumrom/synth.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <random>

namespace drumrom::synth {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

struct BiquadState {
    float z1 = 0.0f;
    float z2 = 0.0f;
};

Biquad make_highpass_biquad(float sample_rate, float cutoff_hz, float q) {
    const float sr = std::max(1.0f, sample_rate);
    const float fc = std::clamp(cutoff_hz, 20.0f, (sr * 0.49f));
    const float qv = std::max(0.1f, q);

    const float w0 = (2.0f * kPi * fc) / sr;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * qv);

    const float b0 = (1.0f + cw) * 0.5f;
    const float b1 = -(1.0f + cw);
    const float b2 = (1.0f + cw) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cw;
    const float a2 = 1.0f - alpha;

    Biquad out;
    out.b0 = b0 / a0;
    out.b1 = b1 / a0;
    out.b2 = b2 / a0;
    out.a1 = a1 / a0;
    out.a2 = a2 / a0;
    return out;
}

float process_biquad_df2t(float x, const Biquad& c, BiquadState& st) {
    const float y = (c.b0 * x) + st.z1;
    st.z1 = (c.b1 * x) - (c.a1 * y) + st.z2;
    st.z2 = (c.b2 * x) - (c.a2 * y);
    return y;
}

float clamp01(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

float attack_gain(float t, float attack_rate, EnvelopeShape shape) {
    if (attack_rate <= 0.0f) {
        return 1.0f;
    }
    const float x = clamp01(t * attack_rate);
    if (shape == EnvelopeShape::Linear) {
        return x;
    }
    if (shape == EnvelopeShape::Logarithmic) {
        return std::log(1.0f + 9.0f * x) / std::log(10.0f);
    }
    return 1.0f - std::exp(-5.0f * x);
}

float decay_gain(float t, float decay_rate, EnvelopeShape shape) {
    if (decay_rate <= 0.0f) {
        return 1.0f;
    }
    if (shape == EnvelopeShape::Linear) {
        return std::max(0.0f, 1.0f - (t * decay_rate));
    }
    if (shape == EnvelopeShape::Logarithmic) {
        return 1.0f / (1.0f + t * decay_rate * 2.0f);
    }
    return std::exp(-t * decay_rate);
}

float sweep_pitch(float start_hz, float end_hz, float t, float sweep_rate, EnvelopeShape shape) {
    if (sweep_rate <= 0.0f) {
        return start_hz;
    }
    if (shape == EnvelopeShape::Linear) {
        const float x = clamp01(t * sweep_rate);
        return start_hz + ((end_hz - start_hz) * x);
    }
    if (shape == EnvelopeShape::Logarithmic) {
        const float x = clamp01(std::log(1.0f + 9.0f * clamp01(t * sweep_rate)) / std::log(10.0f));
        return start_hz + ((end_hz - start_hz) * x);
    }
    const float x = 1.0f - std::exp(-t * sweep_rate);
    return start_hz + ((end_hz - start_hz) * x);
}

float fm_mod_signal(const FmToneParams& fm, float t) {
    const float mod_freq = sweep_pitch(
        fm.mod_freq_hz,
        fm.mod_freq_end_hz,
        t,
        fm.mod_pitch_decay_rate,
        fm.mod_pitch_env_shape);
    const float mod_index = sweep_pitch(
        fm.mod_index,
        fm.mod_index_end,
        t,
        fm.mod_index_decay_rate,
        fm.mod_index_env_shape);
    return mod_index * std::sin(2.0f * kPi * mod_freq * t);
}

float am_osc_gain(const FmToneParams& fm, float t) {
    const float depth = clamp01(sweep_pitch(
        fm.amp_osc_depth,
        fm.amp_osc_depth_end,
        t,
        fm.amp_osc_depth_decay_rate,
        fm.amp_osc_depth_env_shape));
    if (depth <= 0.0001f) {
        return 1.0f;
    }
    const float amp_osc_freq = sweep_pitch(
        fm.amp_osc_hz,
        fm.amp_osc_end_hz,
        t,
        fm.amp_osc_pitch_decay_rate,
        fm.amp_osc_pitch_env_shape);
    const float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * amp_osc_freq * t));
    return (1.0f - depth) + (depth * lfo);
}

float fm_carrier_sample(
    float carrier_start_hz,
    float carrier_end_hz,
    float carrier_pitch_decay_rate,
    EnvelopeShape carrier_pitch_shape,
    const FmToneParams& fm,
    float t) {
    const float carrier_freq = sweep_pitch(
        carrier_start_hz,
        carrier_end_hz,
        t,
        carrier_pitch_decay_rate,
        carrier_pitch_shape);
    const float phase = (2.0f * kPi * carrier_freq * t) + fm_mod_signal(fm, t);
    return std::sin(phase) * am_osc_gain(fm, t);
}

void apply_tail_fade(std::vector<float>& signal, std::size_t fade_len) {
    if (signal.empty() || fade_len == 0) {
        return;
    }
    fade_len = std::min(fade_len, signal.size());
    const std::size_t start = signal.size() - fade_len;
    for (std::size_t i = 0; i < fade_len; ++i) {
        const float x = (fade_len > 1)
            ? static_cast<float>(i) / static_cast<float>(fade_len - 1)
            : 1.0f;
        // Raised-cosine fade from 1.0 down to 0.0 to avoid hard stop clicks.
        const float k = 0.5f * (1.0f + std::cos(kPi * x));
        signal[start + i] *= k;
    }
}

void apply_head_fade(std::vector<float>& signal, std::size_t fade_len) {
    if (signal.empty() || fade_len == 0) {
        return;
    }
    fade_len = std::min(fade_len, signal.size());
    for (std::size_t i = 0; i < fade_len; ++i) {
        const float x = (fade_len > 1)
            ? static_cast<float>(i) / static_cast<float>(fade_len - 1)
            : 1.0f;
        // Raised-cosine fade from 0.0 up to 1.0 to avoid start-click discontinuities.
        const float k = 0.5f * (1.0f - std::cos(kPi * x));
        signal[i] *= k;
    }
}

void normalize_signal(std::vector<float>& signal) {
    if (signal.empty()) {
        return;
    }
    // Find peak amplitude
    float peak = 0.0f;
    for (float s : signal) {
        peak = std::max(peak, std::abs(s));
    }
    
    // Scale to use full dynamic range, with headroom to avoid clipping after dithering
    if (peak > 0.001f) {
        const float scale = 0.95f / peak;  // 0.95 provides headroom for dithering
        for (float& s : signal) {
            s *= scale;
        }
    }
}
}

// Core DSP utilities
// ==================

std::vector<std::uint8_t> to_u8(const std::vector<float>& signal, float gain) {
    std::vector<std::uint8_t> out;
    out.reserve(signal.size());
    for (float s : signal) {
        // Map float [-1.0, +1.0] to signed 8-bit [-128, +127]
        const float v = s * gain * 127.0f;
        const int iv = static_cast<int>(v);
        out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(iv, -128, 127))));
    }
    return out;
}

std::vector<std::uint8_t> to_u8_dithered(const std::vector<float>& signal, float gain, std::mt19937* rng) {
    std::vector<std::uint8_t> out;
    out.reserve(signal.size());
    std::uniform_real_distribution<float> dither_dist(-0.5f, 0.5f);
    std::mt19937 local_rng(0xDEADBEEFU);
    std::mt19937* rng_ptr = rng ? rng : &local_rng;
    const std::size_t dither_guard_samples = std::min<std::size_t>(32, signal.size());
    const std::size_t dither_stop_index = signal.size() - dither_guard_samples;
    
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const float s = signal[i];
        // Triangular dither: sum of two uniform distributions
        // Disable dither in the final guard region to guarantee a silent tail
        // after fades, preventing end-boundary clicks on strict hardware players.
        const float dither = (i >= dither_stop_index)
            ? 0.0f
            : (dither_dist(*rng_ptr) + dither_dist(*rng_ptr));
        const float v = (s * gain * 127.0f) + dither;
        const int iv = static_cast<int>(v);
        out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(iv, -128, 127))));
    }
    return out;
}

// Synthesized drum sounds (using default parameters)
// ===================================================

std::vector<std::uint8_t> synthesize_kick(int sample_rate, std::size_t num_samples, std::mt19937& rng) {
    DrumParams params;
    params.sample_rate = sample_rate;
    return synthesize_kick_custom(params, rng, num_samples);
}

std::vector<std::uint8_t> synthesize_snare(int sample_rate, std::size_t num_samples, std::mt19937& rng) {
    DrumParams params;
    params.sample_rate = sample_rate;
    return synthesize_snare_custom(params, rng, num_samples);
}

std::vector<std::uint8_t> synthesize_hihat(int sample_rate, std::size_t num_samples, std::mt19937& rng) {
    DrumParams params;
    params.sample_rate = sample_rate;
    return synthesize_hihat_custom(params, rng, num_samples);
}

std::vector<std::uint8_t> synthesize_tom(int sample_rate, std::size_t num_samples, std::mt19937& rng) {
    DrumParams params;
    params.sample_rate = sample_rate;
    return synthesize_tom_custom(params, rng, num_samples);
}

std::vector<std::uint8_t> synthesize_clap(int sample_rate, std::size_t num_samples, std::mt19937& rng) {
    DrumParams params;
    params.sample_rate = sample_rate;
    return synthesize_clap_custom(params, rng, num_samples);
}

// Advanced synthesis with custom parameters
// ==========================================

std::vector<std::uint8_t> synthesize_kick_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples) {
    std::vector<float> sig;
    sig.reserve(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(params.sample_rate);
        const float attack = attack_gain(t, params.kick.attack_rate, params.kick.amp_attack_shape);
        const float amp = decay_gain(t, params.kick.env_decay_rate, params.kick.amp_decay_shape) * attack;
        const float tone = decay_gain(t, params.kick.tone_decay_rate, params.kick.tone_env_shape);
        const float body = fm_carrier_sample(
            params.kick.pitch_start_hz,
            params.kick.pitch_end_hz,
            params.kick.pitch_decay_rate,
            params.kick.pitch_env_shape,
            params.kick.fm,
            t);
        sig.push_back(body * amp * tone);
    }

    // Normalize to maintain maximum dynamic range
    normalize_signal(sig);

    // Apply very short de-click head fade (0.4 ms @ 20 kHz) without audible soft attack.
    const std::size_t kick_head_fade = std::min<std::size_t>(8, num_samples);
    apply_head_fade(sig, kick_head_fade);

    // Apply proportional tail fade: 25% of sound length, with 8-sample minimum.
    // This ensures no clicks regardless of sound length (short or long).
    const std::size_t kick_tail_fade = std::max<std::size_t>(8, num_samples / 4);
    apply_tail_fade(sig, kick_tail_fade);

    return to_u8_dithered(sig, 1.0f, &const_cast<std::mt19937&>(rng));
}

std::vector<std::uint8_t> synthesize_snare_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples) {
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> sig;
    sig.reserve(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(params.sample_rate);
        const float attack = attack_gain(t, params.snare.attack_rate, params.snare.amp_attack_shape);
        const float amp = decay_gain(t, params.snare.amp_decay_rate, params.snare.amp_decay_shape) * attack;
        const float noise_env = std::exp(-t * params.snare.noise_decay_rate);
        const float tone_env = decay_gain(t, params.snare.tone_decay_rate, params.snare.tone_env_shape);
        const float tone = fm_carrier_sample(
            params.snare.tone_freq_hz,
            params.snare.tone_freq_end_hz,
            params.snare.pitch_decay_rate,
            params.snare.pitch_env_shape,
            params.snare.fm,
            t) * tone_env;
        sig.push_back(((params.snare.tone_mix * tone) + (params.snare.noise_mix * noise(const_cast<std::mt19937&>(rng)) * noise_env)) * amp);
    }

    // Normalize to maintain maximum dynamic range
    normalize_signal(sig);

    // Apply very short de-click head fade (0.4 ms @ 20 kHz) without audible soft attack.
    const std::size_t snare_head_fade = std::min<std::size_t>(8, num_samples);
    apply_head_fade(sig, snare_head_fade);

    // Apply proportional tail fade: 25% of sound length, with 8-sample minimum.
    const std::size_t snare_tail_fade = std::max<std::size_t>(8, num_samples / 4);
    apply_tail_fade(sig, snare_tail_fade);

    return to_u8_dithered(sig, 1.0f, &const_cast<std::mt19937&>(rng));
}

std::vector<std::uint8_t> synthesize_hihat_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples) {
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    constexpr float kHihatNoiseScale = 0.25f;
    const Biquad hp = make_highpass_biquad(
        static_cast<float>(params.sample_rate),
        params.hihat.hp_cutoff_hz,
        params.hihat.hp_resonance);
    BiquadState hp_state;
    std::vector<float> sig;
    sig.reserve(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(params.sample_rate);
        const float attack = attack_gain(t, params.hihat.attack_rate, params.hihat.amp_attack_shape);
        const float env = decay_gain(t, params.hihat.decay_rate, params.hihat.amp_decay_shape) * attack;
        const float osc_base_freq = sweep_pitch(
            params.hihat.tone_freq_hz,
            params.hihat.tone_freq_end_hz,
            t,
            params.hihat.pitch_decay_rate,
            params.hihat.pitch_env_shape);
        const float tone_env = decay_gain(t, params.hihat.tone_decay_rate, params.hihat.tone_env_shape);
        const float mod_freq_base = sweep_pitch(
            params.hihat.fm.mod_freq_hz,
            params.hihat.fm.mod_freq_end_hz,
            t,
            params.hihat.fm.mod_pitch_decay_rate,
            params.hihat.fm.mod_pitch_env_shape);
        const float mod_index = sweep_pitch(
            params.hihat.fm.mod_index,
            params.hihat.fm.mod_index_end,
            t,
            params.hihat.fm.mod_index_decay_rate,
            params.hihat.fm.mod_index_env_shape);
        const float tone_am = am_osc_gain(params.hihat.fm, t);
        float osc_bank = 0.0f;
        for (float ratio : params.hihat.square_ratios) {
            const float phase =
                (2.0f * kPi * osc_base_freq * ratio * t) +
                (mod_index * std::sin(2.0f * kPi * mod_freq_base * ratio * t));
            osc_bank += (std::sin(phase) >= 0.0f) ? 1.0f : -1.0f;
        }
        osc_bank = (osc_bank / static_cast<float>(params.hihat.square_ratios.size())) * tone_env * tone_am;
        const float tone_mix = std::clamp(params.hihat.tone_mix, 0.0f, 1.0f);
        const float shaped = ((1.0f - tone_mix) * noise(const_cast<std::mt19937&>(rng)) * kHihatNoiseScale) + (tone_mix * osc_bank);
        const float filtered = process_biquad_df2t(shaped, hp, hp_state);
        sig.push_back(filtered * env);
    }

    // Normalize to maintain maximum dynamic range
    normalize_signal(sig);

    // Apply very short de-click head fade (0.4 ms @ 20 kHz) without audible soft attack.
    const std::size_t hihat_head_fade = std::min<std::size_t>(8, num_samples);
    apply_head_fade(sig, hihat_head_fade);

    // Apply proportional tail fade: 25% of sound length, with 8-sample minimum.
    const std::size_t hihat_tail_fade = std::max<std::size_t>(8, num_samples / 4);
    apply_tail_fade(sig, hihat_tail_fade);

    return to_u8_dithered(sig, 1.0f, &const_cast<std::mt19937&>(rng));
}

std::vector<std::uint8_t> synthesize_tom_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples) {
    std::vector<float> sig;
    sig.reserve(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(params.sample_rate);
        const float attack = attack_gain(t, params.tom.attack_rate, params.tom.amp_attack_shape);
        const float amp = decay_gain(t, params.tom.env_decay_rate, params.tom.amp_decay_shape) * attack;
        const float tone = decay_gain(t, params.tom.tone_decay_rate, params.tom.tone_env_shape);
        const float body = fm_carrier_sample(
            params.tom.pitch_start_hz,
            params.tom.pitch_end_hz,
            params.tom.pitch_decay_rate,
            params.tom.pitch_env_shape,
            params.tom.fm,
            t);
        sig.push_back(body * amp * tone);
    }

    // Normalize to maintain maximum dynamic range
    normalize_signal(sig);

    // Apply very short de-click head fade (0.4 ms @ 20 kHz) without audible soft attack.
    const std::size_t tom_head_fade = std::min<std::size_t>(8, num_samples);
    apply_head_fade(sig, tom_head_fade);

    // Apply proportional tail fade: 25% of sound length, with 8-sample minimum.
    const std::size_t tom_tail_fade = std::max<std::size_t>(8, num_samples / 4);
    apply_tail_fade(sig, tom_tail_fade);

    return to_u8_dithered(sig, 1.0f, &const_cast<std::mt19937&>(rng));
}

std::vector<std::uint8_t> synthesize_clap_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples) {
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> sig;
    sig.reserve(num_samples);
    const float click_rate = std::clamp(params.clap.click_rate, 0.25f, 8.0f);
    const float c0 = 0.010f;
    const float c1 = c0 + (0.012f / click_rate);
    const float c2 = c1 + (0.014f / click_rate);
    const float sharp = click_rate;
    for (std::size_t i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(params.sample_rate);
        const float attack = attack_gain(t, params.clap.attack_rate, params.clap.amp_attack_shape);
        const float burst =
            std::exp(-std::pow((t - c0) * (450.0f * sharp), 2.0f)) +
            std::exp(-std::pow((t - c1) * (400.0f * sharp), 2.0f)) +
            std::exp(-std::pow((t - c2) * (360.0f * sharp), 2.0f));
        const float tail = decay_gain(t, params.clap.env_decay_rate, params.clap.amp_decay_shape);
        const float tone = fm_carrier_sample(
            params.clap.tone_freq_hz,
            params.clap.tone_freq_end_hz,
            params.clap.pitch_decay_rate,
            params.clap.pitch_env_shape,
            params.clap.fm,
            t) * decay_gain(t, params.clap.tone_decay_rate, params.clap.tone_env_shape);
        const float tone_mix = std::clamp(params.clap.tone_mix, 0.0f, 1.0f);
        const float envelope = std::min(1.0f, burst + (0.4f * tail)) * attack;
        const float shaped = ((1.0f - tone_mix) * noise(const_cast<std::mt19937&>(rng))) + (tone_mix * tone);
        sig.push_back(shaped * envelope);
    }

    // Normalize to maintain maximum dynamic range
    normalize_signal(sig);

    // Apply very short de-click head fade (0.4 ms @ 20 kHz) without audible soft attack.
    const std::size_t clap_head_fade = std::min<std::size_t>(8, num_samples);
    apply_head_fade(sig, clap_head_fade);

    // Apply proportional tail fade: 25% of sound length, with 8-sample minimum.
    const std::size_t clap_tail_fade = std::max<std::size_t>(8, num_samples / 4);
    apply_tail_fade(sig, clap_tail_fade);

    return to_u8_dithered(sig, 1.0f, &const_cast<std::mt19937&>(rng));
}

// Utility: Fill slot with sine wave
// ==================================

std::vector<std::uint8_t> generate_sine(std::size_t slot_size, int sample_rate, float freq_hz) {
    std::vector<std::uint8_t> out;
    out.reserve(slot_size);
    const float sr = static_cast<float>(sample_rate);
    std::mt19937 rng(0xFEEDBEEFU);
    std::uniform_real_distribution<float> dither_dist(-0.5f, 0.5f);
    
    for (std::size_t i = 0; i < slot_size; ++i) {
        const float t = static_cast<float>(i) / sr;
        // Soft attack to prevent clicks at slot start
        const float attack = std::min(1.0f, t * 20.0f);
        // Exponential fade on tail to prevent ringing
        const float tail_fade = std::exp(-t * 0.5f);
        const float s = std::sin(2.0f * kPi * freq_hz * t) * attack * tail_fade;
        // Apply triangular dither
        const float dither = dither_dist(rng) + dither_dist(rng);
        const int v = static_cast<int>(s * 127.0f + dither);
        out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(v, -128, 127))));
    }
    return out;
}

// Slot processing
// ===============

std::vector<std::uint8_t> fit_slot(const std::vector<std::uint8_t>& raw, std::size_t slot_size, std::uint8_t fill_byte, int sample_rate) {
    std::vector<std::uint8_t> out(slot_size, fill_byte);
    const std::size_t n = std::min(slot_size, raw.size());
    std::copy(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(n), out.begin());

    // If the source must be truncated, apply proportional exponential decay to reduce ringing.
    // Fade duration is 25% of slot size (ensures fade fits even on short samples).
    if (raw.size() > slot_size && slot_size > 1) {
        const std::size_t fade_len = std::max<std::size_t>(1, slot_size / 4);  // 25% of slot
        const std::size_t fade_start = (slot_size > fade_len) ? (slot_size - fade_len) : 0;
        for (std::size_t i = 0; i < fade_len; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(fade_len);
            // Exponential decay for natural audio tail
            const float k = std::exp(-t * t * 10.0f);
            const std::int8_t sample_val = static_cast<std::int8_t>(out[fade_start + i]);
            const float v = (static_cast<float>(sample_val) * k);
            out[fade_start + i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(static_cast<int>(v), -128, 127)));
        }
    }

    // Boundary guards: some hardware appears to read one sample across slot boundaries.
    // Force silence at both edges to avoid clicks from off-by-one playback behavior.
    if (!out.empty()) {
        out.front() = static_cast<std::uint8_t>(0);
        out.back() = static_cast<std::uint8_t>(0);
    }
    return out;
}

}  // namespace drumrom::synth
