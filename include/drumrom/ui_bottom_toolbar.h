#pragma once

#include "imgui.h"

#include <cstddef>
#include <string>
#include <vector>

namespace drumrom::ui_bottom_toolbar {

struct SlotItem {
    std::string label;
    bool is_sample_pad = false;
    bool is_enabled = true;
};

enum class RamSampleLayout {
    None = 0,
    Join12 = 1,
    Join34 = 2,
    Join12And34 = 3,
    JoinAll = 4,
};

struct Model {
    std::vector<SlotItem>* slots;
    std::size_t selected_slot = 0;
    bool show_slot_selection = true;
    float ui_scale = 1.0f;
    int current_page = 0;
    RamSampleLayout ram_sample_layout = RamSampleLayout::None;
};

struct Actions {
    void (*select_slot)(std::size_t slot_idx, bool trigger_preview);
    void (*set_page)(int page_index);
    void (*set_ram_sample_layout)(RamSampleLayout layout);
};

void render(Model* model, const Actions& actions);

}  // namespace drumrom::ui_bottom_toolbar
