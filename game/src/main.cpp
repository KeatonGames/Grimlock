#include "game.hpp"
#ifdef GRIMLOCK_WITH_EDITOR
#include "debug/navigation_debug.hpp"
#include "grimlock/editor.hpp"
#endif
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(const int argc, char** argv) {
    try {
        std::filesystem::path path, environment;
        bool editor = false;

        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};

            if (argument == "--help") {
                std::cout << "Usage: Grimlock [--editor] [scene.json|model.obj|.glb|.gltf] "
                             "[--environment folder]\nNo argument: assets/scenes/demo.json. A "
                             "supplied model starts its first clip, if any. --editor without a scene opens "
                             "assets/scenes/debug.json.\n"
                             "Environment faces: px/nx/py/ny/pz/nz (.hdr, .png, .jpg). Paths are "
                             "executable-relative.\n";
                return EXIT_SUCCESS;
            }

            if (argument == "--editor") {
                editor = true;
            } else if (argument == "--environment") {
                if (++i >= argc || !environment.empty()) {
                    throw std::invalid_argument("--environment requires one cubemap folder.");
                }

                const std::string_view folder{argv[i]};
                environment = std::filesystem::path{std::u8string{folder.begin(), folder.end()}};
            } else {
                if (!path.empty() || argument.starts_with("--")) {
                    throw std::invalid_argument("Usage: Grimlock [--editor] [scene.json|model] [--environment folder]");
                }

                path = std::filesystem::path{std::u8string{argument.begin(), argument.end()}};
            }
        }

        if (editor) {
#ifdef GRIMLOCK_WITH_EDITOR
            if (!path.empty() && path.extension() != ".json") {
                throw std::invalid_argument("Editor mode opens JSON scenes; add models from the Hierarchy panel.");
            }

            grimlock::game::NavigationDebug navigation;
            grimlock::editor::run(path.empty() ? std::filesystem::path{"assets/scenes/debug.json"} : path,
                                  environment,
                                  &navigation);

            return EXIT_SUCCESS;
#else
            throw std::runtime_error("Editor support is disabled; configure with -DGRIMLOCK_BUILD_EDITOR=ON.");
#endif
        }

        grimlock::game::Game game{path, environment};
        game.run();

        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }
}
