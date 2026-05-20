#pragma once

#include "imgui.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace drumrom::ui_pin_matrix_page {

constexpr std::size_t kDefaultEpromPinCount = 28;

struct Model {
    std::array<std::array<std::uint8_t, kDefaultEpromPinCount>, kDefaultEpromPinCount>* pinbend_matrix;
    const std::array<const char*, kDefaultEpromPinCount>* pin_labels;
    float ui_scale;
    ImVec4 color_green;
    ImVec4 color_green_dark;
    ImVec4 color_green_lite;
};

struct Actions {
    void (*set_status)(const std::string& msg);
    bool (*send_pin_matrix_to_onerom_usb)();
};

void render(Model* model, const Actions& actions);

}  // namespace drumrom::ui_pin_matrix_page
