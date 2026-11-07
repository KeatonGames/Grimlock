#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/quaternion.hpp>
#include <grimlock/engine.hpp>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace grimlock;

namespace {
void save(Engine& engine, const std::filesystem::path& path) {
    for (int frame = 0; frame < 24; ++frame) {
        if (!engine.update()) {
            throw std::runtime_error("Capture window closed");
        }
        SDL_Delay(10);
    }
    const auto image = engine.captureFrame();
    std::filesystem::create_directories(path.parent_path());
    if (!stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4)) {
        throw std::runtime_error("Could not save renderer capture: " + path.string());
    }
}

void composition(Engine& engine) {
    auto scene = std::make_unique<Scene>("Menu scenery - not a playable level");
    const auto add = [&](const char* model, glm::dvec3 position, glm::dvec3 scale = {2, 2, 2}, double yaw = 0) {
        const auto object = scene->createObject(model);
        scene->setModel(object, std::filesystem::path{"environment/nature/kenney"} / model);
        Transform transform;
        transform.position = position;
        transform.scale = scale;
        transform.rotation = glm::angleAxis(yaw, glm::dvec3{0, 1, 0});
        scene->setTransform(object, transform);
        const std::string_view name{model};
        if (name.starts_with("ground_grass") || name.starts_with("tree_") || name.starts_with("plant_")) {
            scene->setTint(object, {0.78F, 1, 0.58F, 1});
        }
    };
    // Broad quiet foreground on the left; the winding path, grove and camp frame the right.
    for (int z = -20; z <= 24; z += 2) {
        for (int x = -24; x <= 24; x += 2) {
            const bool hill = x >= 4 && z >= 4;
            const double y = hill ? 1.1 : 0.1;
            add("cliff_blockHalf_rock.glb", {x, y - 1.12, z});
            const bool path = (z <= -2 && x == 2) || (z == 0 && x >= 2 && x <= 6) || (x == 6 && z >= 2);
            add(path ? "ground_pathStraight.glb" : "ground_grass.glb",
                {x, y, z},
                {2, 2, 2},
                z == 0 ? glm::radians(90.0) : 0);
        }
    }
    add("cliff_blockSlope_rock.glb", {6, 0.1, 2});
    for (const auto p : {glm::dvec3{0, 0, -4},
                         {-2, 0, 1},
                         {0, 0, 5},
                         {2, 0, 8},
                         {8, 1, 6},
                         {10, 1, 9},
                         {10, 0, -3},
                         {8, 0, -7},
                         {-6, 0, 3},
                         {-8, 0, 0},
                         {-10, 0, 6},
                         {-12, 0, 4}}) {
        add("tree_default.glb", p, {2.6, 2.6, 2.6});
        add("plant_bushLarge.glb", p + glm::dvec3{1.2, 0, -0.6}, {1.6, 1.6, 1.6});
    }
    for (int z = -6; z <= -2; z += 2) {
        add("fence_simple.glb", {4, 0.1, z});
    }
    add("tent_smallOpen.glb", {8, 1.15, 7}, {2.4, 2.4, 2.4}, glm::radians(180.0));
    add("campfire_stones.glb", {6, 1.1, 8}, {1.4, 1.4, 1.4});
    add("rock_largeA.glb", {1, 0, 6}, {2.2, 2.2, 2.2});
    for (int z = 10; z <= 24; z += 4) {
        for (int x = -20; x <= 24; x += 4) {
            const double y = x >= 4 ? 1 : 0;
            const double jitter = std::sin(x * 13.1 + z * 7.3);
            const double scale = 2.2 + std::abs(jitter) * 1.1;
            add((x + z) % 8 == 0 ? "tree_pineRoundF.glb" : "tree_default_dark.glb",
                {x + jitter, y, z + std::cos(x * 4.7 + z) * 0.8},
                {scale, scale, scale});
            add("plant_bushLarge.glb", {x + 1, y, z - 1});
        }
    }
    Camera camera;
    camera.position = {-21, 17, -23};
    camera.target = {5, 0.5F, -3};
    camera.verticalFovDegrees = 42;
    camera.farPlane = 140;
    scene->setResolvedCamera(camera);
    engine.setScene(std::move(scene));
    engine.lighting().directional = {{-0.6F, -0.8F, 0.3F}, 3.2F, {1, 0.88F, 0.65F}};
    engine.lighting().environmentIntensity = 0.75F;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument("Expected absolute asset root");
        }
        Engine engine{
            EngineConfig{.windowTitle = "Grimlock menu capture", .width = 1600, .height = 900, .assetRoot = argv[1]}};
        engine.setCameraNavigationEnabled(false);
        if (argc == 3) {
            game::LevelRuntime runtime;
            game::GameHud hud{engine, runtime, true};
            save(engine, argv[2]);
        } else {
            composition(engine);
            save(engine, engine.assetRoot() / "ui/menu_background.png");
            engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/forest_defense.json"));
            auto camera = engine.scene()->resolvedCamera();
            camera.position = {14, 18, -15};
            camera.target = {-1, 0.5F, 4};
            camera.verticalFovDegrees = 48;
            engine.scene()->setResolvedCamera(camera);
            save(engine, engine.assetRoot() / "previews/forest_defense.png");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
