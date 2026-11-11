#include "runtime/level_runtime.hpp"
#include "scene_document.hpp"

#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct SavedFixture {
    std::filesystem::path folder;

    ~SavedFixture() {
        std::error_code error;
        std::filesystem::remove(folder / "scene.json", error);
        std::filesystem::remove(folder / "level.json", error);
        std::filesystem::remove(folder, error);
    }
};
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Editor to Game save checks", .assetRoot = assetRootForLevel({})}};
        const auto root = engine.assetRoot();
        auto definition = loadLevelDefinition(root / "levels/forest_defense.json");
        definition.id = createLevelId();
        SavedFixture files{root / "test_fixtures" / ("editor-save-" + definition.id)};
        std::filesystem::create_directories(files.folder);
        editor::SceneDocument document{engine};
        document.open(root / definition.scene);
        const auto rock = document.addModel("environment/platformer/rocks.glb");
        engine.scene()->setRole(rock, "grimlock.decoration");
        auto edited = engine.scene()->object(rock).local;
        edited.position = {-4, 0, -5};
        edited.position.x += 0.2F;
        document.setTransform(rock, edited);
        document.save(files.folder / "scene.json");
        definition.scene = std::filesystem::relative(files.folder / "scene.json", root);
        saveLevelDefinition(files.folder / "level.json", definition);

        LevelRuntime game;
        game.load(engine, files.folder / "level.json");
        require(glm::length(engine.scene()->object(rock).local.position - edited.position) < 1e-4F,
                "Game did not load the Editor's saved scene transform");

        // Simulate a second save in another Editor while the Game retains its current scene.
        auto saved = Scene::load(files.folder / "scene.json");
        edited.position.z += 0.35F;
        saved->setTransform(rock, edited);
        saved->save(files.folder / "scene.json");
        ++definition.startingGold;
        saveLevelDefinition(files.folder / "level.json", definition);
        game.restart(engine);
        require(glm::length(engine.scene()->object(rock).local.position - edited.position) < 1e-4F &&
                    game.level().economy().gold() == definition.startingGold,
                "Game restart retained stale Editor scene or level data");
        require(assetRootForLevel(root / "levels/forest_defense.json") == root,
                "Explicit source-level paths selected a different asset tree");
        game.unload(engine);
        std::cout << "PASS Editor scene save -> Game load/restart without rebuilding; shared assets: " << root << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL Editor level save: " << error.what() << '\n';
        return 1;
    }
}
