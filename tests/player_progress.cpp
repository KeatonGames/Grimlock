#include "runtime/player_progress.hpp"

#include "entities/level_definition.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, {}};
}

void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream{path, std::ios::binary} << data;
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected assets and fixture directory");
        const std::filesystem::path folder{argv[2]}, path = folder / "profile/progress.json";
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        const auto id = createLevelId(), other = createLevelId();
        require(validLevelId(id) && id != other, "stable ID generation failed");
        PlayerProgress progress{path};
        require(progress.error().empty() && !progress.find(id) && !std::filesystem::exists(path),
                "missing save failed");
        progress.startAttempt(id);
        require(progress.find(id)->attempts == 1 && progress.lastPlayed() == id, "attempt/last played not recorded");
        require(progress.finishAttempt(id, true, 10), "first victory was not a record");
        const auto firstSave = read(path);
        require(!progress.finishAttempt(id, true, 1) && firstSave == read(path), "duplicate victory/save occurred");
        PlayerProgress reopened{path};
        require(reopened.error().empty() && reopened.find(id)->completed && reopened.find(id)->victories == 1 &&
                    reopened.find(id)->bestSeconds == 10 && reopened.lastPlayed() == id,
                "progress did not persist across sessions");
        reopened.startAttempt(id);
        require(!reopened.finishAttempt(id, true, 15) && reopened.find(id)->bestSeconds == 10,
                "slower replay lost best time");
        reopened.startAttempt(id);
        const auto defeatSave = read(path);
        require(!reopened.finishAttempt(id, false, 1) && reopened.find(id)->attempts == 3 &&
                    reopened.find(id)->victories == 2 && defeatSave == read(path),
                "defeat recorded a victory or redundant save");
        reopened.startAttempt(id);
        require(reopened.finishAttempt(id, true, 5) && reopened.find(id)->bestSeconds == 5,
                "faster replay was not a record");
        reopened.startAttempt(other);
        reopened.abandonAttempt();
        require(reopened.find(id)->victories == 3 && reopened.find(other)->attempts == 1 &&
                    reopened.lastPlayed() == other,
                "new/abandoned level lost existing progress");
        PlayerProgress nextSession{path};
        require(nextSession.find(other)->attempts == 1 && !nextSession.find(other)->completed,
                "abandoned attempt was not durable");

        auto level = loadLevelDefinition(std::filesystem::path{argv[1]} / "levels/first_watch.json");
        level.id = id;
        const auto original = folder / "original.json", renamed = folder / "renamed.json";
        saveLevelDefinition(original, level);
        level.name = "A different display name";
        saveLevelDefinition(renamed, level);
        require(loadLevelDefinition(original).id == loadLevelDefinition(renamed).id &&
                    nextSession.find(loadLevelDefinition(renamed).id)->bestSeconds == 5,
                "renaming level changed identity/progress");
        level.id.clear();
        const auto legacy = folder / "legacy.json";
        saveLevelDefinition(legacy, level);
        const auto assigned = loadLevelDefinition(legacy).id;
        saveLevelDefinition(legacy, level);
        require(validLevelId(assigned) && assigned == loadLevelDefinition(legacy).id,
                "repeated legacy save changed identity");
        auto legacyDocument = nlohmann::json::parse(read(legacy));
        legacyDocument.erase("id");
        write(legacy, legacyDocument.dump());
        const auto legacyBefore = read(legacy), progressBefore = read(path);
        const auto untracked = loadLevelDefinition(legacy);
        nextSession.startAttempt(untracked.id);
        require(untracked.id.empty() && read(legacy) == legacyBefore && read(path) == progressBefore,
                "Game assigned filename-based progress or modified a legacy level");

        auto invalid = nlohmann::json::parse(firstSave);
        std::vector<std::string> corrupt{"{", R"({"version":0})", R"({"version":99})"};
        invalid["levels"][id]["attempts"] = -1;
        corrupt.push_back(invalid.dump());
        invalid = nlohmann::json::parse(firstSave);
        invalid["levels"][id]["best_seconds"] = -1;
        corrupt.push_back(invalid.dump());
        invalid = nlohmann::json::parse(firstSave);
        invalid["levels"][id]["victories"] = 10;
        corrupt.push_back(invalid.dump());
        invalid = nlohmann::json::parse(firstSave);
        invalid["last_played"] = "unknown";
        corrupt.push_back(invalid.dump());
        for (std::size_t index = 0; index < corrupt.size(); ++index) {
            const auto file = folder / ("invalid-" + std::to_string(index) + ".json");
            write(file, corrupt[index]);
            PlayerProgress protectedSave{file};
            require(!protectedSave.error().empty(), "invalid save silently accepted");
            protectedSave.startAttempt(other);
            (void)protectedSave.finishAttempt(other, true, 1);
            require(read(file) == corrupt[index], "invalid/unsupported save was overwritten");
        }
        PlayerProgress external{path};
        write(path, firstSave);
        external.startAttempt(id);
        require(!external.error().empty() && read(path) == firstSave, "external changes were overwritten");
        const auto blocked = folder / "not-a-directory";
        write(blocked, "keep");
        PlayerProgress unwritable{blocked / "progress.json"};
        unwritable.startAttempt(id);
        (void)unwritable.finishAttempt(id, true, 1);
        require(!unwritable.error().empty() && read(blocked) == "keep", "unwritable path was not handled safely");
        std::cout << "PASS durable attempts/victories, records, stable IDs, replay, abandoned attempts, corrupt/old "
                     "saves and write protection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL player progress: " << error.what() << '\n';
        return 1;
    }
}
