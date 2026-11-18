#include "systems/tower_system.hpp"
#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <glm/gtc/quaternion.hpp>
#include <grimlock/engine.hpp>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace grimlock;

namespace {
void save(Engine& engine, const std::filesystem::path& path, bool transparent = false) {
    for (int frame = 0; frame < 24; ++frame) {
        if (!engine.update()) {
            throw std::runtime_error("Capture window closed");
        }
        SDL_Delay(10);
    }
    auto image = engine.captureFrame();
    if (transparent && image.rgba.size() >= 4) {
        const std::array<unsigned char, 3> background{image.rgba[0], image.rgba[1], image.rgba[2]};
        for (std::size_t pixel = 0; pixel + 3 < image.rgba.size(); pixel += 4) {
            if (image.rgba[pixel] == background[0] && image.rgba[pixel + 1] == background[1] &&
                image.rgba[pixel + 2] == background[2]) {
                image.rgba[pixel + 3] = 0;
            }
        }
    }
    std::filesystem::create_directories(path.parent_path());
    if (!stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4)) {
        throw std::runtime_error("Could not save renderer capture: " + path.string());
    }
}

void composition(Engine& engine) {
    auto scene = std::make_unique<Scene>("Menu scenery - not a playable level");
    const auto add = [&](const char* model, glm::dvec3 position, glm::dvec3 scale = {2, 2, 2}, double yaw = 0) {
        const auto object = scene->createObject(model);
        scene->setModel(object, std::filesystem::path{"environment/platformer"} / model);
        Transform transform;
        transform.position = position;
        transform.scale = scale;
        transform.rotation = glm::angleAxis(yaw, glm::dvec3{0, 1, 0});
        scene->setTransform(object, transform);
        const std::string_view name{model};
        if (name.starts_with("tree") || name.starts_with("plant")) {
            scene->setTint(object, {0.78F, 1, 0.58F, 1});
        }
    };
    // Broad quiet foreground on the left; the winding path, grove and supplies frame the right.
    for (int z = -20; z <= 24; z += 2) {
        for (int x = -24; x <= 24; x += 2) {
            const bool hill = x >= 4 && z >= 4;
            const double y = hill ? 1 : 0;
            add("block-grass-low.glb", {x, y - 1, z}, {2.5, 2, 2.5});
            const bool path = (z <= -2 && x == 2) || (z == 0 && x >= 2 && x <= 6) || (x == 6 && z >= 2);
            if (path) {
                add("stones.glb", {x, y + 0.01, z}, {2, 2, 2}, z == 0 ? glm::radians(90.0) : 0);
            }
        }
    }
    add("block-grass-large-slope.glb", {6, -0.45, 2}, {1, 1.87, 1}, glm::radians(180.0));
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
        add("tree.glb", p, {2.6, 2.6, 2.6});
        add("plant.glb", p + glm::dvec3{1.2, 0, -0.6}, {1.6, 1.6, 1.6});
    }
    for (int z = -6; z <= -2; z += 2) {
        add("fence-low-straight.glb", {4, 0.1, z});
    }
    add("chest.glb", {8, 1.15, 7}, {2.4, 2.4, 2.4}, glm::radians(180.0));
    add("barrel.glb", {6, 1.1, 8}, {1.4, 1.4, 1.4});
    add("rocks.glb", {1, 0, 6}, {2.2, 2.2, 2.2});
    for (int z = 10; z <= 24; z += 4) {
        for (int x = -20; x <= 24; x += 4) {
            const double y = x >= 4 ? 1 : 0;
            const double jitter = std::sin(x * 13.1 + z * 7.3);
            const double scale = 2.2 + std::abs(jitter) * 1.1;
            add((x + z) % 8 == 0 ? "tree-pine.glb" : "tree.glb",
                {x + jitter, y, z + std::cos(x * 4.7 + z) * 0.8},
                {scale, scale, scale});
            add("plant.glb", {x + 1, y, z - 1});
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
        const bool portraits = argc == 3 && std::string_view{argv[2]} == "--hero-portraits";
        Engine engine{EngineConfig{.windowTitle = "Grimlock menu capture",
                                   .width = portraits ? 256 : 1600,
                                   .height = portraits ? 256 : 900,
                                   .assetRoot = argv[1]}};
        engine.setCameraNavigationEnabled(false);
        if (portraits) {
            for (const auto& hero : game::loadHeroDefinitions(engine.assetRoot())) {
                auto scene = std::make_unique<Scene>("Hero portrait");
                const auto object = scene->createObject(hero.name);
                scene->setModel(object, hero.model);
                engine.setScene(std::move(scene));
                const auto& model = engine.scene()->animation(object).model();
                const auto center = (model.minimum + model.maximum) * 0.5F;
                const float height = std::max(model.maximum.y - model.minimum.y, 0.1F);
                Camera view;
                view.target = center;
                view.position = center + glm::vec3{0.8F, 0.35F, 1.8F} * height;
                view.verticalFovDegrees = 35;
                view.farPlane = height * 20;
                engine.scene()->setResolvedCamera(view);
                const auto clips = engine.scene()->availableAnimations(object);
                for (std::size_t i = 0; i < clips.size(); ++i) {
                    if (clips[i].name.find("Idle") != std::string::npos ||
                        clips[i].name.find("idle") != std::string::npos) {
                        engine.scene()->playAnimation(object, i, true);
                        break;
                    }
                }
                auto& animation = engine.scene()->animation(object);
                animation.pause();
                const float duration =
                    animation.clip() < clips.size() ? clips[animation.clip()].end - clips[animation.clip()].start : 0;
                std::vector<unsigned char> atlas(1024 * 1024 * 4);
                for (int index = 0; index < 16; ++index) {
                    animation.seek(duration * index / 16);
                    const auto frame = engine.captureFrame();
                    if (frame.width != 256 || frame.height != 256) {
                        throw std::runtime_error("Portrait capture dimensions changed");
                    }
                    for (int y = 0; y < 256; ++y) {
                        for (int x = 0; x < 256; ++x) {
                            const auto source = (y * 256 + x) * 4;
                            const auto target = (((index / 4) * 256 + y) * 1024 + (index % 4) * 256 + x) * 4;
                            for (int channel = 0; channel < 4; ++channel) {
                                atlas[target + channel] = frame.rgba[source + channel];
                            }
                            if (frame.rgba[source] == frame.rgba[0] && frame.rgba[source + 1] == frame.rgba[1] &&
                                frame.rgba[source + 2] == frame.rgba[2]) {
                                atlas[target + 3] = 0;
                            }
                        }
                    }
                }
                const auto path = engine.assetRoot() / "ui/heroes" / (hero.id + ".png");
                std::filesystem::create_directories(path.parent_path());
                if (!stbi_write_png(path.string().c_str(), 1024, 1024, 4, atlas.data(), 1024 * 4)) {
                    throw std::runtime_error("Portrait atlas save failed");
                }
            }
        } else if (argc == 3 && std::string_view{argv[2]} == "--profile-map") {
            engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/world_map.json"));
            engine.setPerformanceEnabled(true);
            engine.setVsync(false);
            for (int i = 0; i < 12; ++i) {
                (void)engine.update();
            }
            double total = 0, gpu = 0;
            for (int i = 0; i < 90; ++i) {
                auto view = engine.scene()->resolvedCamera();
                view.position.x += 0.015F;
                view.target.x += 0.015F;
                engine.scene()->setResolvedCamera(view);
                (void)engine.update();
                total += engine.performance().frameMilliseconds;
                gpu += engine.performance().gpuMilliseconds.value_or(0);
            }
            std::cout << "PROFILE world_map frame=" << total / 90 << " gpu=" << gpu / 90 << std::endl;
        } else if (argc == 4 && std::string_view{argv[2]} == "--profile") {
            game::LevelRuntime runtime;
            runtime.load(engine, argv[3]);
            game::GameHud hud{engine, runtime};
            engine.setPerformanceEnabled(true);
            engine.setVsync(false);
            int windowCount = 0;
            auto windows = SDL_GetWindows(&windowCount);
            if (windowCount) {
                SDL_RaiseWindow(windows[0]);
                SDL_WarpMouseInWindow(windows[0], 800, 450);
            }
            SDL_free(windows);
            for (int frame = 0; frame < 12; ++frame) {
                (void)engine.update([&](double) {
                    runtime.update(engine, 1.0 / 60);
                });
            }
            double total = 0, game = 0, preparation = 0, render = 0, gpu = 0;
            for (int frame = 0; frame < 90; ++frame) {
                auto view = engine.scene()->resolvedCamera();
                const glm::vec3 offset{0.015F, 0, 0};
                view.position += offset;
                view.target += offset;
                engine.scene()->setResolvedCamera(view);
                (void)engine.update([&](double) {
                    runtime.update(engine, 1.0 / 60);
                });
                const auto& p = engine.performance();
                total += p.frameMilliseconds;
                game += p.gameMilliseconds;
                preparation += p.preparationMilliseconds;
                render += p.renderMilliseconds;
                gpu += p.gpuMilliseconds.value_or(0);
            }
            std::cout << "PROFILE camera frame=" << total / 90 << " game=" << game / 90
                      << " preparation=" << preparation / 90 << " render=" << render / 90 << " gpu=" << gpu / 90
                      << std::endl;
            const auto zone = runtime.navigation().placement().zones().front();
            const auto tower = zone.snap(runtime.navigation().towers().definition("basic").instance());
            std::cout << "PROFILE nav_triangles=" << runtime.navigation().navigation().statistics().triangles
                      << " nav_build=" << runtime.navigation().navigation().statistics().buildMilliseconds;
            if (runtime.heroes()->typeCount()) {
                std::cout << " hero_build=" << runtime.heroes()->navigation().statistics().buildMilliseconds;
            }
            std::cout << std::endl;
            const auto pickStart = std::chrono::steady_clock::now();
            const auto view = engine.scene()->resolvedCamera();
            const Ray query{view.position, glm::normalize(view.target - view.position), 0, 200};
            for (int i = 0; i < 100; ++i) {
                (void)engine.pickObject(query);
            }
            std::cout
                << "PROFILE pick="
                << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pickStart).count() / 100
                << std::endl;
            const auto start = std::chrono::steady_clock::now();
            const auto error = runtime.navigation().checkTower(engine, tower);
            const auto checked = std::chrono::steady_clock::now();
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            (void)runtime.level().buildTower(engine, runtime.navigation(), tower);
            runtime.update(engine, 0);
            const auto built = std::chrono::steady_clock::now();
            std::cout << "PROFILE placement_check="
                      << std::chrono::duration<double, std::milli>(checked - start).count()
                      << " commit=" << std::chrono::duration<double, std::milli>(built - checked).count() << std::endl;
        } else if (argc == 5 && std::string_view{argv[2]} == "--scene") {
            engine.setScene(engine.loadScene(argv[3]));
            game::TowerSystem towers;
            towers.synchronize(engine);
            save(engine, argv[4]);
        } else if ((argc == 5 || (argc == 6 && (std::string_view{argv[5]} == "--tower" ||
                                                std::string_view{argv[5]} == "--hero-control"))) &&
                   std::string_view{argv[2]} == "--level") {
            game::LevelRuntime runtime;
            runtime.load(engine, argv[3]);
            game::GameHud hud{engine, runtime};
            if (argc == 6 && std::string_view{argv[5]} == "--hero-control") {
                runtime.startWave();
                if (!runtime.controlHeroSlot(0)) {
                    throw std::runtime_error("Level has no available hero");
                }
                runtime.update(engine, 0.1);
            } else if (argc == 6) {
                const auto& zone = runtime.navigation().placement().zones().front();
                const auto tower = zone.snap(runtime.navigation().towers().definition("basic").instance());
                const auto built = runtime.level().buildTower(engine, runtime.navigation(), tower);
                runtime.selectObject(engine, Ray{tower.position + glm::dvec3{0, 20, 0}, {0, -1, 0}, 0, 40});
                if (!runtime.inspectedTower() || runtime.inspectedTower()->object != built) {
                    throw std::runtime_error("Capture could not select the constructed tower.");
                }
            }
            save(engine, argv[4]);
        } else if (argc == 3) {
            game::LevelRuntime runtime;
            game::GameHud hud{engine, runtime, true};
            save(engine, argv[2]);
        } else {
            composition(engine);
            save(engine, engine.assetRoot() / "ui/menu_background.png");
            engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/forest_defense.json"));
            save(engine, engine.assetRoot() / "previews/forest_defense.png");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
