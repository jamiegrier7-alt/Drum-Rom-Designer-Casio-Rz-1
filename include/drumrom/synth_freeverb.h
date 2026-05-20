#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace drumrom::synth {

// Freeverb reverb parameters
struct ReverbParams {
    float decay_time_ms = 200.0f;  // 10.0 to 2000.0 - total reverb tail length
    float damping = 0.5f;          // 0.0 to 1.0 - highpass damping (darker = more damping)
    float width = 1.0f;            // 0.0 to 1.0 - stereo width (we use mono, so 1.0 = full)
    float early_level = 0.35f;     // 0.0 to 1.0 - early reflection amount
    float early_spread = 1.0f;     // 0.5 to 2.0 - early reflection spacing
    float diffusion = 0.5f;        // 0.0 to 1.0 - late reverb density
    float tone = 0.75f;            // 0.0 to 1.0 - dark to bright wet tone
    float late_mix = 0.65f;        // 0.0 to 1.0 - tail vs early balance
    float size = 1.0f;             // 0.5 to 1.5 - room size scaling
    float decay_shape = 0.5f;      // 0.0 to 1.0 - abrupt to smooth tail curve
    float wet_level = 0.3f;        // 0.0 to 1.0 - wet output level
    float dry_level = 0.7f;        // 0.0 to 1.0 - dry output level
    float pre_delay_ms = 0.0f;     // 0.0 to 50.0 - pre-delay in milliseconds
};

// Simple Freeverb stereo reverb processor
// Based on Schroeder reverberator architecture
class Freeverb {
public:
    Freeverb();
    
    // Initialize for given sample rate
    void init(int sample_rate);
    
    // Reset internal state
    void reset();
    
    // Set parameters
    void set_params(const ReverbParams& params);
    
    // Process mono sample, return [left, right] for stereo output
    // (for mono drum synthesis, both outputs will be similar)
    void process_sample(float input, float& left_out, float& right_out);
    
    // Process vector of samples and return stereo output vectors
    // Applies wet/dry mix internally
    std::pair<std::vector<float>, std::vector<float>> process(
        const std::vector<float>& input,
        const ReverbParams& params);

private:
    // Dynamic comb filter (buffer size scales with sample rate)
    struct CombFilter {
        std::vector<float> buffer;
        int buffer_index = 0;
        float feedback = 0.5f;
        float damp1 = 0.5f;
        float damp2 = 0.5f;
        float filter_store = 0.0f;
        
        void init(int size) {
            buffer.assign(size, 0.0f);
            buffer_index = 0;
            filter_store = 0.0f;
        }
        
        float process(float input) {
            if (buffer.empty()) return input;
            const float buffered_out = buffer[buffer_index];
            const float filter_out = (buffered_out * damp2) + (filter_store * damp1);
            filter_store = filter_out;
            buffer[buffer_index] = input + (filter_out * feedback);
            buffer_index = (buffer_index + 1) % static_cast<int>(buffer.size());
            return buffered_out;
        }
        
        void reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            buffer_index = 0;
            filter_store = 0.0f;
        }
    };
    
    // Dynamic allpass filter (buffer size scales with sample rate)
    struct AllpassFilter {
        std::vector<float> buffer;
        int buffer_index = 0;
        float feedback = 0.5f;
        
        void init(int size) {
            buffer.assign(size, 0.0f);
            buffer_index = 0;
        }
        
        float process(float input) {
            if (buffer.empty()) return input;
            const float buffered_out = buffer[buffer_index];
            const float out = -input + buffered_out;
            buffer[buffer_index] = input + (buffered_out * feedback);
            buffer_index = (buffer_index + 1) % static_cast<int>(buffer.size());
            return out;
        }
        
        void reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            buffer_index = 0;
        }
    };
    
    // Pre-delay ring buffer
    struct PreDelay {
        std::vector<float> buffer;
        int buffer_index = 0;
        int delay_samples = 0;
        
        void init(int max_samples) {
            buffer.assign(max_samples, 0.0f);
            buffer_index = 0;
        }
        
        void set_delay_samples(int samples) {
            const int max_samples = static_cast<int>(buffer.size()) - 1;
            delay_samples = (samples < 0) ? 0 : (samples > max_samples) ? max_samples : samples;
        }
        
        float process(float input) {
            if (buffer.empty()) return input;
            if (delay_samples == 0) {
                return input;
            }
            int read_index = buffer_index - delay_samples;
            if (read_index < 0) {
                read_index += static_cast<int>(buffer.size());
            }
            const float out = buffer[read_index];
            buffer[buffer_index] = input;
            buffer_index = (buffer_index + 1) % static_cast<int>(buffer.size());
            return out;
        }
        
        void reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            buffer_index = 0;
        }
    };
    
    // Parallel comb filters
    std::array<CombFilter, 8> combs;
    
    // Series allpass filters
    std::array<AllpassFilter, 4> allpasses;
    
    // Pre-delay
    PreDelay pre_delay;
    
    ReverbParams current_params;
    int sample_rate = 20833;
    
    void update_damping(float damping);
    void init_filter_sizes();
};

// Apply Freeverb to a mono signal with wet/dry mix
std::vector<float> apply_freeverb(
    const std::vector<float>& signal,
    const ReverbParams& params,
    int sample_rate);

}  // namespace drumrom::synth
