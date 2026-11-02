#include "runtime/player_progress.hpp"

#include "entities/level_definition.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace grimlock::game {
namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("Cannot read progression file.");
    }
    std::string data{std::istreambuf_iterator<char>{stream}, {}};
    if (stream.bad()) {
        throw std::runtime_error("Cannot read complete progression file.");
    }
    return data;
}

std::uint64_t count(const nlohmann::json& value) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        throw std::invalid_argument("Invalid progression counter.");
    }
    return value.get<std::uint64_t>();
}

void increment(std::uint64_t& value) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Progression counter exhausted.");
    }
    ++value;
}
} // namespace

PlayerProgress::PlayerProgress(std::filesystem::path path) : path_(std::move(path)) {
    load();
}

const LevelProgress* PlayerProgress::find(const std::string& id) const noexcept {
    const auto found = levels_.find(id);
    return found == levels_.end() ? nullptr : &found->second;
}

void PlayerProgress::load() {
    try {
        if (path_.empty()) {
            throw std::runtime_error("User-data directory is unavailable.");
        }
        if (!std::filesystem::exists(path_)) {
            return;
        }
        const auto data = read(path_);
        const auto source = nlohmann::json::parse(data);
        if (!source.at("version").is_number_integer() || source.at("version") != 1) {
            throw std::runtime_error("Unsupported progression save version.");
        }
        std::map<std::string, LevelProgress> loaded;
        if (!source.at("levels").is_object()) {
            throw std::invalid_argument("Invalid progression level records.");
        }
        for (const auto& [id, entry] : source.at("levels").items()) {
            if (!validLevelId(id)) {
                throw std::invalid_argument("Invalid progression level ID.");
            }
            LevelProgress progress;
            progress.completed = entry.at("completed").get<bool>();
            progress.attempts = count(entry.at("attempts"));
            progress.victories = count(entry.at("victories"));
            if (!entry.at("best_seconds").is_null()) {
                progress.bestSeconds = entry.at("best_seconds").get<double>();
                if (!std::isfinite(*progress.bestSeconds) || *progress.bestSeconds < 0) {
                    throw std::invalid_argument("Invalid best completion time.");
                }
            }
            if (progress.victories > progress.attempts || progress.completed != (progress.victories > 0) ||
                progress.bestSeconds.has_value() != progress.completed) {
                throw std::invalid_argument("Inconsistent progression record.");
            }
            loaded.emplace(id, progress);
        }
        const auto last = source.at("last_played").get<std::string>();
        if (!last.empty() && !loaded.contains(last)) {
            throw std::invalid_argument("Invalid last played level.");
        }
        levels_ = std::move(loaded);
        lastPlayed_ = last;
        snapshot_ = data;
    } catch (const std::exception& failure) {
        writable_ = false;
        error_ = std::string{"Progress cannot be saved: "} + failure.what() + " Existing file was preserved.";
    }
}

void PlayerProgress::save() {
    if (!writable_) {
        return;
    }
    try {
        const bool exists = std::filesystem::exists(path_);
        if (exists != snapshot_.has_value() || (exists && read(path_) != *snapshot_)) {
            writable_ = false;
            throw std::runtime_error("Progress file changed externally; reopen Game before saving again.");
        }
        nlohmann::json document{{"version", 1}, {"last_played", lastPlayed_}, {"levels", nlohmann::json::object()}};
        for (const auto& [id, level] : levels_) {
            document["levels"][id] = {
                {"completed", level.completed},
                {"attempts", level.attempts},
                {"victories", level.victories},
                {"best_seconds", level.bestSeconds ? nlohmann::json(*level.bestSeconds) : nlohmann::json(nullptr)}};
        }
        const auto data = document.dump(2) + '\n';
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        auto temporary = path_;
        temporary += ".tmp";
        {
            std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
            file << data;
            file.close();
            if (!file) {
                throw std::runtime_error("Cannot write progression temporary file.");
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace progression save (Windows error " +
                                     std::to_string(GetLastError()) + ").");
        }
#else
        std::filesystem::rename(temporary, path_);
#endif
        snapshot_ = data;
        error_.clear();
    } catch (const std::exception& failure) {
        error_ = std::string{"Progress cannot be saved: "} + failure.what();
    }
}

void PlayerProgress::startAttempt(const std::string& id) {
    active_.clear();
    if (!validLevelId(id)) {
        return;
    }
    auto& level = levels_[id];
    if (level.attempts == std::numeric_limits<std::uint64_t>::max()) {
        error_ = "Progression attempt counter exhausted; level remains playable without recording another attempt.";
        return;
    }
    increment(level.attempts);
    active_ = lastPlayed_ = id;
    save();
}

bool PlayerProgress::finishAttempt(const std::string& id, bool victory, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid completion duration.");
    }
    if (active_ != id || active_.empty()) {
        return false;
    }
    active_.clear();
    auto& level = levels_.at(id);
    if (!victory) {
        return false;
    }
    increment(level.victories);
    const bool record = !level.bestSeconds || seconds < *level.bestSeconds;
    level.completed = true;
    if (record) {
        level.bestSeconds = seconds;
    }
    save();
    return record;
}

} // namespace grimlock::game
