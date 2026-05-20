#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace drumrom::synth {

// Physical resonator models inspired by Mutable Instruments Elements.
// Each model provides a distinct set of inharmonic partial frequency ratios
// that determine the character of the resonated sound.
enum class ElementsModel : int {
    Membrane = 0,  // Circular drum head (toms, kicks, snare body)
    Plate    = 1,  // Flat plate (cymbals, snare body, metallic hits)
    Bar      = 2,  // Free bar (marimba, xylophone, wood blocks)
    Bell     = 3,  // Inharmonic bell (cowbell, tubular bells, metallic rings)
    String   = 4,  // Harmonic string (plucked bass, pitched hits)
    Tube     = 5,  // Closed tube / odd-harmonic resonator (woodwind-like body)
};

// Parameters for Elements-style modal/physical resonator synthesis.
// Must remain trivially copyable for binary preset serialization.
struct ElementsParams {
    ElementsModel model          = ElementsModel::Membrane;
    float         frequency_hz   = 80.0f;   // Fundamental resonator frequency [Hz]
    float         brightness     = 0.5f;    // Spectral brightness [0=dark, 1=bright]
    float         damping        = 0.4f;    // Resonator damping [0=long sustain, 1=short]
    float         position       = 0.2f;    // Excitation position [0=center, 1=edge]
    float         exciter_level  = 0.9f;    // Exciter amplitude [0..1]
    float         exciter_noise  = 0.1f;    // Noise blend [0=impulse only, 1=noise only]
    float         exciter_dur_s  = 0.008f;  // Exciter pulse width [seconds]
    float         env_decay_rate = 8.0f;    // Output amplitude decay rate [0=off, >0=faster]
    float         env_attack_rate = 0.0f;   // Output amplitude attack rate [0=instant]
};

// Synthesize a sample using modal resonator physical modelling.
// Drives a bank of 8 tuned resonators (per the selected model) with a
// short strike exciter (impulse + optional noise).
// Returns signed-8-bit PCM packed in uint8 (matches the rest of the synth API).
std::vector<std::uint8_t> synthesize_elements(int sample_rate,
                                              std::size_t num_samples,
                                              const ElementsParams& params,
                                              std::mt19937& rng);

}  // namespace drumrom::synth
