// Renders the settings page for global app, MIDI, and workflow preferences.
#include "drumrom/ui_settings_page.h"

#include <cstdio>

namespace drumrom::ui_settings_page {

void render(Model* model, const Actions& actions) {
    if (model == nullptr) {
        return;
    }

    ImGui::SeparatorText("Settings");
    ImGui::Spacing();

    ImGui::SeparatorText("MIDI Input");

    static bool midi_in_ports_refreshed = false;
    if (!midi_in_ports_refreshed && actions.refresh_midi_in_ports != nullptr) {
        actions.refresh_midi_in_ports();
        midi_in_ports_refreshed = true;
    }

    if (ImGui::Button("Refresh Input Ports##in") && actions.refresh_midi_in_ports != nullptr) {
        actions.refresh_midi_in_ports();
    }
    ImGui::Spacing();

    ImGui::Text("Available MIDI Input Ports:");
    ImGui::BeginListBox("##midi_in_ports", ImVec2(-1.0f, 120.0f));
    for (int i = 0; i < static_cast<int>(model->available_midi_in_ports->size()); ++i) {
        const bool is_selected = (*model->selected_midi_in_port == i);
        if (ImGui::Selectable((*model->available_midi_in_ports)[i].c_str(), is_selected)) {
            if (*model->selected_midi_in_port != i) {
                if (actions.open_midi_in_port != nullptr) {
                    actions.open_midi_in_port(i);
                }
                *model->settings_midi_in_port_index = i;
                if (actions.save_settings != nullptr) {
                    actions.save_settings();
                }
            }
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndListBox();

    ImGui::Spacing();
    ImGui::TextColored(model->status_color, "Status: %s", model->midi_in_status->c_str());

    if (*model->midi_in_enabled) {
        const float scaled_disconnect_input_width = 200.0f * model->ui_scale;
        if (ImGui::Button("Disconnect Input", ImVec2(scaled_disconnect_input_width, 0.0f))) {
            if (actions.close_midi_in_port != nullptr) {
                actions.close_midi_in_port();
            }
            *model->settings_midi_in_port_index = -1;
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("MIDI Output");

    static bool midi_out_ports_refreshed = false;
    if (!midi_out_ports_refreshed && actions.refresh_midi_out_ports != nullptr) {
        actions.refresh_midi_out_ports();
        midi_out_ports_refreshed = true;
    }

    if (ImGui::Button("Refresh Output Ports##out") && actions.refresh_midi_out_ports != nullptr) {
        actions.refresh_midi_out_ports();
    }
    ImGui::Spacing();

    ImGui::Text("Available MIDI Output Ports:");
    ImGui::BeginListBox("##midi_out_ports", ImVec2(-1.0f, 120.0f));
    for (int i = 0; i < static_cast<int>(model->available_midi_out_ports->size()); ++i) {
        const bool is_selected = (*model->selected_midi_out_port == i);
        if (ImGui::Selectable((*model->available_midi_out_ports)[i].c_str(), is_selected)) {
            if (*model->selected_midi_out_port != i) {
                if (actions.open_midi_out_port != nullptr) {
                    actions.open_midi_out_port(i);
                }
                *model->settings_midi_out_port_index = i;
                if (actions.save_settings != nullptr) {
                    actions.save_settings();
                }
            }
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndListBox();

    ImGui::Spacing();
    ImGui::TextColored(model->status_color, "Status: %s", model->midi_out_status->c_str());

    if (*model->midi_out_enabled) {
        const float scaled_disconnect_output_width = 200.0f * model->ui_scale;
        if (ImGui::Button("Disconnect Output", ImVec2(scaled_disconnect_output_width, 0.0f))) {
            if (actions.close_midi_out_port != nullptr) {
                actions.close_midi_out_port();
            }
            *model->settings_midi_out_port_index = -1;
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("OneROM USB Routing");

    static bool onerom_ports_refreshed = false;
    if (!onerom_ports_refreshed && actions.refresh_onerom_usb_devices != nullptr) {
        actions.refresh_onerom_usb_devices();
        onerom_ports_refreshed = true;
    }

    if (ImGui::Button("Scan OneROM USB Devices") && actions.refresh_onerom_usb_devices != nullptr) {
        actions.refresh_onerom_usb_devices();
    }

    const std::size_t device_count = model->available_onerom_serials != nullptr ? model->available_onerom_serials->size() : 0u;
    ImGui::Text("Detected OneROM devices: %zu", device_count);

    if (device_count == 0u) {
        ImGui::TextUnformatted("No OneROM USB devices detected. Uploads will use default onerom selection.");
    } else if (device_count == 1u) {
        ImGui::Text("Device: %s", (*model->available_onerom_serials)[0].c_str());
        static const char* single_roles[] = {"ROM A", "ROM B"};
        if (ImGui::Combo("Single Device Serves", model->onerom_single_device_role, single_roles, 2)) {
            if (*model->onerom_single_device_role == 0) {
                *model->selected_onerom_rom_a = 0;
                *model->selected_onerom_rom_b = -1;
            } else {
                *model->selected_onerom_rom_a = -1;
                *model->selected_onerom_rom_b = 0;
            }

            model->onerom_serial_rom_a[0] = '\0';
            model->onerom_serial_rom_b[0] = '\0';
            if (*model->selected_onerom_rom_a == 0) {
                std::snprintf(model->onerom_serial_rom_a, model->onerom_serial_rom_a_capacity, "%s", (*model->available_onerom_serials)[0].c_str());
            }
            if (*model->selected_onerom_rom_b == 0) {
                std::snprintf(model->onerom_serial_rom_b, model->onerom_serial_rom_b_capacity, "%s", (*model->available_onerom_serials)[0].c_str());
            }
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
    } else {
        auto render_assignment_combo = [&](const char* label, int* selected_index, char* out_serial, std::size_t out_capacity) {
            if (selected_index == nullptr || out_serial == nullptr || out_capacity == 0) {
                return;
            }

            std::vector<const char*> labels;
            labels.reserve(model->available_onerom_serials->size() + 1u);
            labels.push_back("(None)");
            for (const std::string& serial : *model->available_onerom_serials) {
                labels.push_back(serial.c_str());
            }

            int combo_index = (*selected_index >= 0) ? (*selected_index + 1) : 0;
            if (ImGui::Combo(label, &combo_index, labels.data(), static_cast<int>(labels.size()))) {
                *selected_index = combo_index - 1;
                if (*selected_index >= 0 && static_cast<std::size_t>(*selected_index) < model->available_onerom_serials->size()) {
                    std::snprintf(out_serial, out_capacity, "%s", (*model->available_onerom_serials)[static_cast<std::size_t>(*selected_index)].c_str());
                } else {
                    out_serial[0] = '\0';
                }
                if (actions.save_settings != nullptr) {
                    actions.save_settings();
                }
            }
        };

        render_assignment_combo("ROM A Device", model->selected_onerom_rom_a, model->onerom_serial_rom_a, model->onerom_serial_rom_a_capacity);
        render_assignment_combo("ROM B Device", model->selected_onerom_rom_b, model->onerom_serial_rom_b, model->onerom_serial_rom_b_capacity);
    }

    if (model->onerom_usb_status != nullptr && !model->onerom_usb_status->empty()) {
        ImGui::TextColored(model->status_color, "Status: %s", model->onerom_usb_status->c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("Folders");
    ImGui::Spacing();

    ImGui::Text("Samples Folder:");
    ImGui::InputText("##samples_folder", model->samples_folder, model->samples_folder_capacity, ImGuiInputTextFlags_ReadOnly);
    ImGui::TextUnformatted("Using workspace root samples/ directory");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("Display");
    ImGui::Spacing();

    ImGui::Text("Monitor Resolution:");
    int selected_monitor = -1;
    const int monitor_options[] = {
        1280, 720,
        1366, 768,
        1920, 1080,
        2560, 1440,
        3840, 2160
    };
    const char* monitor_labels[] = {
        "1280x720 (HD)",
        "1366x768 (Common Laptop)",
        "1920x1080 (Full HD)",
        "2560x1440 (2K)",
        "3840x2160 (4K)"
    };

    for (int i = 0; i < 5; ++i) {
        if (monitor_options[i * 2] == *model->monitor_width && monitor_options[i * 2 + 1] == *model->monitor_height) {
            selected_monitor = i;
            break;
        }
    }

    if (ImGui::Combo("##monitor_size", &selected_monitor, monitor_labels, 5)) {
        *model->monitor_width = monitor_options[selected_monitor * 2];
        *model->monitor_height = monitor_options[selected_monitor * 2 + 1];
        *model->window_resize_pending = true;
        *model->pending_window_width = *model->monitor_width;
        *model->pending_window_height = *model->monitor_height;
        if (actions.save_settings != nullptr) {
            actions.save_settings();
        }
    }

    ImGui::Text("Current: %dx%d", *model->monitor_width, *model->monitor_height);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("Loop Split");
    if (model->loop_split_reset_slots != nullptr) {
        if (ImGui::Checkbox("Reset slot settings on split", model->loop_split_reset_slots)) {
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
        ImGui::TextUnformatted("When enabled, loop splitting applies neutral defaults before assigning slices.");
    }
    if (model->loop_split_target_pads != nullptr) {
        const char* split_targets[] = {
            "12 pads (ignore Sample 1-4)",
            "16 pads (include all slots)",
        };
        int split_mode = (*model->loop_split_target_pads == 16) ? 1 : 0;
        if (ImGui::Combo("Split Target", &split_mode, split_targets, 2)) {
            *model->loop_split_target_pads = (split_mode == 1) ? 16 : 12;
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
    }
    if (model->loop_split_autofit != nullptr) {
        if (ImGui::Checkbox("Autofit regions to closest pad length", model->loop_split_autofit)) {
            if (actions.save_settings != nullptr) {
                actions.save_settings();
            }
        }
        ImGui::TextUnformatted("Assigns shorter transients to smaller pads and longer regions to larger pads.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("Kit Defaults");
    if (ImGui::Button("Restore Factory Default RZ-1 Kit", ImVec2(320.0f * model->ui_scale, 0.0f))) {
        const bool restored = actions.restore_default_rz1_kit_file != nullptr && actions.restore_default_rz1_kit_file();
        const bool loaded = actions.load_default_rz1_kit_into_editor != nullptr && actions.load_default_rz1_kit_into_editor();
        if (restored && loaded) {
            if (actions.set_status != nullptr) {
                actions.set_status("Factory default kit restored and loaded");
            }
            if (actions.initialize_history_if_needed != nullptr) {
                actions.initialize_history_if_needed();
            }
            if (actions.maybe_commit_history != nullptr) {
                actions.maybe_commit_history(true);
            }
        } else if (actions.set_status != nullptr) {
            actions.set_status("Failed to restore factory default kit");
        }
    }
    ImGui::TextUnformatted("Default kit file: kits/default-rz1-kit.kit");

    ImGui::Spacing();
    const float scaled_save_settings_width = 200.0f * model->ui_scale;
    if (ImGui::Button("Save Settings", ImVec2(scaled_save_settings_width, 0.0f)) && actions.save_settings != nullptr) {
        actions.save_settings();
    }
}

}  // namespace drumrom::ui_settings_page
