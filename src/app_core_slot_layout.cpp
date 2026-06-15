#include "drumrom/app_core_slot_layout.h"

#include <string>

namespace drumrom::app_core_slot_layout {

namespace {

constexpr std::size_t kRamSampleBaseSize = 4096;
constexpr std::size_t kRamSampleTotalSize = kRamSampleBaseSize * 4;
constexpr std::size_t kRamSampleMetadataBytes = 1;

}  // namespace

std::size_t get_slot_capacity(const std::array<SlotDef, 16>& slots,
                              std::size_t slot_idx,
                              drumrom::RamSampleLayout ram_sample_layout) {
    if (slot_idx >= slots.size()) {
        return 0;
    }

    const std::string slot_name = slots[slot_idx].name;
    if (!slots[slot_idx].is_ram_sample) {
        return slots[slot_idx].size;
    }

    if (slot_name == "sample1") {
        if (ram_sample_layout == drumrom::RamSampleLayout::Join12) {
            return kRamSampleBaseSize * 2;
        }
        if (ram_sample_layout == drumrom::RamSampleLayout::Join12And34) {
            return kRamSampleBaseSize * 2;
        }
        if (ram_sample_layout == drumrom::RamSampleLayout::JoinAll) {
            return kRamSampleTotalSize - kRamSampleMetadataBytes;
        }
        return kRamSampleBaseSize;
    }
    if (slot_name == "sample2") {
        if (ram_sample_layout == drumrom::RamSampleLayout::Join12 ||
            ram_sample_layout == drumrom::RamSampleLayout::Join12And34 ||
            ram_sample_layout == drumrom::RamSampleLayout::JoinAll) {
            return 0;
        }
        return kRamSampleBaseSize;
    }
    if (slot_name == "sample3") {
        if (ram_sample_layout == drumrom::RamSampleLayout::Join34) {
            return (kRamSampleBaseSize * 2) - kRamSampleMetadataBytes;
        }
        if (ram_sample_layout == drumrom::RamSampleLayout::Join12And34) {
            return (kRamSampleBaseSize * 2) - kRamSampleMetadataBytes;
        }
        return kRamSampleBaseSize;
    }
    if (slot_name == "sample4") {
        if (ram_sample_layout == drumrom::RamSampleLayout::Join34 ||
            ram_sample_layout == drumrom::RamSampleLayout::Join12And34 ||
            ram_sample_layout == drumrom::RamSampleLayout::JoinAll) {
            return 0;
        }
        return kRamSampleBaseSize - kRamSampleMetadataBytes;
    }

    return slots[slot_idx].size;
}

bool is_slot_enabled(const std::array<SlotDef, 16>& slots,
                     std::size_t slot_idx,
                     drumrom::RamSampleLayout ram_sample_layout) {
    if (slot_idx >= slots.size()) {
        return false;
    }
    if (!slots[slot_idx].is_ram_sample) {
        return true;
    }
    if (ram_sample_layout == drumrom::RamSampleLayout::JoinAll) {
        return slot_idx == 6;
    }
    return get_slot_capacity(slots, slot_idx, ram_sample_layout) > 0;
}

std::size_t selected_slot_after_layout_change(
    const std::array<SlotDef, 16>& slots,
    drumrom::RamSampleLayout ram_sample_layout,
    std::size_t selected_slot) {
    if (ram_sample_layout == drumrom::RamSampleLayout::JoinAll) {
        return 6;
    }

    if (is_slot_enabled(slots, selected_slot, ram_sample_layout)) {
        return selected_slot;
    }

    if (ram_sample_layout == drumrom::RamSampleLayout::Join34 ||
        ram_sample_layout == drumrom::RamSampleLayout::Join12And34) {
        return 7;
    }
    return 6;
}

std::size_t normalized_selected_slot(
    const std::array<SlotDef, 16>& slots,
    drumrom::RamSampleLayout ram_sample_layout,
    std::size_t selected_slot) {
    if (slots.empty()) {
        return 0;
    }

    const std::size_t clamped_slot = (selected_slot < slots.size()) ? selected_slot : (slots.size() - 1);
    if (is_slot_enabled(slots, clamped_slot, ram_sample_layout)) {
        return clamped_slot;
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (is_slot_enabled(slots, i, ram_sample_layout)) {
            return i;
        }
    }

    return 0;
}

}  // namespace drumrom::app_core_slot_layout
