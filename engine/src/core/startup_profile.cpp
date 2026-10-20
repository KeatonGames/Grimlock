#include "core/startup_profile.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace grimlock::diagnostics {
thread_local StartupProfile* StartupProfile::active_ = nullptr;

StartupProfile::StartupProfile() : start_(Clock::now()) {
#ifndef NDEBUG
    previous_ = active_;
    active_ = this;
#endif
}

StartupProfile::~StartupProfile() {
#ifndef NDEBUG
    if (!finished_) {
        report("aborted_before_first_present");
    }
    if (active_ == this) {
        active_ = previous_;
    }
#endif
}

void StartupProfile::firstPresent() {
#ifndef NDEBUG
    if (!finished_) {
        report("first_present_returned");
        finished_ = true;
        active_ = previous_;
    }
#endif
}

void StartupProfile::report(std::string_view result) noexcept {
    try {
        const double elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
        std::vector<std::pair<std::string, Measurement>> rows(measurements_.begin(),
                                                              measurements_.end());
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second.self > b.second.self; });
        std::ostringstream out;
        out << std::fixed << std::setprecision(3) << "[startup] result=" << result
            << " total_ms=" << elapsed << '\n'
            << "[startup] steady-clock wall times; inclusive rows overlap; self excludes nested "
               "spans. Wait rows measure host blocking, not GPU execution.\n";
        for (const auto& [phase, m] : rows) {
            out << "[startup] phase=" << std::quoted(phase) << " calls=" << m.calls
                << " inclusive_ms=" << m.inclusive << " self_ms=" << m.self << '\n';
        }
        std::clog << out.str() << std::flush;
    } catch (...) {
    } // Diagnostics must not mask initialization/cleanup errors.
}

StartupSpan::StartupSpan(std::string_view phase) {
#ifndef NDEBUG
    profile_ = StartupProfile::active_;
    if (profile_) {
        phase_ = phase;
        parent_ = profile_->top_;
        profile_->top_ = this;
        start_ = std::chrono::steady_clock::now();
    }
#else
    (void)phase;
#endif
}

StartupSpan::~StartupSpan() {
#ifndef NDEBUG
    if (!profile_) {
        return;
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    profile_->top_ = parent_;
    if (parent_) {
        parent_->children_ += elapsed;
    }
    try {
        auto& m = profile_->measurements_[std::string{phase_}];
        ++m.calls;
        m.inclusive += elapsed;
        m.self += std::max(0.0, elapsed - children_);
    } catch (...) {
    }
#endif
}
} // namespace grimlock::diagnostics
