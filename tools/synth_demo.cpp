// Simple drum synthesizer tool for testing and customization
// Compile: g++ -std=c++17 -I../include synth_demo.cpp ../src/synth.cpp -o synth_demo -lm
// Usage: ./synth_demo kick 20000 4400 kick_demo.raw [--randomize-params] [--seed N]
//        (drum_type can be: kick, snare, hihat, tom, clap)

#include "drumrom/synth.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

float rand_range(std::mt19937& rng, float min_v, float max_v) {
    std::uniform_real_distribution<float> dist(min_v, max_v);
    return dist(rng);
}

float clampf(float x, float min_v, float max_v) {
    return std::max(min_v, std::min(x, max_v));
}

bool rand_bool(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 1;
}

drumrom::synth::EnvelopeShape rand_shape(std::mt19937& rng) {
    return rand_bool(rng)
        ? drumrom::synth::EnvelopeShape::Exponential
        : drumrom::synth::EnvelopeShape::Linear;
}

float rate_from_target_time(float target_s, drumrom::synth::EnvelopeShape shape) {
    const float t = std::max(0.002f, target_s);
    // Exponential: reach about -40 dB near target time (e^-4.605 ~= 0.01).
    if (shape == drumrom::synth::EnvelopeShape::Exponential) {
        return 4.6051702f / t;
    }
    // Linear: hit zero at target time.
    return 1.0f / t;
}

float random_slot_aware_rate(
    std::mt19937& rng,
    drumrom::synth::EnvelopeShape shape,
    float slot_time_s,
    float min_tail_frac,
    float max_tail_frac,
    float min_rate,
    float max_rate) {
    const float min_target = std::max(0.002f, slot_time_s * min_tail_frac);
    const float max_target = std::max(min_target, slot_time_s * max_tail_frac);
    const float target = rand_range(rng, min_target, max_target);
    return clampf(rate_from_target_time(target, shape), min_rate, max_rate);
}

