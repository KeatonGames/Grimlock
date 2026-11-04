#include "allocation_counter.hpp"

#include <cstdlib>
#include <limits>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool counting = false;
thread_local std::int64_t failureAfter = -1;
thread_local grimlock::checks::Allocations allocations;

void record(std::size_t size) noexcept {
    if (counting) {
        ++allocations.count;
        allocations.bytes += size;
    }
}

void* allocate(std::size_t size, std::size_t alignment = 0) {
    if (failureAfter == 0) {
        throw std::bad_alloc{};
    }
    if (failureAfter > 0) {
        --failureAfter;
    }
    const auto actual = size ? size : 1;
    void* result = nullptr;
    if (!alignment) {
        result = std::malloc(actual);
    } else {
#ifdef _WIN32
        result = _aligned_malloc(actual, alignment);
#else
        if (actual <= std::numeric_limits<std::size_t>::max() - alignment + 1) {
            result = std::aligned_alloc(alignment, (actual + alignment - 1) / alignment * alignment);
        }
#endif
    }
    if (!result) {
        throw std::bad_alloc{};
    }
    record(size);
    return result;
}

void alignedFree(void* pointer) noexcept {
#ifdef _WIN32
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    alignedFree(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    alignedFree(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    alignedFree(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    alignedFree(pointer);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    alignedFree(pointer);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    alignedFree(pointer);
}

namespace grimlock::checks {
AllocationFailure::AllocationFailure(std::int64_t successfulCalls) noexcept : previous_(failureAfter) {
    failureAfter = successfulCalls;
}

AllocationFailure::~AllocationFailure() {
    failureAfter = previous_;
}

AllocationScope::AllocationScope() noexcept : before_(allocations), previous_(counting) {
    counting = true;
}

AllocationScope::~AllocationScope() {
    (void)stop();
}

Allocations AllocationScope::stop() noexcept {
    if (active_) {
        result_ = {allocations.count - before_.count, allocations.bytes - before_.bytes};
        counting = previous_;
        active_ = false;
    }
    return result_;
}
} // namespace grimlock::checks
