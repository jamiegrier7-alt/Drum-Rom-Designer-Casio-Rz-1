#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace drumrom::synth {

enum class ElementsExactResonatorModel : int {
    Modal = 0,
    Sympathetic = 1,
    InharmonicString = 2,
};

enum class ElementsExactCvTarget : int {
    None = 0,
    Note,
    Modulation,
    Strength,
    BlowCv,
    StrikeCv,
    ExciterBowLevel,
    ExciterBlowLevel,
    ExciterStrikeLevel,
    ResonatorGeometry,
    ResonatorBrightness,
    ResonatorDamping,
    ResonatorPosition,
    Space,
};

struct ElementsExactEnvelope {
    ElementsExactCvTarget target = ElementsExactCvTarget::None;
    float amount = 0.0f;
    float attack_s = 0.002f;
    float decay_s = 0.20f;
    float sustain = 0.0f;
    float release_s = 0.08f;
};

// Direct mirror of Mutable Elements patch/performance controls, with 2 extra
// envelope routers for CV-like modulation destinations.
struct ElementsExactParams {
    // Performance state
    float note = 24.0f;
    float modulation = 0.0f;
    float strength = 0.85f;

    // External CV-like inputs (per-sample control streams)
    float blow_cv = 0.0f;
    float strike_cv = 0.0f;

    // Patch controls from Mutable Elements
    float exciter_envelope_shape = 1.0f;
    float exciter_bow_level = 0.0f;
    float exciter_bow_timbre = 0.5f;
    float exciter_blow_level = 0.0f;
    float exciter_blow_meta = 0.5f;
    float exciter_blow_timbre = 0.5f;
    float exciter_strike_level = 0.8f;
    float exciter_strike_meta = 0.5f;
    float exciter_strike_timbre = 0.5f;
    float exciter_signature = 0.0f;
    float resonator_geometry = 0.2f;
    float resonator_brightness = 0.5f;
    float resonator_damping = 0.25f;
    float resonator_position = 0.30f;
    float resonator_modulation_frequency = 0.5f / 32000.0f;
    float resonator_modulation_offset = 0.10f;
    float reverb_diffusion = 0.625f;
    float reverb_lp = 0.7f;
    float space = 0.5f;
    float modulation_frequency = 0.0f;

    ElementsExactResonatorModel resonator_model = ElementsExactResonatorModel::Modal;
    float easter_egg = 0.0f;

    ElementsExactEnvelope cv_env1{};
    ElementsExactEnvelope cv_env2{};
};

std::vector<std::uint8_t> synthesize_elements_exact(int output_sample_rate,
                                                    std::size_t output_num_samples,
                                                    const ElementsExactParams& params,
                                                    std::mt19937& rng);

}  // namespace drumrom::synth