drumrom::synth::DrumParams make_randomized_params(
    const std::string& drum_type,
    int sample_rate,
    std::size_t num_samples,
    std::mt19937& rng,
    bool slot_aware_random) {
    drumrom::synth::DrumParams params;
    params.sample_rate = sample_rate;
    const float slot_time_s = static_cast<float>(num_samples) / static_cast<float>(sample_rate);

    if (drum_type == "kick") {
        params.kick.pitch_start_hz = rand_range(rng, 110.0f, 220.0f);
        params.kick.pitch_end_hz = rand_range(rng, 28.0f, 60.0f);
        if (params.kick.pitch_end_hz > params.kick.pitch_start_hz - 5.0f) {
            params.kick.pitch_end_hz = std::max(20.0f, params.kick.pitch_start_hz - 5.0f);
        }
        params.kick.pitch_decay_rate = rand_range(rng, 8.0f, 22.0f);
        params.kick.pitch_env_shape = rand_shape(rng);
        params.kick.amp_decay_shape = rand_shape(rng);
        params.kick.tone_env_shape = rand_shape(rng);
        if (slot_aware_random) {
            params.kick.env_decay_rate = random_slot_aware_rate(rng, params.kick.amp_decay_shape, slot_time_s, 0.70f, 0.95f, 4.0f, 35.0f);
            params.kick.tone_decay_rate = random_slot_aware_rate(rng, params.kick.tone_env_shape, slot_time_s, 0.45f, 0.85f, 0.0f, 20.0f);
        } else {
            params.kick.env_decay_rate = rand_range(rng, 12.0f, 28.0f);
            params.kick.tone_decay_rate = rand_range(rng, 0.0f, 18.0f);
        }
        params.kick.fm.mod_freq_hz = rand_range(rng, 20.0f, 180.0f);
        params.kick.fm.mod_freq_end_hz = rand_range(rng, 15.0f, 140.0f);
        params.kick.fm.mod_pitch_decay_rate = rand_range(rng, 0.0f, 20.0f);
        params.kick.fm.mod_pitch_env_shape = rand_shape(rng);
        params.kick.fm.mod_index = rand_range(rng, 0.2f, 4.0f);
        params.kick.fm.mod_index_end = rand_range(rng, 0.0f, 1.2f);
        params.kick.fm.mod_index_env_shape = rand_shape(rng);
        params.kick.fm.mod_index_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.kick.fm.mod_index_env_shape, slot_time_s, 0.35f, 0.90f, 1.0f, 40.0f)
            : rand_range(rng, 2.0f, 24.0f);
        params.kick.fm.amp_osc_hz = rand_range(rng, 0.0f, 14.0f);
        params.kick.fm.amp_osc_end_hz = rand_range(rng, 0.0f, 12.0f);
        params.kick.fm.amp_osc_pitch_decay_rate = rand_range(rng, 0.0f, 12.0f);
        params.kick.fm.amp_osc_pitch_env_shape = rand_shape(rng);
        params.kick.fm.amp_osc_depth = rand_range(rng, 0.0f, 0.35f);
        params.kick.fm.amp_osc_depth_end = rand_range(rng, 0.0f, 0.20f);
        params.kick.fm.amp_osc_depth_env_shape = rand_shape(rng);
        params.kick.fm.amp_osc_depth_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.kick.fm.amp_osc_depth_env_shape, slot_time_s, 0.30f, 0.95f, 0.0f, 22.0f)
            : rand_range(rng, 0.0f, 18.0f);
        params.kick.attack_rate = 0.0f;  // Keep attack fast/no fade-in.
        params.kick.amp_attack_shape = drumrom::synth::EnvelopeShape::Linear;
    } else if (drum_type == "snare") {
        params.snare.tone_freq_hz = rand_range(rng, 130.0f, 320.0f);
        params.snare.tone_freq_end_hz = rand_range(rng, 120.0f, 300.0f);
        params.snare.pitch_decay_rate = rand_range(rng, 0.0f, 20.0f);
        params.snare.pitch_env_shape = rand_shape(rng);
        params.snare.tone_env_shape = rand_shape(rng);
        params.snare.tone_mix = rand_range(rng, 0.15f, 0.65f);
        params.snare.noise_mix = rand_range(rng, 0.55f, 1.0f);
        params.snare.amp_decay_shape = rand_shape(rng);
        if (slot_aware_random) {
            params.snare.tone_decay_rate = random_slot_aware_rate(rng, params.snare.tone_env_shape, slot_time_s, 0.55f, 0.90f, 6.0f, 40.0f);
            params.snare.noise_decay_rate = random_slot_aware_rate(rng, drumrom::synth::EnvelopeShape::Exponential, slot_time_s, 0.60f, 0.98f, 8.0f, 50.0f);
            params.snare.amp_decay_rate = random_slot_aware_rate(rng, params.snare.amp_decay_shape, slot_time_s, 0.70f, 0.98f, 0.0f, 26.0f);
        } else {
            params.snare.tone_decay_rate = rand_range(rng, 12.0f, 35.0f);
            params.snare.noise_decay_rate = rand_range(rng, 14.0f, 45.0f);
            params.snare.amp_decay_rate = rand_range(rng, 0.0f, 22.0f);
        }
        params.snare.fm.mod_freq_hz = rand_range(rng, 80.0f, 480.0f);
        params.snare.fm.mod_freq_end_hz = rand_range(rng, 60.0f, 420.0f);
        params.snare.fm.mod_pitch_decay_rate = rand_range(rng, 0.0f, 24.0f);
        params.snare.fm.mod_pitch_env_shape = rand_shape(rng);
        params.snare.fm.mod_index = rand_range(rng, 0.2f, 3.5f);
        params.snare.fm.mod_index_end = rand_range(rng, 0.0f, 1.0f);
        params.snare.fm.mod_index_env_shape = rand_shape(rng);
        params.snare.fm.mod_index_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.snare.fm.mod_index_env_shape, slot_time_s, 0.40f, 0.95f, 1.0f, 45.0f)
            : rand_range(rng, 2.0f, 28.0f);
        params.snare.fm.amp_osc_hz = rand_range(rng, 0.0f, 18.0f);
        params.snare.fm.amp_osc_end_hz = rand_range(rng, 0.0f, 16.0f);
        params.snare.fm.amp_osc_pitch_decay_rate = rand_range(rng, 0.0f, 14.0f);
        params.snare.fm.amp_osc_pitch_env_shape = rand_shape(rng);
        params.snare.fm.amp_osc_depth = rand_range(rng, 0.0f, 0.40f);
        params.snare.fm.amp_osc_depth_end = rand_range(rng, 0.0f, 0.25f);
        params.snare.fm.amp_osc_depth_env_shape = rand_shape(rng);
        params.snare.fm.amp_osc_depth_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.snare.fm.amp_osc_depth_env_shape, slot_time_s, 0.30f, 0.95f, 0.0f, 26.0f)
            : rand_range(rng, 0.0f, 20.0f);
        params.snare.attack_rate = 0.0f;  // Keep attack fast/no fade-in.
        params.snare.amp_attack_shape = drumrom::synth::EnvelopeShape::Linear;
    } else if (drum_type == "hihat") {
        params.hihat.tone_freq_hz = rand_range(rng, 220.0f, 620.0f);
        params.hihat.tone_freq_end_hz = rand_range(rng, 220.0f, 620.0f);
        params.hihat.pitch_decay_rate = rand_range(rng, 0.0f, 24.0f);
        params.hihat.pitch_env_shape = rand_shape(rng);
        params.hihat.tone_mix = rand_range(rng, 0.20f, 0.75f);
        for (std::size_t i = 0; i < params.hihat.square_ratios.size(); ++i) {
            const float base = 1.0f + (0.25f * static_cast<float>(i));
            params.hihat.square_ratios[i] = rand_range(rng, base * 0.85f, base * 1.30f);
        }
        params.hihat.tone_env_shape = rand_shape(rng);
        params.hihat.amp_decay_shape = rand_shape(rng);
        if (slot_aware_random) {
            params.hihat.tone_decay_rate = random_slot_aware_rate(rng, params.hihat.tone_env_shape, slot_time_s, 0.45f, 0.85f, 0.0f, 45.0f);
            params.hihat.decay_rate = random_slot_aware_rate(rng, params.hihat.amp_decay_shape, slot_time_s, 0.55f, 0.95f, 10.0f, 95.0f);
        } else {
            params.hihat.tone_decay_rate = rand_range(rng, 0.0f, 40.0f);
            params.hihat.decay_rate = rand_range(rng, 25.0f, 85.0f);
        }
        params.hihat.fm.mod_freq_hz = rand_range(rng, 250.0f, 1600.0f);
        params.hihat.fm.mod_freq_end_hz = rand_range(rng, 220.0f, 1400.0f);
        params.hihat.fm.mod_pitch_decay_rate = rand_range(rng, 0.0f, 30.0f);
        params.hihat.fm.mod_pitch_env_shape = rand_shape(rng);
        params.hihat.fm.mod_index = rand_range(rng, 0.4f, 6.0f);
        params.hihat.fm.mod_index_end = rand_range(rng, 0.05f, 2.5f);
        params.hihat.fm.mod_index_env_shape = rand_shape(rng);
        params.hihat.fm.mod_index_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.hihat.fm.mod_index_env_shape, slot_time_s, 0.35f, 0.95f, 1.0f, 60.0f)
            : rand_range(rng, 4.0f, 36.0f);
        params.hihat.fm.amp_osc_hz = rand_range(rng, 2.0f, 55.0f);
        params.hihat.fm.amp_osc_end_hz = rand_range(rng, 2.0f, 45.0f);
        params.hihat.fm.amp_osc_pitch_decay_rate = rand_range(rng, 0.0f, 22.0f);
        params.hihat.fm.amp_osc_pitch_env_shape = rand_shape(rng);
        params.hihat.fm.amp_osc_depth = rand_range(rng, 0.0f, 0.45f);
        params.hihat.fm.amp_osc_depth_end = rand_range(rng, 0.0f, 0.30f);
        params.hihat.fm.amp_osc_depth_env_shape = rand_shape(rng);
        params.hihat.fm.amp_osc_depth_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.hihat.fm.amp_osc_depth_env_shape, slot_time_s, 0.30f, 0.95f, 0.0f, 32.0f)
            : rand_range(rng, 0.0f, 22.0f);
        params.hihat.attack_rate = 0.0f;  // Keep attack fast/no fade-in.
        params.hihat.amp_attack_shape = drumrom::synth::EnvelopeShape::Linear;
    } else if (drum_type == "tom") {
        params.tom.pitch_start_hz = rand_range(rng, 85.0f, 180.0f);
        params.tom.pitch_end_hz = rand_range(rng, 40.0f, 95.0f);
        if (params.tom.pitch_end_hz > params.tom.pitch_start_hz - 5.0f) {
            params.tom.pitch_end_hz = std::max(30.0f, params.tom.pitch_start_hz - 5.0f);
        }
        params.tom.pitch_decay_rate = rand_range(rng, 2.0f, 10.0f);
        params.tom.pitch_env_shape = rand_shape(rng);
        params.tom.amp_decay_shape = rand_shape(rng);
        params.tom.tone_env_shape = rand_shape(rng);
        if (slot_aware_random) {
            params.tom.env_decay_rate = random_slot_aware_rate(rng, params.tom.amp_decay_shape, slot_time_s, 0.70f, 0.98f, 6.0f, 28.0f);
            params.tom.tone_decay_rate = random_slot_aware_rate(rng, params.tom.tone_env_shape, slot_time_s, 0.50f, 0.90f, 0.0f, 20.0f);
        } else {
            params.tom.env_decay_rate = rand_range(rng, 10.0f, 22.0f);
            params.tom.tone_decay_rate = rand_range(rng, 0.0f, 18.0f);
        }
        params.tom.fm.mod_freq_hz = rand_range(rng, 40.0f, 260.0f);
        params.tom.fm.mod_freq_end_hz = rand_range(rng, 35.0f, 220.0f);
        params.tom.fm.mod_pitch_decay_rate = rand_range(rng, 0.0f, 22.0f);
        params.tom.fm.mod_pitch_env_shape = rand_shape(rng);
        params.tom.fm.mod_index = rand_range(rng, 0.2f, 3.6f);
        params.tom.fm.mod_index_end = rand_range(rng, 0.0f, 1.4f);
        params.tom.fm.mod_index_env_shape = rand_shape(rng);
        params.tom.fm.mod_index_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.tom.fm.mod_index_env_shape, slot_time_s, 0.35f, 0.95f, 1.0f, 42.0f)
            : rand_range(rng, 2.0f, 26.0f);
        params.tom.fm.amp_osc_hz = rand_range(rng, 0.0f, 16.0f);
        params.tom.fm.amp_osc_end_hz = rand_range(rng, 0.0f, 14.0f);
        params.tom.fm.amp_osc_pitch_decay_rate = rand_range(rng, 0.0f, 14.0f);
        params.tom.fm.amp_osc_pitch_env_shape = rand_shape(rng);
        params.tom.fm.amp_osc_depth = rand_range(rng, 0.0f, 0.35f);
        params.tom.fm.amp_osc_depth_end = rand_range(rng, 0.0f, 0.22f);
        params.tom.fm.amp_osc_depth_env_shape = rand_shape(rng);
        params.tom.fm.amp_osc_depth_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.tom.fm.amp_osc_depth_env_shape, slot_time_s, 0.30f, 0.95f, 0.0f, 24.0f)
            : rand_range(rng, 0.0f, 18.0f);
        params.tom.attack_rate = 0.0f;  // Keep attack fast/no fade-in.
        params.tom.amp_attack_shape = drumrom::synth::EnvelopeShape::Linear;
    } else if (drum_type == "clap") {
        params.clap.tone_freq_hz = rand_range(rng, 700.0f, 2200.0f);
        params.clap.tone_freq_end_hz = rand_range(rng, 700.0f, 2200.0f);
        params.clap.pitch_decay_rate = rand_range(rng, 0.0f, 35.0f);
        params.clap.pitch_env_shape = rand_shape(rng);
        params.clap.tone_mix = rand_range(rng, 0.0f, 0.35f);
        params.clap.tone_env_shape = rand_shape(rng);
        params.clap.amp_decay_shape = rand_shape(rng);
        if (slot_aware_random) {
            params.clap.tone_decay_rate = random_slot_aware_rate(rng, params.clap.tone_env_shape, slot_time_s, 0.45f, 0.85f, 0.0f, 28.0f);
            params.clap.env_decay_rate = random_slot_aware_rate(rng, params.clap.amp_decay_shape, slot_time_s, 0.65f, 0.98f, 8.0f, 42.0f);
        } else {
            params.clap.tone_decay_rate = rand_range(rng, 0.0f, 25.0f);
            params.clap.env_decay_rate = rand_range(rng, 15.0f, 35.0f);
        }
        params.clap.fm.mod_freq_hz = rand_range(rng, 120.0f, 1200.0f);
        params.clap.fm.mod_freq_end_hz = rand_range(rng, 100.0f, 1000.0f);
        params.clap.fm.mod_pitch_decay_rate = rand_range(rng, 0.0f, 28.0f);
        params.clap.fm.mod_pitch_env_shape = rand_shape(rng);
        params.clap.fm.mod_index = rand_range(rng, 0.2f, 3.2f);
        params.clap.fm.mod_index_end = rand_range(rng, 0.0f, 1.2f);
        params.clap.fm.mod_index_env_shape = rand_shape(rng);
        params.clap.fm.mod_index_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.clap.fm.mod_index_env_shape, slot_time_s, 0.35f, 0.95f, 1.0f, 46.0f)
            : rand_range(rng, 2.0f, 30.0f);
        params.clap.fm.amp_osc_hz = rand_range(rng, 0.0f, 20.0f);
        params.clap.fm.amp_osc_end_hz = rand_range(rng, 0.0f, 18.0f);
        params.clap.fm.amp_osc_pitch_decay_rate = rand_range(rng, 0.0f, 16.0f);
        params.clap.fm.amp_osc_pitch_env_shape = rand_shape(rng);
        params.clap.fm.amp_osc_depth = rand_range(rng, 0.0f, 0.40f);
        params.clap.fm.amp_osc_depth_end = rand_range(rng, 0.0f, 0.25f);
        params.clap.fm.amp_osc_depth_env_shape = rand_shape(rng);
        params.clap.fm.amp_osc_depth_decay_rate = slot_aware_random
            ? random_slot_aware_rate(rng, params.clap.fm.amp_osc_depth_env_shape, slot_time_s, 0.30f, 0.95f, 0.0f, 26.0f)
            : rand_range(rng, 0.0f, 20.0f);
        params.clap.attack_rate = 0.0f;  // Keep attack fast/no fade-in.
        params.clap.amp_attack_shape = drumrom::synth::EnvelopeShape::Linear;
    }

    return params;
}

}  // namespace

