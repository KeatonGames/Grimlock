#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace grimlock::game {

struct LevelProgress {
    bool completed = false;
    std::uint64_t attempts = 0, victories = 0;
    std::optional<double> bestSeconds;
};

class PlayerProgress final {
public:
    explicit PlayerProgress(std::filesystem::path path = {});
    void startAttempt(const std::string& id);
    [[nodiscard]] bool finishAttempt(const std::string& id, bool victory, double seconds);

    void abandonAttempt() noexcept {
        active_.clear();
    }

    [[nodiscard]] const LevelProgress* find(const std::string& id) const noexcept;

    [[nodiscard]] const std::string& lastPlayed() const noexcept {
        return lastPlayed_;
    }

    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    void load();
    void save();
    std::filesystem::path path_;
    std::map<std::string, LevelProgress> levels_;
    std::string lastPlayed_, active_, error_;
    std::optional<std::string> snapshot_;
    bool writable_ = true;
};

} // namespace grimlock::game
