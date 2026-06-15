#pragma once

namespace drumrom {

enum class RamSampleLayout {
    None = 0,
    Join12 = 1,
    Join34 = 2,
    Join12And34 = 3,
    JoinAll = 4,
};

}  // namespace drumrom
