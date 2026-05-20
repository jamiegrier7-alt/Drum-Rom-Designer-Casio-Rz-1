// Freeverb reverb effect implementation (Schroeder reverberator architecture)
#include "drumrom/synth_freeverb.h"

#include <algorithm>
#include <cmath>

namespace drumrom::synth {

// Compact Freeverb buffer sizes optimized for short percussion reverbs
// These are scaled to produce fast reverb buildup and quick decay
// Original Freeverb was designed for long reverb tails, we want short ones
constexpr int kCombSizes[8] = {225, 240, 260, 280, 300, 320, 340, 360};
constexpr int kAllpassSizes[4] = {110, 100, 90, 80};

Freeverb::Freeverb() {
    reset();
}

void Freeverb::init(int sr) {
    sample_rate = sr;
    init_filter_sizes();
    reset();
}

void Freeverb::init_filter_sizes() {
    // Scale buffer sizes from 44100 Hz baseline to actual sample rate
    // These smaller sizes mean faster response and shorter reverb tail
    const float rate_scale = sample_rate / 44100.0f;
    
    for (int i = 0; i < 8; ++i) {
        const int scaled_size = std::max(16, static_cast<int>(kCombSizes[i] * rate_scale));
        combs[i].init(scaled_size);
    }
    
    for (int i = 0; i < 4; ++i) {
        const int scaled_size = std::max(8, static_cast<int>(kAllpassSizes[i] * rate_scale));
        allpasses[i].init(scaled_size);
    }
    
    // Pre-delay buffer: up to 50ms
    const int max_pre_delay_samples = std::max(512, static_cast<int>(sample_rate * 0.050f));
    pre_delay.init(max_pre_delay_samples);
}

void Freeverb::reset() {
    for (auto& c : combs) {
        c.reset();
    }
    for (auto& a : allpasses) {
        a.reset();
    }
    pre_delay.reset();
}

void Freeverb::set_params(const ReverbParams& params) {
    current_params = params;
    
    // Calculate feedback from decay time
    // Longer decay_time = higher feedback (more sustain)
    // Short decay_time = lower feedback (quick fade)
    const float decay_seconds = std::clamp(params.decay_time_ms * 0.001f, 0.01f, 2.0f);
    
    // Feedback calculation: f = decay_distance^(1/num_combs)
    // For a given decay time, we solve: signal_level = f^(delay_length / decay_samples)
    // This gives us the feedback needed for that decay time
    const float decay_in_samples = decay_seconds * sample_rate;
    const float size_scale = std::clamp(params.size, 0.5f, 1.5f);
    float avg_delay_samples = 0.0f;
    for (const auto& c : combs) {
        avg_delay_samples += static_cast<float>(c.buffer.size());
    }
    avg_delay_samples = (avg_delay_samples / static_cast<float>(combs.size())) * size_scale;
    
    // f such that f^(decay_samples / avg_delay) ≈ 0.001 (silence threshold)
    // Solving: f = 0.001^(avg_delay / decay_samples)
    const float silence_threshold = 0.001f;
    const float feedback = std::pow(silence_threshold, avg_delay_samples / std::max(decay_in_samples, 1.0f));
    
    for (auto& c : combs) {
        c.feedback = std::clamp(feedback, 0.05f, 0.98f);
    }
    
    // Enhanced damping: higher damping = more high-frequency loss
    update_damping(params.damping);
    
    // Allpass feedback is fixed
    const float diffusion = std::clamp(params.diffusion, 0.0f, 1.0f);
    const float allpass_feedback = std::clamp(0.20f + (diffusion * 0.75f), 0.20f, 0.95f);
    for (auto& a : allpasses) {
        a.feedback = allpass_feedback;
    }
    
    // Set pre-delay samples
    const int pre_delay_samples = static_cast<int>(params.pre_delay_ms * sample_rate * 0.001f);
    pre_delay.set_delay_samples(pre_delay_samples);
}

void Freeverb::update_damping(float damping) {
    // Enhanced damping control: 0 = bright, 1 = dark
    // Higher damping means more high-frequency rolloff (damp2 higher, damp1 lower)
    const float damp = std::clamp(damping, 0.0f, 1.0f);
    const float damp_amount = damp * 0.5f;
    
    for (auto& c : combs) {
        c.damp1 = damp_amount;
        c.damp2 = 1.0f - damp_amount;
    }
}

void Freeverb::process_sample(float input, float& left_out, float& right_out) {
    // Apply pre-delay
    const float delayed = pre_delay.process(input);
    
    // Feed parallel comb filters
    float comb_out = 0.0f;
    for (auto& c : combs) {
        comb_out += c.process(delayed);
    }
    comb_out /= static_cast<float>(combs.size());
    
    // Feed through series allpass filters
    float allpass_out = comb_out;
    for (auto& a : allpasses) {
        allpass_out = a.process(allpass_out);
    }
    
    // Simple stereo width control (mixed back to mono in caller)
    const float width = std::clamp(current_params.width, 0.0f, 1.0f);
    left_out = allpass_out;
    right_out = allpass_out * width;
}

std::pair<std::vector<float>, std::vector<float>> Freeverb::process(
    const std::vector<float>& input,
    const ReverbParams& params) {
    
    set_params(params);
    reset();  // Reset state for each note
    
    std::vector<float> left_out;
    std::vector<float> right_out;
    left_out.reserve(input.size());
    right_out.reserve(input.size());
    
    for (float sample : input) {
        float l, r;
        process_sample(sample, l, r);
        left_out.push_back(l);
        right_out.push_back(r);
    }
    
    return {left_out, right_out};
}

std::vector<float> apply_freeverb(
    const std::vector<float>& signal,
    const ReverbParams& params,
    int sample_rate) {
    
    // Convert input to float if needed and process through reverb
    Freeverb reverb;
    reverb.init(sample_rate);
    
    auto [wet_l, wet_r] = reverb.process(signal, params);
    
    // Mix wet and dry - normalize properly to avoid clipping
    std::vector<float> output(signal.size());
    const float wet_gain = params.wet_level;
    const float dry_gain = params.dry_level;
    const float decay_samples = std::max(1.0f, params.decay_time_ms * 0.001f * sample_rate);
    const float early_spread = std::clamp(params.early_spread, 0.5f, 2.0f);
    const int early_tap_1 = std::max(1, static_cast<int>((sample_rate * 0.0012f) * early_spread));
    const int early_tap_2 = std::max(2, static_cast<int>((sample_rate * 0.0028f) * early_spread));
    const int early_tap_3 = std::max(3, static_cast<int>((sample_rate * 0.0056f) * early_spread));
    const float early_level = std::clamp(params.early_level, 0.0f, 1.0f);
    const float late_mix = std::clamp(params.late_mix, 0.0f, 1.0f);
    const float decay_shape = std::clamp(params.decay_shape, 0.0f, 1.0f);
    const float decay_curve = 0.6f + decay_shape;
    const float tone = std::clamp(params.tone, 0.0f, 1.0f);
    const float wet_lowpass_hz = 500.0f + (tone * 9000.0f);
    const float lowpass_alpha = std::exp((-6.2831853f * wet_lowpass_hz) / static_cast<float>(sample_rate));
    float wet_lowpass_state = 0.0f;
    
    // Normalize: wet + dry should equal 1.0 for proper mixing
    const float total = std::max(0.01f, wet_gain + dry_gain);
    const float normalized_wet = wet_gain / total;
    const float normalized_dry = dry_gain / total;
    
    for (std::size_t i = 0; i < signal.size(); ++i) {
        float early = 0.0f;
        if (i >= static_cast<std::size_t>(early_tap_1)) {
            early += signal[i - early_tap_1] * 0.20f;
        }
        if (i >= static_cast<std::size_t>(early_tap_2)) {
            early += signal[i - early_tap_2] * 0.14f;
        }
        if (i >= static_cast<std::size_t>(early_tap_3)) {
            early += signal[i - early_tap_3] * 0.09f;
        }
        early *= early_level;

        const float t = static_cast<float>(i) / decay_samples;
        const float decay_envelope = std::exp(-6.9077554f * std::pow(t, decay_curve));
        const float late = (wet_l[i] + wet_r[i]) * 0.5f;
        const float wet_mono = (early * (1.0f - late_mix)) + (late * late_mix);
        const float wet_shaped = wet_mono * decay_envelope;
        wet_lowpass_state = ((1.0f - lowpass_alpha) * wet_shaped) + (lowpass_alpha * wet_lowpass_state);
        const float wet = wet_lowpass_state * normalized_wet;
        const float dry = signal[i] * normalized_dry;
        output[i] = wet + dry;
    }
    
    return output;
}

}  // namespace drumrom::synth

