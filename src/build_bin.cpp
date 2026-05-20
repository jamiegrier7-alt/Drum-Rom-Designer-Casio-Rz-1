// CLI tool that builds ROM BIN images from sample folders and slot maps.
#include "drumrom/synth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Slot {
    std::string name;
    std::size_t start;
    std::size_t end;
    std::string source;

    std::size_t size() const {
        return (end - start) + 1;
    }
};

struct Config {
    std::size_t size_bytes = 0;
    std::vector<Slot> slots;
};

struct Options {
    std::string map_path;
    std::string sample_dir = "samples";
    std::string out_path = "roms/drumrom_27c256.bin";
    int fill = 128;
    bool make_factory = false;
    int sample_rate = 20833;
    bool strict = false;
    bool list_required = false;
};

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " --map <path> [options]\n"
        << "Options:\n"
        << "  --sample-dir <path>   Raw sample directory (default: samples)\n"
        << "  --out <path>          Output .bin path (default: roms/drumrom_27c256.bin)\n"
        << "  --fill <0..255>       Fill byte for unused space (default: 128)\n"
        << "  --make-factory        Generate kick/snare/hihat/tom/clap raw files\n"
        << "  --sample-rate <hz>    Factory sample rate (default: 20833 - Casio RZ-1 native)\n"
        << "  --strict              Fail if any slot source file is missing\n"
        << "  --list-required       Print unique required source filenames from map\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };

        if (a == "--map") {
            opt.map_path = require_value("--map");
        } else if (a == "--sample-dir") {
            opt.sample_dir = require_value("--sample-dir");
        } else if (a == "--out") {
            opt.out_path = require_value("--out");
        } else if (a == "--fill") {
            opt.fill = std::stoi(require_value("--fill"));
        } else if (a == "--sample-rate") {
            opt.sample_rate = std::stoi(require_value("--sample-rate"));
        } else if (a == "--make-factory") {
            opt.make_factory = true;
        } else if (a == "--strict") {
            opt.strict = true;
        } else if (a == "--list-required") {
            opt.list_required = true;
        } else if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + a);
        }
    }

    if (opt.map_path.empty()) {
        throw std::runtime_error("--map is required");
    }
    if (opt.fill < 0 || opt.fill > 255) {
        throw std::runtime_error("--fill must be in [0, 255]");
    }
    if (opt.sample_rate <= 1000) {
        throw std::runtime_error("--sample-rate must be > 1000");
    }
    return opt;
}

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Config parse_config(const std::string& json_text) {
    Config cfg;

    std::smatch m;
    const std::regex size_re(R"("size_bytes"\s*:\s*(\d+))");
    if (!std::regex_search(json_text, m, size_re)) {
        throw std::runtime_error("size_bytes not found in map JSON");
    }
    cfg.size_bytes = static_cast<std::size_t>(std::stoul(m[1].str()));

    const std::regex slot_re(
        R"DR(\{\s*"name"\s*:\s*"([^"]+)"\s*,\s*"start"\s*:\s*(\d+)\s*,\s*"end"\s*:\s*(\d+)\s*,\s*"source"\s*:\s*"([^"]+)"\s*\})DR");
    auto begin = std::sregex_iterator(json_text.begin(), json_text.end(), slot_re);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const auto& sm = *it;
        Slot s;
        s.name = sm[1].str();
        s.start = static_cast<std::size_t>(std::stoul(sm[2].str()));
        s.end = static_cast<std::size_t>(std::stoul(sm[3].str()));
        s.source = sm[4].str();
        if (s.end < s.start) {
            throw std::runtime_error("Invalid slot range for " + s.name);
        }
        if (s.end >= cfg.size_bytes) {
            throw std::runtime_error("Slot exceeds image size: " + s.name);
        }
        cfg.slots.push_back(s);
    }

    if (cfg.slots.empty()) {
        throw std::runtime_error("No slots found in map JSON");
    }
    return cfg;
}

void write_file_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot write file: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open sample: " + path.string());
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void make_factory_set(const fs::path& sample_dir, int sample_rate) {
    std::mt19937 rng(0xD12U);
    write_file_bytes(sample_dir / "kick.raw", drumrom::synth::synthesize_kick(sample_rate, static_cast<std::size_t>(sample_rate * 0.22f), rng));
    write_file_bytes(sample_dir / "snare.raw", drumrom::synth::synthesize_snare(sample_rate, static_cast<std::size_t>(sample_rate * 0.18f), rng));
    write_file_bytes(sample_dir / "hihat.raw", drumrom::synth::synthesize_hihat(sample_rate, static_cast<std::size_t>(sample_rate * 0.09f), rng));
    write_file_bytes(sample_dir / "tom.raw", drumrom::synth::synthesize_tom(sample_rate, static_cast<std::size_t>(sample_rate * 0.20f), rng));
    write_file_bytes(sample_dir / "clap.raw", drumrom::synth::synthesize_clap(sample_rate, static_cast<std::size_t>(sample_rate * 0.16f), rng));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse_args(argc, argv);
        const Config cfg = parse_config(read_text_file(opt.map_path));

        if (opt.list_required) {
            std::vector<std::string> names;
            names.reserve(cfg.slots.size());
            for (const auto& slot : cfg.slots) {
                names.push_back(slot.source);
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            for (const auto& name : names) {
                std::cout << name << "\n";
            }
            return 0;
        }

        const fs::path sample_dir(opt.sample_dir);
        if (opt.make_factory) {
            make_factory_set(sample_dir, opt.sample_rate);
        }

        std::vector<std::uint8_t> image(cfg.size_bytes, static_cast<std::uint8_t>(opt.fill));
        for (const auto& slot : cfg.slots) {
            const fs::path source_path = sample_dir / slot.source;
            std::vector<std::uint8_t> raw;
            bool used_sine_fill = false;
            if (fs::exists(source_path)) {
                raw = read_file_bytes(source_path);
                if (raw.size() > slot.size()) {
                    std::cerr << "Warning: sample " << source_path << " is " << raw.size()
                              << " bytes, truncating to slot size " << slot.size() << "\n";
                } else if (raw.size() < slot.size()) {
                    std::cerr << "Warning: sample " << source_path << " is " << raw.size()
                              << " bytes, padding to slot size " << slot.size() << "\n";
                }
            } else {
                if (opt.strict) {
                    throw std::runtime_error("Missing required sample in strict mode: " + source_path.string());
                }
                std::cerr << "Warning: missing sample " << source_path
                          << ", filling slot with 50Hz sine wave\n";
                used_sine_fill = true;
            }
            const auto fitted = used_sine_fill
                ? drumrom::synth::generate_sine(slot.size(), opt.sample_rate, 50.0f)
                : drumrom::synth::fit_slot(raw, slot.size(), static_cast<std::uint8_t>(opt.fill), opt.sample_rate);
            std::copy(fitted.begin(), fitted.end(), image.begin() + static_cast<std::ptrdiff_t>(slot.start));
            const std::string source_desc = used_sine_fill ? "50Hz sine fill" : slot.source;
            std::cout << slot.name << ": " << slot.start << ".." << slot.end
                      << " (" << slot.size() << " bytes) <- " << source_desc << "\n";
        }

        const fs::path out_path(opt.out_path);
        write_file_bytes(out_path, image);
        std::cout << "Wrote " << out_path << " (" << image.size() << " bytes)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }
}