#include "drumrom/synth_elements_exact.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "elements/dsp/part.h"
#include "elements/dsp/patch.h"

namespace drumrom::synth {
namespace {

constexpr int kElementsSampleRate = 32000;
constexpr std::size_t kElementsBlockSize = 16;
constexpr std::size_t kElementsReverbWords = 32768;

inline float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline float sanitize_range(float v, float lo, float hi, float fallback) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

inline float envelope_value(const ElementsExactEnvelope& env, std::size_t i, std::size_t n, std::size_t gate_off) {
    const float sr = static_cast<float>(kElementsSampleRate);
    const std::size_t attack_n = static_cast<std::size_t>(std::max(1.0f, env.attack_s * sr));
    const std::size_t decay_n = static_cast<std::size_t>(std::max(1.0f, env.decay_s * sr));
    const std::size_t release_n = static_cast<std::size_t>(std::max(1.0f, env.release_s * sr));

    if (i < gate_off) {
        if (i < attack_n) {
            return static_cast<float>(i) / static_cast<float>(attack_n);
        }
        const std::size_t after_attack = i - attack_n;
        if (after_attack < decay_n) {
            const float t = static_cast<float>(after_attack) / static_cast<float>(decay_n);
            return 1.0f + (env.sustain - 1.0f) * t;
        }
        return env.sustain;
    }

    const std::size_t rel_i = i - gate_off;
    if (rel_i >= release_n) {
        return 0.0f;
    }

    float level_at_release = env.sustain;
    if (gate_off < attack_n) {
        level_at_release = static_cast<float>(gate_off) / static_cast<float>(attack_n);
    } else {
        const std::size_t after_attack = gate_off - attack_n;
        if (after_attack < decay_n) {
            const float t = static_cast<float>(after_attack) / static_cast<float>(decay_n);
            level_at_release = 1.0f + (env.sustain - 1.0f) * t;
        }
    }

    const float t = static_cast<float>(rel_i) / static_cast<float>(release_n);
    return level_at_release * (1.0f - t);
}

inline void apply_cv_mod(ElementsExactCvTarget target,
                         float value,
                         float* note,
                         float* modulation,
                         float* strength,
                         float* blow_cv,
                         float* strike_cv,
                         elements::Patch* patch) {
    if (!std::isfinite(value)) {
        return;
    }
    switch (target) {
        case ElementsExactCvTarget::None:
            break;
        case ElementsExactCvTarget::Note:
            *note += value;
            break;
        case ElementsExactCvTarget::Modulation:
            *modulation += value;
            break;
        case ElementsExactCvTarget::Strength:
            *strength += value;
            break;
        case ElementsExactCvTarget::BlowCv:
            *blow_cv += value;
            break;
        case ElementsExactCvTarget::StrikeCv:
            *strike_cv += value;
            break;
        case ElementsExactCvTarget::ExciterBowLevel:
            patch->exciter_bow_level = clamp01(patch->exciter_bow_level + value);
            break;
        case ElementsExactCvTarget::ExciterBlowLevel:
            patch->exciter_blow_level = clamp01(patch->exciter_blow_level + value);
            break;
        case ElementsExactCvTarget::ExciterStrikeLevel:
            patch->exciter_strike_level = clamp01(patch->exciter_strike_level + value);
            break;
        case ElementsExactCvTarget::ResonatorGeometry:
            patch->resonator_geometry = clamp01(patch->resonator_geometry + value);
            break;
        case ElementsExactCvTarget::ResonatorBrightness:
            patch->resonator_brightness = clamp01(patch->resonator_brightness + value);
            break;
        case ElementsExactCvTarget::ResonatorDamping:
            patch->resonator_damping = clamp01(patch->resonator_damping + value);
            break;
        case ElementsExactCvTarget::ResonatorPosition:
            patch->resonator_position = clamp01(patch->resonator_position + value);
            break;
        case ElementsExactCvTarget::Space:
            patch->space = clamp01(patch->space + value);
            break;
    }
}

inline void apply_cv_stream_mod(ElementsExactCvTarget target,
                                float value,
                                float* blow_cv,
                                float* strike_cv) {
    if (!std::isfinite(value)) {
        return;
    }
    switch (target) {
        case ElementsExactCvTarget::BlowCv:
            *blow_cv += value;
            break;
        case ElementsExactCvTarget::StrikeCv:
            *strike_cv += value;
            break;
        default:
            break;
    }
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

}  // namespace

std::vector<std::uint8_t> synthesize_elements_exact(int output_sample_rate,
                                                    std::size_t output_num_samples,
                                                    const ElementsExactParams& params,
                                                    std::mt19937& rng) {
    (void)rng;
    if (output_sample_rate <= 0 || output_num_samples == 0) {
        return {};
    }

    const float src_len_f = static_cast<float>(output_num_samples) *
        (static_cast<float>(kElementsSampleRate) / static_cast<float>(output_sample_rate));
    const std::size_t src_num_samples = std::max<std::size_t>(kElementsBlockSize, static_cast<std::size_t>(std::ceil(src_len_f)) + 1u);
    const std::size_t src_num_samples_padded =
        ((src_num_samples + kElementsBlockSize - 1u) / kElementsBlockSize) * kElementsBlockSize;

    // Defensive sanitization for preset/kit data: malformed floats can crash Mutable DSP.
    const float note_base = sanitize_range(params.note, 0.0f, 127.0f, 48.0f);
    const float modulation_base = sanitize_range(params.modulation, -48.0f, 48.0f, 0.0f);
    const float strength_base = sanitize_range(params.strength, 0.0f, 1.0f, 0.5f);
    const float blow_cv_base = sanitize_range(params.blow_cv, 0.0f, 1.0f, 0.0f);
    const float strike_cv_base = sanitize_range(params.strike_cv, 0.0f, 1.0f, 0.0f);

    ElementsExactEnvelope env1{};
    env1.target = params.cv_env1.target;
    env1.amount = sanitize_range(params.cv_env1.amount, -1.0f, 1.0f, 0.0f);
    env1.attack_s = sanitize_range(params.cv_env1.attack_s, 0.0f, 5.0f, 0.01f);
    env1.decay_s = sanitize_range(params.cv_env1.decay_s, 0.0f, 5.0f, 0.10f);
    env1.sustain = sanitize_range(params.cv_env1.sustain, 0.0f, 1.0f, 0.0f);
    env1.release_s = sanitize_range(params.cv_env1.release_s, 0.0f, 5.0f, 0.20f);

    ElementsExactEnvelope env2{};
    env2.target = params.cv_env2.target;
    env2.amount = sanitize_range(params.cv_env2.amount, -1.0f, 1.0f, 0.0f);
    env2.attack_s = sanitize_range(params.cv_env2.attack_s, 0.0f, 5.0f, 0.01f);
    env2.decay_s = sanitize_range(params.cv_env2.decay_s, 0.0f, 5.0f, 0.10f);
    env2.sustain = sanitize_range(params.cv_env2.sustain, 0.0f, 1.0f, 0.0f);
    env2.release_s = sanitize_range(params.cv_env2.release_s, 0.0f, 5.0f, 0.20f);

    elements::Part part;
    std::vector<std::uint16_t> reverb_buffer(kElementsReverbWords);
    part.Init(reverb_buffer.data());

    // Host safety: keep the exact resonator path but disable OminousVoice
    // branch, which is unstable in this integration.
    part.set_easter_egg(false);
    part.set_resonator_model(static_cast<elements::ResonatorModel>(
        std::clamp(static_cast<int>(params.resonator_model), 0, 2)));

    elements::Patch* patch = part.mutable_patch();
    patch->exciter_envelope_shape = sanitize_range(params.exciter_envelope_shape, 0.0f, 1.0f, 0.5f);
    patch->exciter_bow_level = sanitize_range(params.exciter_bow_level, 0.0f, 1.0f, 0.0f);
    patch->exciter_bow_timbre = sanitize_range(params.exciter_bow_timbre, 0.0f, 1.0f, 0.5f);
    patch->exciter_blow_level = sanitize_range(params.exciter_blow_level, 0.0f, 1.0f, 0.0f);
    patch->exciter_blow_meta = sanitize_range(params.exciter_blow_meta, 0.0f, 1.0f, 0.5f);
    patch->exciter_blow_timbre = sanitize_range(params.exciter_blow_timbre, 0.0f, 1.0f, 0.5f);
    patch->exciter_strike_level = sanitize_range(params.exciter_strike_level, 0.0f, 1.0f, 0.25f);
    patch->exciter_strike_meta = sanitize_range(params.exciter_strike_meta, 0.0f, 1.0f, 0.5f);
    patch->exciter_strike_timbre = sanitize_range(params.exciter_strike_timbre, 0.0f, 1.0f, 0.5f);
    patch->exciter_signature = sanitize_range(params.exciter_signature, 0.0f, 1.0f, 0.5f);
    patch->resonator_geometry = sanitize_range(params.resonator_geometry, 0.0f, 1.0f, 0.5f);
    patch->resonator_brightness = sanitize_range(params.resonator_brightness, 0.0f, 1.0f, 0.5f);
    patch->resonator_damping = sanitize_range(params.resonator_damping, 0.0f, 1.0f, 0.2f);
    patch->resonator_position = sanitize_range(params.resonator_position, 0.0f, 1.0f, 0.3f);
    patch->resonator_modulation_frequency = sanitize_range(params.resonator_modulation_frequency, 0.0f, 0.01f, 0.0f);
    patch->resonator_modulation_offset = sanitize_range(params.resonator_modulation_offset, 0.0f, 1.0f, 0.5f);
    patch->reverb_diffusion = sanitize_range(params.reverb_diffusion, 0.0f, 1.0f, 0.6f);
    patch->reverb_lp = sanitize_range(params.reverb_lp, 0.0f, 1.0f, 0.8f);
    patch->space = sanitize_range(params.space, 0.0f, 1.0f, 0.2f);
    patch->modulation_frequency = sanitize_range(params.modulation_frequency, 0.0f, 1.0f, 0.2f);

    std::vector<float> src_mono(src_num_samples_padded, 0.0f);
    const std::size_t gate_off = (src_num_samples * 2u) / 3u;

    std::array<float, kElementsBlockSize> blow{};
    std::array<float, kElementsBlockSize> strike{};
    std::array<float, kElementsBlockSize> main{};
    std::array<float, kElementsBlockSize> aux{};
    constexpr float kElementsOutputTrim = 0.72f;
    constexpr float kAuxMonoMix = 0.25f;
    constexpr float kBlockControlSmooth = 0.2f;

    float smoothed_note = note_base;
    float smoothed_mod = modulation_base;
    float smoothed_strength = strength_base;
    elements::Patch smoothed_patch = *patch;

    for (std::size_t block_start = 0; block_start < src_num_samples_padded; block_start += kElementsBlockSize) {
        const std::size_t block_n = std::min<std::size_t>(kElementsBlockSize, src_num_samples - block_start);

        const std::size_t mid_i = block_start + (block_n / 2u);
        const float env1_mid = envelope_value(env1, mid_i, src_num_samples, gate_off);
        const float env2_mid = envelope_value(env2, mid_i, src_num_samples, gate_off);
        const float env1_mid_mod = sanitize_range(env1_mid * env1.amount, -1.0f, 1.0f, 0.0f);
        const float env2_mid_mod = sanitize_range(env2_mid * env2.amount, -1.0f, 1.0f, 0.0f);

        float target_note = note_base;
        float target_mod = modulation_base;
        float target_strength = strength_base;
        float tmp_blow_cv = blow_cv_base;
        float tmp_strike_cv = strike_cv_base;
        elements::Patch target_patch = *patch;
        apply_cv_mod(env1.target, env1_mid_mod, &target_note, &target_mod, &target_strength, &tmp_blow_cv, &tmp_strike_cv, &target_patch);
        apply_cv_mod(env2.target, env2_mid_mod, &target_note, &target_mod, &target_strength, &tmp_blow_cv, &tmp_strike_cv, &target_patch);

        smoothed_note = lerp(smoothed_note, target_note, kBlockControlSmooth);
        smoothed_mod = lerp(smoothed_mod, target_mod, kBlockControlSmooth);
        smoothed_strength = lerp(smoothed_strength, target_strength, kBlockControlSmooth);

        smoothed_patch.exciter_bow_level = lerp(smoothed_patch.exciter_bow_level, target_patch.exciter_bow_level, kBlockControlSmooth);
        smoothed_patch.exciter_blow_level = lerp(smoothed_patch.exciter_blow_level, target_patch.exciter_blow_level, kBlockControlSmooth);
        smoothed_patch.exciter_strike_level = lerp(smoothed_patch.exciter_strike_level, target_patch.exciter_strike_level, kBlockControlSmooth);
        smoothed_patch.resonator_geometry = lerp(smoothed_patch.resonator_geometry, target_patch.resonator_geometry, kBlockControlSmooth);
        smoothed_patch.resonator_brightness = lerp(smoothed_patch.resonator_brightness, target_patch.resonator_brightness, kBlockControlSmooth);
        smoothed_patch.resonator_damping = lerp(smoothed_patch.resonator_damping, target_patch.resonator_damping, kBlockControlSmooth);
        smoothed_patch.resonator_position = lerp(smoothed_patch.resonator_position, target_patch.resonator_position, kBlockControlSmooth);
        smoothed_patch.space = lerp(smoothed_patch.space, target_patch.space, kBlockControlSmooth);

        smoothed_patch.exciter_bow_level = clamp01(smoothed_patch.exciter_bow_level);
        smoothed_patch.exciter_blow_level = clamp01(smoothed_patch.exciter_blow_level);
        smoothed_patch.exciter_strike_level = clamp01(smoothed_patch.exciter_strike_level);
        smoothed_patch.resonator_geometry = clamp01(smoothed_patch.resonator_geometry);
        smoothed_patch.resonator_brightness = clamp01(smoothed_patch.resonator_brightness);
        smoothed_patch.resonator_damping = clamp01(smoothed_patch.resonator_damping);
        smoothed_patch.resonator_position = clamp01(smoothed_patch.resonator_position);
        smoothed_patch.space = clamp01(smoothed_patch.space);

        *patch = smoothed_patch;

        for (std::size_t i = 0; i < block_n; ++i) {
            const std::size_t si = block_start + i;
            const float env1v = envelope_value(env1, si, src_num_samples, gate_off);
            const float env2v = envelope_value(env2, si, src_num_samples, gate_off);
            const float env1_amt = sanitize_range(env1.amount, -1.0f, 1.0f, 0.0f);
            const float env2_amt = sanitize_range(env2.amount, -1.0f, 1.0f, 0.0f);
            const float env1_mod = sanitize_range(env1v * env1_amt, -1.0f, 1.0f, 0.0f);
            const float env2_mod = sanitize_range(env2v * env2_amt, -1.0f, 1.0f, 0.0f);

            float blow_cv = blow_cv_base;
            float strike_cv = strike_cv_base;
            apply_cv_stream_mod(env1.target, env1_mod, &blow_cv, &strike_cv);
            apply_cv_stream_mod(env2.target, env2_mod, &blow_cv, &strike_cv);

            blow[i] = sanitize_range(blow_cv, 0.0f, 1.0f, 0.0f);
            strike[i] = sanitize_range(strike_cv, 0.0f, 1.0f, 0.0f);
        }

        for (std::size_t i = block_n; i < kElementsBlockSize; ++i) {
            blow[i] = 0.0f;
            strike[i] = 0.0f;
            main[i] = 0.0f;
            aux[i] = 0.0f;
        }

        elements::PerformanceState perf{};
        perf.gate = block_start < gate_off;
        perf.note = sanitize_range(smoothed_note, 0.0f, 127.0f, note_base);
        perf.modulation = sanitize_range(smoothed_mod, -48.0f, 48.0f, modulation_base);
        perf.strength = sanitize_range(smoothed_strength, 0.0f, 1.0f, strength_base);

        // Mutable Elements DSP is block-based and expects full 16-sample blocks.
        part.Process(perf, blow.data(), strike.data(), main.data(), aux.data(), kElementsBlockSize);

        for (std::size_t i = 0; i < block_n; ++i) {
            const float m = std::isfinite(main[i]) ? main[i] : 0.0f;
            const float a = std::isfinite(aux[i]) ? aux[i] : 0.0f;
            // Equal-summing main+aux can produce phasey reverb artifacts in mono.
            // Bias toward main and add a little aux for body.
            const float mono = ((1.0f - kAuxMonoMix) * m + (kAuxMonoMix * a)) * kElementsOutputTrim;
            src_mono[block_start + i] = std::isfinite(mono) ? mono : 0.0f;
        }
    }

    std::vector<std::uint8_t> out(output_num_samples);
    const float src_to_out = static_cast<float>(kElementsSampleRate) / static_cast<float>(output_sample_rate);
    for (std::size_t i = 0; i < output_num_samples; ++i) {
        const float src_pos = static_cast<float>(i) * src_to_out;
        const std::size_t i0 = static_cast<std::size_t>(std::floor(src_pos));
        const std::size_t i1 = std::min(i0 + 1u, src_mono.size() - 1u);
        const float frac = src_pos - static_cast<float>(i0);
        float s = lerp(src_mono[i0], src_mono[i1], frac);
        if (!std::isfinite(s)) {
            s = 0.0f;
        }
        s = std::clamp(s, -1.0f, 1.0f);
        out[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::round(s * 127.0f)));
    }

    return out;
}

}  // namespace drumrom::synth