void write_raw(const std::string& filename, const std::vector<std::uint8_t>& data) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot write: " + filename);
    }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    std::cout << "Wrote " << filename << " (" << data.size() << " bytes)\n";
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <drum_type> <sample_rate> <num_samples> <output.raw>\n";
        std::cerr << "Drum types: kick, snare, hihat, tom, clap\n";
        std::cerr << "Example: " << argv[0] << " kick 20000 4400 kick.raw\n";
        std::cerr << "         (4400 samples @ 20kHz = 0.22 seconds)\n";
        return 1;
    }

    try {
        const std::string drum_type = argv[1];
        const int sample_rate = std::stoi(argv[2]);
        const std::size_t num_samples = std::stoul(argv[3]);
        const std::string output = argv[4];

        bool use_randomized_params = false;
        bool slot_aware_random = false;
        std::uint32_t seed = 0xDEADBEEFU;

        for (int i = 5; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--randomize-params") {
                use_randomized_params = true;
            } else if (arg == "--slot-aware-random") {
                slot_aware_random = true;
                use_randomized_params = true;
            } else if (arg == "--seed") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("Missing value for --seed");
                }
                const unsigned long parsed = std::stoul(argv[++i]);
                if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::runtime_error("Seed out of range for uint32");
                }
                seed = static_cast<std::uint32_t>(parsed);
            } else {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }

        std::mt19937 rng(seed);
        std::vector<std::uint8_t> sample;
        drumrom::synth::DrumParams params;
        if (use_randomized_params) {
            params = make_randomized_params(drum_type, sample_rate, num_samples, rng, slot_aware_random);
        }

        if (drum_type == "kick") {
            sample = use_randomized_params
                ? drumrom::synth::synthesize_kick_custom(params, rng, num_samples)
                : drumrom::synth::synthesize_kick(sample_rate, num_samples, rng);
        } else if (drum_type == "snare") {
            sample = use_randomized_params
                ? drumrom::synth::synthesize_snare_custom(params, rng, num_samples)
                : drumrom::synth::synthesize_snare(sample_rate, num_samples, rng);
        } else if (drum_type == "hihat") {
            sample = use_randomized_params
                ? drumrom::synth::synthesize_hihat_custom(params, rng, num_samples)
                : drumrom::synth::synthesize_hihat(sample_rate, num_samples, rng);
        } else if (drum_type == "tom") {
            sample = use_randomized_params
                ? drumrom::synth::synthesize_tom_custom(params, rng, num_samples)
                : drumrom::synth::synthesize_tom(sample_rate, num_samples, rng);
        } else if (drum_type == "clap") {
            sample = use_randomized_params
                ? drumrom::synth::synthesize_clap_custom(params, rng, num_samples)
                : drumrom::synth::synthesize_clap(sample_rate, num_samples, rng);
        } else {
            throw std::runtime_error("Unknown drum type: " + drum_type);
        }

        write_raw(output, sample);
        const float duration_s = static_cast<float>(num_samples) / static_cast<float>(sample_rate);
        std::cout << "Duration: " << duration_s << "s @ " << sample_rate << "Hz = " << sample.size() << " bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
