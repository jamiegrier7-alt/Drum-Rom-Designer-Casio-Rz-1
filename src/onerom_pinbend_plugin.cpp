// C++ wrapper/adapter for integrating the OneROM pin-bend plugin interface.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace drumrom::onerom {

constexpr std::size_t kPinCount = 28;

enum class LineLevel : std::int8_t {
    Floating = -1,
    Low = 0,
    High = 1,
};

enum class ContentionPolicy : std::uint8_t {
    // If both low and high are present in a connected net, resolve to high.
    DominantHigh = 0,
    // If both low and high are present in a connected net, resolve to low.
    DominantLow = 1,
    // If both low and high are present, treat as undefined/floating.
    UndefinedFloating = 2,
};

class PinBendMatrix {
public:
    using Matrix = std::array<std::array<std::uint8_t, kPinCount>, kPinCount>;

    PinBendMatrix() {
        clear();
    }

    void clear() {
        for (std::size_t i = 0; i < kPinCount; ++i) {
            drives_[i] = LineLevel::Floating;
            resolved_[i] = LineLevel::Floating;
            for (std::size_t j = 0; j < kPinCount; ++j) {
                connections_[i][j] = false;
            }
        }
    }

    bool set_connection(std::size_t a, std::size_t b, bool connected) {
        if (!in_range(a) || !in_range(b) || a == b) {
            return false;
        }
        connections_[a][b] = connected;
        connections_[b][a] = connected;
        return true;
    }

    bool set_matrix_01(const Matrix& matrix) {
        clear_connections();
        for (std::size_t i = 0; i < kPinCount; ++i) {
            for (std::size_t j = 0; j < kPinCount; ++j) {
                if (i == j) {
                    continue;
                }
                const bool connected = matrix[i][j] != 0;
                connections_[i][j] = connected;
                connections_[j][i] = connected;
            }
        }
        return true;
    }

    bool set_drive(std::size_t pin, LineLevel level) {
        if (!in_range(pin)) {
            return false;
        }
        drives_[pin] = level;
        return true;
    }

    void set_contention_policy(ContentionPolicy policy) {
        policy_ = policy;
    }

    void resolve() {
        std::array<bool, kPinCount> visited{};

        for (std::size_t start = 0; start < kPinCount; ++start) {
            if (visited[start]) {
                continue;
            }

            std::vector<std::size_t> stack;
            std::vector<std::size_t> component;
            stack.push_back(start);
            visited[start] = true;

            while (!stack.empty()) {
                const std::size_t p = stack.back();
                stack.pop_back();
                component.push_back(p);

                for (std::size_t q = 0; q < kPinCount; ++q) {
                    if (visited[q]) {
                        continue;
                    }
                    if (connections_[p][q]) {
                        visited[q] = true;
                        stack.push_back(q);
                    }
                }
            }

            bool any_high = false;
            bool any_low = false;
            for (const std::size_t p : component) {
                if (drives_[p] == LineLevel::High) {
                    any_high = true;
                } else if (drives_[p] == LineLevel::Low) {
                    any_low = true;
                }
            }

            const LineLevel mixed = mix_component(any_high, any_low);
            for (const std::size_t p : component) {
                resolved_[p] = mixed;
            }
        }
    }

    [[nodiscard]] LineLevel resolved_level(std::size_t pin) const {
        if (!in_range(pin)) {
            return LineLevel::Floating;
        }
        return resolved_[pin];
    }

private:
    static bool in_range(std::size_t pin) {
        return pin < kPinCount;
    }

    void clear_connections() {
        for (std::size_t i = 0; i < kPinCount; ++i) {
            for (std::size_t j = 0; j < kPinCount; ++j) {
                connections_[i][j] = false;
            }
        }
    }

    [[nodiscard]] LineLevel mix_component(bool any_high, bool any_low) const {
        if (any_high && any_low) {
            switch (policy_) {
                case ContentionPolicy::DominantHigh:
                    return LineLevel::High;
                case ContentionPolicy::DominantLow:
                    return LineLevel::Low;
                case ContentionPolicy::UndefinedFloating:
                    return LineLevel::Floating;
            }
        }
        if (any_high) {
            return LineLevel::High;
        }
        if (any_low) {
            return LineLevel::Low;
        }
        return LineLevel::Floating;
    }

    std::array<std::array<bool, kPinCount>, kPinCount> connections_{};
    std::array<LineLevel, kPinCount> drives_{};
    std::array<LineLevel, kPinCount> resolved_{};
    ContentionPolicy policy_ = ContentionPolicy::UndefinedFloating;
};

}  // namespace drumrom::onerom

// Plugin-style C API shim.
//
// These symbols are intentionally generic and can be adapted to the exact
// OneROM plugin ABI once available.
extern "C" {

struct OneromPinbendHandle {
    drumrom::onerom::PinBendMatrix matrix;
};

const char* onerom_pinbend_plugin_name() {
    return "virtual-pinbend-matrix-28x28";
}

OneromPinbendHandle* onerom_pinbend_create() {
    return new OneromPinbendHandle();
}

void onerom_pinbend_destroy(OneromPinbendHandle* handle) {
    delete handle;
}

int onerom_pinbend_set_connection(OneromPinbendHandle* handle, std::size_t pin_a, std::size_t pin_b, int connected) {
    if (handle == nullptr) {
        return 0;
    }
    return handle->matrix.set_connection(pin_a, pin_b, connected != 0) ? 1 : 0;
}

int onerom_pinbend_set_pin_level(OneromPinbendHandle* handle, std::size_t pin, int level) {
    if (handle == nullptr) {
        return 0;
    }

    using drumrom::onerom::LineLevel;
    LineLevel mapped = LineLevel::Floating;
    if (level < 0) {
        mapped = LineLevel::Floating;
    } else if (level == 0) {
        mapped = LineLevel::Low;
    } else {
        mapped = LineLevel::High;
    }

    return handle->matrix.set_drive(pin, mapped) ? 1 : 0;
}

void onerom_pinbend_set_policy(OneromPinbendHandle* handle, int policy) {
    if (handle == nullptr) {
        return;
    }

    using drumrom::onerom::ContentionPolicy;
    ContentionPolicy mapped = ContentionPolicy::UndefinedFloating;
    if (policy <= 0) {
        mapped = ContentionPolicy::DominantHigh;
    } else if (policy == 1) {
        mapped = ContentionPolicy::DominantLow;
    }

    handle->matrix.set_contention_policy(mapped);
}

void onerom_pinbend_resolve(OneromPinbendHandle* handle) {
    if (handle == nullptr) {
        return;
    }
    handle->matrix.resolve();
}

int onerom_pinbend_get_pin_level(const OneromPinbendHandle* handle, std::size_t pin) {
    if (handle == nullptr) {
        return -1;
    }

    using drumrom::onerom::LineLevel;
    switch (handle->matrix.resolved_level(pin)) {
        case LineLevel::Low:
            return 0;
        case LineLevel::High:
            return 1;
        case LineLevel::Floating:
            return -1;
    }
    return -1;
}

}  // extern "C"
