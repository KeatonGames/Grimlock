#include "debug/navigation_debug.hpp"
#include "editor.hpp"
#include "runtime/level_runtime.hpp"

#include <cstdlib>
#include <grimlock/editor.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        std::filesystem::path scene, level, environment;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--help") {
                std::cout << "Usage: GrimlockEditor [scene.json | --level level.json] [--environment folder]\nDefault: "
                             "assets/scenes/debug.json. Paths are executable-relative.\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--environment" || argument == "--level") {
                auto& destination = argument == "--level" ? level : environment;
                if (++i >= argc || !destination.empty()) {
                    throw std::invalid_argument("Option requires one path.");
                }
                const std::string_view value{argv[i]};
                destination = std::filesystem::path{std::u8string{value.begin(), value.end()}};
            } else {
                if (argument.starts_with("--") || !scene.empty()) {
                    throw std::invalid_argument(
                        "Usage: GrimlockEditor [scene.json | --level level.json] [--environment folder]");
                }
                scene = std::filesystem::path{std::u8string{argument.begin(), argument.end()}};
            }
        }
        if (!scene.empty() && !level.empty()) {
            throw std::invalid_argument("Choose a scene or a level, not both.");
        }
        grimlock::game::NavigationDebug debug;
        if (level.empty()) {
            grimlock::editor::run(scene.empty() ? std::filesystem::path{"assets/scenes/debug.json"} : scene,
                                  environment,
                                  &debug);
        } else {
            grimlock::Engine engine{grimlock::EngineConfig{.windowTitle = "Grimlock Editor",
                                                           .environmentFolder = environment,
                                                           .assetRoot = grimlock::game::assetRootForLevel(level)}};
            debug.requestLevelLoad(engine, level);
            const auto requested = *debug.takeSceneRequest();
            grimlock::editor::Editor editor{engine, requested, &debug};
            debug.sceneOpened(engine, requested);
            while (engine.update([&](double delta) {
                debug.update(engine, delta);
            })) {
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
