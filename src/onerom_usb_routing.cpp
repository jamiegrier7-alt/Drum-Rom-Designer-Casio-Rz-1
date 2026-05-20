// USB routing/discovery helpers for communicating with OneROM hardware.
#include "drumrom/onerom_usb_routing.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace drumrom::onerom_usb_routing {

namespace {

std::string trim_copy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

}  // namespace

std::vector<std::string> parse_serials_from_scan_output(const std::string& output) {
    std::vector<std::string> serials;
    std::set<std::string> seen;

    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string line = trim_copy(
            end == std::string::npos ? output.substr(start) : output.substr(start, end - start));

        const std::size_t serial_pos = line.find("Serial:");
        if (serial_pos != std::string::npos) {
            std::string serial = trim_copy(line.substr(serial_pos + 7));
            const std::size_t state_pos = serial.find(" State:");
            if (state_pos != std::string::npos) {
                serial = trim_copy(serial.substr(0, state_pos));
            }
            if (!serial.empty() && serial != "N/A" && seen.insert(serial).second) {
                serials.push_back(serial);
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return serials;
}

bool slot_belongs_to_rom_a(const std::string& slot_name) {
    static constexpr std::array<const char*, 8> kRomASlots = {{
        "tom1", "tom2", "tom3", "kick", "snare", "rimshot", "closed_hihat", "open_hihat",
    }};
    return std::find(kRomASlots.begin(), kRomASlots.end(), slot_name) != kRomASlots.end();
}

bool slot_belongs_to_rom_b(const std::string& slot_name) {
    static constexpr std::array<const char*, 4> kRomBSlots = {{
        "clap", "ride", "cowbell", "crash",
    }};
    return std::find(kRomBSlots.begin(), kRomBSlots.end(), slot_name) != kRomBSlots.end();
}

bool slot_belongs_to_any_rom(const std::string& slot_name) {
    return slot_belongs_to_rom_a(slot_name) || slot_belongs_to_rom_b(slot_name);
}

}  // namespace drumrom::onerom_usb_routing
