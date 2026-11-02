#pragma once

#include <cstddef>
#include <optional>

namespace grimlock {
// Opt-in diagnostics. CPU stages include their waits; GPU timestamps describe the preceding submitted frame.
struct FramePerformance {
    double frameMilliseconds = 0, gameMilliseconds = 0, animationMilliseconds = 0;
    double preparationMilliseconds = 0, renderMilliseconds = 0;
    double fenceWaitMilliseconds = 0, presentMilliseconds = 0;
    std::optional<double> gpuMilliseconds;
    std::size_t modelDraws = 0, primitiveDraws = 0, instances = 0, models = 0;
};
} // namespace grimlock
