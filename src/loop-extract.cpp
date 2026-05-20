// Loop analysis and slicing helpers used to split loops across drum slots.
// loop-extract.cpp
// Included directly by main_imgui.cpp inside its anonymous namespace.
// Provides loop file scanning and loop-slice-across-slots functionality.

struct LoopSplitAnalysis {
    std::vector<float> mono;
    std::vector<std::size_t> boundaries;
    std::vector<std::size_t> target_slots;
    std::vector<std::size_t> assigned_slots;
    int source_rate_hz = kSampleRate;
};

std::vector<std::size_t> get_loop_split_target_slots() {
    std::vector<std::size_t> slots;
    if (g_settings.loop_split_target_pads == 12) {
        slots.reserve(kSlots.size());
        for (std::size_t i = 0; i < kSlots.size(); ++i) {
            if (!kSlots[i].is_ram_sample) {
                slots.push_back(i);
            }
        }
    } else {
        slots.reserve(kSlots.size());
        for (std::size_t i = 0; i < kSlots.size(); ++i) {
            slots.push_back(i);
        }
    }
    if (slots.empty()) {
        slots.push_back(0);
    }
    return slots;
}

std::vector<std::size_t> build_loop_split_slot_assignment(const std::vector<std::size_t>& target_slots,
                                                          const std::vector<std::size_t>& boundaries) {
    std::vector<std::size_t> assignment = target_slots;
    if (!g_settings.loop_split_autofit) {
        return assignment;
    }
    if (target_slots.empty() || boundaries.size() < 2 || (boundaries.size() - 1) != target_slots.size()) {
        return assignment;
    }

    const std::size_t region_count = boundaries.size() - 1;
    std::vector<std::size_t> region_order(region_count);
    for (std::size_t i = 0; i < region_count; ++i) {
        region_order[i] = i;
    }
    std::sort(region_order.begin(), region_order.end(), [&](std::size_t a, std::size_t b) {
        const std::size_t len_a = boundaries[a + 1] > boundaries[a] ? (boundaries[a + 1] - boundaries[a]) : 0;
        const std::size_t len_b = boundaries[b + 1] > boundaries[b] ? (boundaries[b + 1] - boundaries[b]) : 0;
        if (len_a == len_b) {
            return a < b;
        }
        return len_a < len_b;
    });

    std::vector<std::size_t> slot_order = target_slots;
    std::sort(slot_order.begin(), slot_order.end(), [&](std::size_t a, std::size_t b) {
        if (kSlots[a].size == kSlots[b].size) {
            return a < b;
        }
        return kSlots[a].size < kSlots[b].size;
    });

    assignment.assign(region_count, 0);
    for (std::size_t rank = 0; rank < region_count; ++rank) {
        assignment[region_order[rank]] = slot_order[rank];
    }
    return assignment;
}

void refresh_loop_files() {
    const std::filesystem::path sample_root = sample_root_path();
    if (!std::filesystem::exists(sample_root) || !std::filesystem::is_directory(sample_root)) {
        g_loop_files.clear();
        g_selected_loop_file = -1;
        g_selected_loop_folder_path.clear();
        return;
    }

    std::filesystem::path initial_folder;
    if (g_selected_loop_folder_path.empty()) {
        const std::filesystem::path preferred_loop_root = sample_root / "loops";
        if (std::filesystem::exists(preferred_loop_root) && std::filesystem::is_directory(preferred_loop_root)) {
            initial_folder = preferred_loop_root;
        } else {
            initial_folder = sample_root;
        }
        g_selected_loop_folder_path = initial_folder.lexically_normal().string();
    }

    drumrom::sample_browser_fs::refresh_files_for_folder(
        sample_root,
        &g_selected_loop_folder_path,
        &g_loop_files,
        &g_selected_loop_file);
}

