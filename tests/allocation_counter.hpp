#pragma once

#include <cstdint>

namespace grimlock::checks {

struct Allocations {
    std::uint64_t count = 0, bytes = 0;
};

class AllocationFailure final {
public:
    explicit AllocationFailure(std::int64_t successfulCalls) noexcept;
    ~AllocationFailure();
    AllocationFailure(const AllocationFailure&) = delete;
    AllocationFailure& operator=(const AllocationFailure&) = delete;

private:
    std::int64_t previous_;
};

// Test binaries only: counts successful C++ new calls on this thread, including aligned/array forms.
class AllocationScope final {
public:
    AllocationScope() noexcept;
    ~AllocationScope();
    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;
    [[nodiscard]] Allocations stop() noexcept;

private:
    Allocations before_, result_;
    bool previous_ = false, active_ = true;
};

} // namespace grimlock::checks
