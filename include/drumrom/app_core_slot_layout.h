#pragma once

#include "drumrom/ram_sample_layout.h"

#include <array>
#include <cstddef>

namespace drumrom::app_core_slot_layout {

struct SlotDef {
    const char* name;
    const char* label;
    std::size_t size;
    bool is_ram_sample;
};

std::size_t get_slot_capacity(const std::array<SlotDef, 16>& slots,
                              std::size_t slot_idx,
                              drumrom::RamSampleLayout ram_sample_layout);

bool is_slot_enabled(const std::array<SlotDef, 16>& slots,
                     std::size_t slot_idx,
                     drumrom::RamSampleLayout ram_sample_layout);

std::size_t selected_slot_after_layout_change(
    const std::array<SlotDef, 16>& slots,
    drumrom::RamSampleLayout ram_sample_layout,
    std::size_t selected_slot);

std::size_t normalized_selected_slot(
    const std::array<SlotDef, 16>& slots,
    drumrom::RamSampleLayout ram_sample_layout,
    std::size_t selected_slot);

}  // namespace drumrom::app_core_slot_layout