std::vector<std::size_t> detect_loop_slice_boundaries(const std::vector<float>& mono, std::size_t num_slices) {
    const std::size_t slices = std::max<std::size_t>(1, num_slices);
    std::vector<std::size_t> boundaries;
    boundaries.reserve(slices + 1);

    if (mono.size() < 8) {
        for (std::size_t i = 0; i <= slices; ++i) {
            boundaries.push_back((i * std::max<std::size_t>(1, mono.size() - 1)) / slices);
        }
        return boundaries;
    }

    std::vector<float> env(mono.size(), 0.0f);
    float e = 0.0f;
    const float smooth = 0.96f;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const float a = std::abs(mono[i]);
        e = (smooth * e) + ((1.0f - smooth) * a);
        env[i] = e;
    }

    std::vector<std::pair<float, std::size_t>> candidates;
    candidates.reserve(mono.size() / 8);
    const std::size_t edge = std::max<std::size_t>(1, mono.size() / 200);
    for (std::size_t i = edge + 1; i + edge + 1 < env.size(); ++i) {
        const float prev = env[i - 1];
        const float cur = env[i];
        const float next = env[i + 1];
        const float onset = std::max(0.0f, cur - prev);
        if (onset > 0.002f && cur >= prev && cur >= next) {
            candidates.push_back({onset, i});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    const std::size_t min_gap = std::max<std::size_t>(8, mono.size() / (slices * 3));
    std::vector<std::size_t> selected;
    selected.reserve(slices - 1);
    for (const auto& c : candidates) {
        if (selected.size() >= slices - 1) {
            break;
        }
        const std::size_t idx = c.second;
        bool too_close = false;
        for (std::size_t s : selected) {
            const std::size_t d = (s > idx) ? (s - idx) : (idx - s);
            if (d < min_gap) {
                too_close = true;
                break;
            }
        }
        if (!too_close) {
            selected.push_back(idx);
        }
    }

    for (std::size_t i = 1; selected.size() < slices - 1 && i < slices; ++i) {
        const std::size_t idx = (i * (mono.size() - 1)) / slices;
        bool too_close = false;
        for (std::size_t s : selected) {
            const std::size_t d = (s > idx) ? (s - idx) : (idx - s);
            if (d < min_gap) {
                too_close = true;
                break;
            }
        }
        if (!too_close) {
            selected.push_back(idx);
        }
    }

    std::sort(selected.begin(), selected.end());
    boundaries.push_back(0);
    for (std::size_t s : selected) {
        boundaries.push_back(s);
    }
    boundaries.push_back(mono.size() - 1);

    while (boundaries.size() < slices + 1) {
        const std::size_t i = boundaries.size();
        boundaries.push_back((i * (mono.size() - 1)) / slices);
    }
    if (boundaries.size() > slices + 1) {
        boundaries.resize(slices + 1);
        boundaries.back() = mono.size() - 1;
    }

    for (std::size_t i = 1; i < boundaries.size(); ++i) {
        boundaries[i] = std::max(boundaries[i], boundaries[i - 1] + 1);
        boundaries[i] = std::min(boundaries[i], mono.size() - 1);
    }
    boundaries.front() = 0;
    boundaries.back() = mono.size() - 1;
    return boundaries;
}

std::optional<LoopSplitAnalysis> analyze_loop_for_split(const std::string& loop_path, std::size_t num_slices, std::string* error_message = nullptr) {
    if (error_message != nullptr) {
        error_message->clear();
    }

    if (loop_path.empty()) {
        if (error_message != nullptr) {
            *error_message = "Select a loop file first";
        }
        return std::nullopt;
    }

    SampleEdit tmp;
    tmp.path = loop_path;
    auto loaded = load_sample_any_format(tmp);
    if (!loaded.has_value()) {
        if (error_message != nullptr) {
            *error_message = "Loop load failed";
        }
        return std::nullopt;
    }

    auto mono = resample_rate_linear(loaded->first, loaded->second, kSampleRate);
    if (mono.empty()) {
        if (error_message != nullptr) {
            *error_message = "Loop decode produced no audio";
        }
        return std::nullopt;
    }

    auto boundaries = detect_loop_slice_boundaries(mono, num_slices);
    if (boundaries.size() < (num_slices + 1)) {
        if (error_message != nullptr) {
            *error_message = "Could not split loop";
        }
        return std::nullopt;
    }

    LoopSplitAnalysis analysis;
    analysis.mono = std::move(mono);
    analysis.boundaries = std::move(boundaries);
    analysis.target_slots = get_loop_split_target_slots();
    analysis.assigned_slots = build_loop_split_slot_assignment(analysis.target_slots, analysis.boundaries);
    analysis.source_rate_hz = std::max(1, loaded->second);
    return analysis;
}

void apply_loop_split_default_profile(SlotConfig* slot, const std::string& loop_path, int source_rate_hz, int start_pct, int end_pct) {
    if (slot == nullptr) {
        return;
    }

    slot->source = SourceKind::Loop;
    slot->sample.path = loop_path;
    slot->sample.source_rate_hz = static_cast<float>(std::max(1, source_rate_hz));
    slot->sample.start_pct = start_pct;
    slot->sample.end_pct = end_pct;
    slot->sample.loop_start_pct = 0;
    slot->sample.loop_end_pct = 100;
    slot->sample.loop_increment_pct = 0.0f;
    slot->sample.tune_semitones = 0.0f;
    slot->sample.filter_cutoff_hz = 12000.0f;
    slot->sample.filter_cutoff_end_hz = 12000.0f;
    slot->sample.filter_env_decay_s = 2.00f;
    slot->sample.filter_resonance = 0.0f;
    slot->sample.amp_attack_s = 0.0f;
    slot->sample.amp_decay_s = 0.0f;
    slot->sample.amp_sustain = 1.0f;
    slot->sample.amp_release_s = 0.0f;
    slot->sample.amp_envelope_mode = AmpEnvelopeMode::Off;
    slot->output_gain_db = 0.0f;
    slot->limiter_ceiling = 1.0f;
    slot->output_shaper_mode = 0;
    slot->output_saturation = 0.0f;
}

bool split_loop_across_slots(const std::string& loop_path) {
    const auto target_slots = get_loop_split_target_slots();
    std::string error_message;
    auto analysis = analyze_loop_for_split(loop_path, target_slots.size(), &error_message);
    if (!analysis.has_value()) {
        set_status(error_message.empty() ? "Could not split loop" : error_message);
        return false;
    }

    g_loop_split_boundaries = analysis->boundaries;
    g_loop_split_waveform_length = analysis->mono.size();
    g_loop_split_slot_indices = analysis->assigned_slots;
    g_loop_split_selected_region = -1;
    for (std::size_t i = 0; i < g_loop_split_slot_indices.size(); ++i) {
        if (g_loop_split_slot_indices[i] == g_selected_slot) {
            g_loop_split_selected_region = static_cast<int>(i);
            break;
        }
    }
    if (g_loop_split_selected_region < 0 && !g_loop_split_slot_indices.empty()) {
        g_loop_split_selected_region = 0;
    }
    g_loop_split_drag_boundary = -1;
    g_loop_split_cached_path = loop_path;
    g_loop_split_cached_target_pads = g_settings.loop_split_target_pads;
    g_loop_split_cached_autofit = g_settings.loop_split_autofit;

    const std::size_t max_index = std::max<std::size_t>(1, analysis->mono.size() - 1);
    for (std::size_t i = 0; i < analysis->assigned_slots.size(); ++i) {
        SlotConfig& slot = g_slot_cfg[analysis->assigned_slots[i]];
        const std::size_t b0 = std::min(analysis->boundaries[i], max_index);
        const std::size_t b1 = std::min(analysis->boundaries[i + 1], max_index);
        int start_pct = static_cast<int>((b0 * 100u) / max_index);
        int end_pct = static_cast<int>((b1 * 100u) / max_index);
        start_pct = std::clamp(start_pct, 0, 99);
        end_pct = std::clamp(end_pct, start_pct + 1, 100);

        if (g_settings.loop_split_reset_slots) {
            apply_loop_split_default_profile(&slot, loop_path, analysis->source_rate_hz, start_pct, end_pct);
        } else {
            slot.source = SourceKind::Loop;
            slot.sample.path = loop_path;
            slot.sample.source_rate_hz = static_cast<float>(analysis->source_rate_hz);
            slot.sample.start_pct = start_pct;
            slot.sample.end_pct = end_pct;
        }
    }

    g_sample_path_slot = static_cast<std::size_t>(-1);
    g_wave_preview_dirty = true;
    g_params_dirty = true;
    g_auto_upload_commit_requested = true;
    g_auto_play_commit_requested = true;
    set_status(g_settings.loop_split_target_pads == 12 ? "Loop sliced across 12 pads" : "Loop sliced across 16 pads");
    return true;
}
