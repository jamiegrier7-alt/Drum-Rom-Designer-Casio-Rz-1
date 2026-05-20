#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace drumrom::synth {

enum class EnvelopeShape {
    Exponential,
    Linear,
    Logarithmic
};

struct FmToneParams {
    float mod_freq_hz = 120.0f;
    float mod_freq_end_hz = 120.0f;
    float mod_pitch_decay_rate = 0.0f;
    EnvelopeShape mod_pitch_env_shape = EnvelopeShape::Exponential;
    float mod_index = 1.0f;
    float mod_index_end = 0.2f;
    float mod_index_decay_rate = 8.0f;
    EnvelopeShape mod_index_env_shape = EnvelopeShape::Exponential;
    float amp_osc_hz = 0.0f;
    float amp_osc_end_hz = 0.0f;
    float amp_osc_pitch_decay_rate = 0.0f;
    EnvelopeShape amp_osc_pitch_env_shape = EnvelopeShape::Exponential;
    float amp_osc_depth = 0.0f;
    float amp_osc_depth_end = 0.0f;
    float amp_osc_depth_decay_rate = 0.0f;
    EnvelopeShape amp_osc_depth_env_shape = EnvelopeShape::Exponential;
};

// Core DSP utilities
// ==================

// Convert float signal [-1.0, +1.0] to signed 8-bit PCM [-128, +127]
std::vector<std::uint8_t> to_u8(const std::vector<float>& signal, float gain = 1.0f);

// Convert float signal to signed 8-bit PCM with triangular dithering to reduce quantization noise
std::vector<std::uint8_t> to_u8_dithered(const std::vector<float>& signal, float gain = 1.0f, std::mt19937* rng = nullptr);

// Synthesized drum sounds (specify length in samples for precision)
// ================================================================

// Kick drum: deep bass with pitch bend decay
// num_samples: exact sample count (e.g., 20000 samples @ 20kHz = 1 second)
std::vector<std::uint8_t> synthesize_kick(int sample_rate, std::size_t num_samples, std::mt19937& rng);

// Snare drum: tonal attack + noise tail
std::vector<std::uint8_t> synthesize_snare(int sample_rate, std::size_t num_samples, std::mt19937& rng);

// Hi-hat: 6-square metallic oscillator bank mixed with noise
std::vector<std::uint8_t> synthesize_hihat(int sample_rate, std::size_t num_samples, std::mt19937& rng);

// Tom drum: mid-range tonal drum with pitch sweep
std::vector<std::uint8_t> synthesize_tom(int sample_rate, std::size_t num_samples, std::mt19937& rng);

// Clap: multi-burst noise with fast envelope
std::vector<std::uint8_t> synthesize_clap(int sample_rate, std::size_t num_samples, std::mt19937& rng);

// Utility: Fill slot with sine wave (for padding missing samples)
// This is typically 50 Hz sine as a silent filler
std::vector<std::uint8_t> generate_sine(std::size_t slot_size, int sample_rate, float freq_hz = 50.0f);

// Slot processing
// ===============

// Fit raw sample to slot: truncate if too large, pad if too small
// Applies adaptive exponential decay fade on truncation (25% of slot size)
std::vector<std::uint8_t> fit_slot(const std::vector<std::uint8_t>& raw, std::size_t slot_size, std::uint8_t fill_byte, int sample_rate);

// Parameters for drum synthesis (useful for creating variants)
// =============================================================

struct DrumParams {
    int sample_rate = 20000;

    // Kick
    struct {
        float duration_s = 0.22f;
        float pitch_start_hz = 140.0f;
        float pitch_end_hz = 36.0f;
        float pitch_decay_rate = 14.0f;
        EnvelopeShape pitch_env_shape = EnvelopeShape::Exponential;
        float env_decay_rate = 14.0f;  // Increased from 10.0f for faster decay
        float attack_rate = 0.0f;
        EnvelopeShape amp_decay_shape = EnvelopeShape::Exponential;
        EnvelopeShape amp_attack_shape = EnvelopeShape::Linear;
        float tone_decay_rate = 0.0f;
        EnvelopeShape tone_env_shape = EnvelopeShape::Exponential;
        FmToneParams fm;
    } kick;
    
    // Snare
    struct {
        float duration_s = 0.18f;
        float tone_freq_hz = 180.0f;
        float tone_freq_end_hz = 180.0f;
        float pitch_decay_rate = 0.0f;
        EnvelopeShape pitch_env_shape = EnvelopeShape::Exponential;
        float tone_decay_rate = 18.0f;  // Increased from 15.0f for faster decay
        EnvelopeShape tone_env_shape = EnvelopeShape::Exponential;
        float noise_decay_rate = 24.0f;  // Increased from 20.0f for faster decay
        float tone_mix = 0.35f;
        float noise_mix = 0.85f;
        float attack_rate = 0.0f;
        EnvelopeShape amp_attack_shape = EnvelopeShape::Linear;
        float amp_decay_rate = 0.0f;
        EnvelopeShape amp_decay_shape = EnvelopeShape::Exponential;
        FmToneParams fm;
    } snare;
    
