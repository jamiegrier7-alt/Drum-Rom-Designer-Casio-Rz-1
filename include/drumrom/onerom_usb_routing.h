#pragma once

#include <string>
#include <vector>

namespace drumrom::onerom_usb_routing {

std::vector<std::string> parse_serials_from_scan_output(const std::string& output);

bool slot_belongs_to_rom_a(const std::string& slot_name);
bool slot_belongs_to_rom_b(const std::string& slot_name);
bool slot_belongs_to_any_rom(const std::string& slot_name);

}  // namespace drumrom::onerom_usb_routing
