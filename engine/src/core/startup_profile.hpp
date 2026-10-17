#pragma once
#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace grimlock::diagnostics {
class StartupSpan;

// Engine-owned, single-thread startup capture. The thread-local pointer only routes
// nested diagnostic spans; it owns no engine state and is detached after first present.
class StartupProfile final {
public:
    StartupProfile();
    ~StartupProfile();
    StartupProfile(const StartupProfile&) = delete;
    StartupProfile& operator=(const StartupProfile&) = delete;
    void firstPresent();
private:
    friend class StartupSpan;
    using Clock = std::chrono::steady_clock;
    struct Measurement { std::size_t calls = 0; double inclusive = 0, self = 0; };
    void report(std::string_view result) noexcept;
    static thread_local StartupProfile* active_;
    StartupProfile* previous_ = nullptr;
    StartupSpan* top_ = nullptr;
    Clock::time_point start_;
    std::map<std::string, Measurement, std::less<>> measurements_;
    bool finished_ = false;
};

class StartupSpan final {
public:
    explicit StartupSpan(std::string_view phase);
    ~StartupSpan();
    StartupSpan(const StartupSpan&) = delete;
    StartupSpan& operator=(const StartupSpan&) = delete;
private:
    friend class StartupProfile;
    StartupProfile* profile_ = nullptr;
    StartupSpan* parent_ = nullptr;
    std::string_view phase_;
    std::chrono::steady_clock::time_point start_;
    double children_ = 0;
};
}