    // Hi-hat
    struct {
        float duration_s = 0.09f;
        float tone_freq_hz = 340.0f;
        float tone_freq_end_hz = 340.0f;
        std::array<float, 6> square_ratios = {1.00f, 1.31f, 1.52f, 1.79f, 2.13f, 2.47f};
        float pitch_decay_rate = 0.0f;
        EnvelopeShape pitch_env_shape = EnvelopeShape::Exponential;
        float tone_mix = 0.20f;
        float hp_cutoff_hz = 4200.0f;
        float hp_resonance = 0.75f;
        float tone_decay_rate = 0.0f;
        EnvelopeShape tone_env_shape = EnvelopeShape::Exponential;
        float decay_rate = 50.0f;  // Increased from 40.0f for faster decay
        EnvelopeShape amp_decay_shape = EnvelopeShape::Exponential;
        float attack_rate = 0.0f;
        EnvelopeShape amp_attack_shape = EnvelopeShape::Linear;
        FmToneParams fm;
    } hihat;
    
    // Tom
    struct {
        float duration_s = 0.20f;
        float pitch_start_hz = 110.0f;
        float pitch_end_hz = 70.0f;
        float pitch_decay_rate = 4.0f;
        EnvelopeShape pitch_env_shape = EnvelopeShape::Exponential;
        float env_decay_rate = 12.0f;  // Increased from 9.0f for faster decay
        EnvelopeShape amp_decay_shape = EnvelopeShape::Exponential;
        float attack_rate = 0.0f;
        EnvelopeShape amp_attack_shape = EnvelopeShape::Linear;
        float tone_decay_rate = 0.0f;
        EnvelopeShape tone_env_shape = EnvelopeShape::Exponential;
        FmToneParams fm;
    } tom;
    
    // Clap
    struct {
        float tone_freq_hz = 1100.0f;
        float tone_freq_end_hz = 1100.0f;
        float pitch_decay_rate = 0.0f;
        EnvelopeShape pitch_env_shape = EnvelopeShape::Exponential;
        float click_rate = 1.0f;
        float tone_mix = 0.15f;
        float tone_decay_rate = 0.0f;
        EnvelopeShape tone_env_shape = EnvelopeShape::Exponential;
        float duration_s = 0.16f;
        float env_decay_rate = 20.0f;  // Increased from 18.0f for faster decay
        EnvelopeShape amp_decay_shape = EnvelopeShape::Exponential;
        float attack_rate = 0.0f;
        EnvelopeShape amp_attack_shape = EnvelopeShape::Linear;
        FmToneParams fm;
    } clap;
    
    // Reverb effect (applies to all synth and sample voices)
    struct {
        float enabled = 0.0f;         // 0.0 = off, 1.0 = on
        float decay_time_ms = 200.0f; // 10.0 to 2000.0 ms - total reverb tail length
        float damping = 0.5f;         // 0.0 to 1.0 - highpass damping (darker = more)
        float width = 1.0f;           // 0.0 to 1.0 (stereo width, unused for mono)
        float early_level = 0.35f;    // 0.0 to 1.0 - early reflection amount
        float early_spread = 1.0f;    // 0.5 to 2.0 - early reflection spacing
        float diffusion = 0.5f;       // 0.0 to 1.0 - late reverb density
        float tone = 0.75f;           // 0.0 to 1.0 - dark to bright wet tone
        float late_mix = 0.65f;       // 0.0 to 1.0 - tail vs early balance
        float size = 1.0f;            // 0.5 to 1.5 - room size scaling
        float decay_shape = 0.5f;     // 0.0 to 1.0 - abrupt to smooth tail curve
        float wet_level = 0.3f;       // 0.0 to 1.0
        float dry_level = 0.7f;       // 0.0 to 1.0
        float pre_delay_ms = 0.0f;    // 0.0 to 50.0 ms
    } reverb;
};

// Advanced synthesis with custom parameters (sample count for precision)
std::vector<std::uint8_t> synthesize_kick_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples);
std::vector<std::uint8_t> synthesize_snare_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples);
std::vector<std::uint8_t> synthesize_hihat_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples);
std::vector<std::uint8_t> synthesize_tom_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples);
std::vector<std::uint8_t> synthesize_clap_custom(const DrumParams& params, std::mt19937& rng, std::size_t num_samples);

}  // namespace drumrom::synth
