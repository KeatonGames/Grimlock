#include "game.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(const int argc, char** argv) {
    try {
        std::filesystem::path path, environment;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--help") {
                std::cout << "Usage: Grimlock [scene.json|model.obj|.glb|.gltf] [--environment folder]\nNo argument: assets/scenes/demo.json. A supplied model starts its first clip, if any.\n"
                             "Environment faces: px/nx/py/ny/pz/nz (.hdr, .png, .jpg). Paths are executable-relative.\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--environment") {
                if (++i >= argc || !environment.empty()) { throw std::invalid_argument("--environment requires one cubemap folder."); }
                const std::string_view folder{argv[i]};
                environment = std::filesystem::path{std::u8string{folder.begin(),folder.end()}};
            } else {
                if (!path.empty() || argument.starts_with("--")) { throw std::invalid_argument("Usage: Grimlock [scene.json|model] [--environment folder]"); }
                path = std::filesystem::path{std::u8string{argument.begin(),argument.end()}};
            }
        }
        grimlock::game::Game game{path, environment};
        game.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
