#include "game.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        std::filesystem::path level, environment, assets;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--help") {
                std::cout
                    << "Usage: Grimlock [level.json] [--assets folder] [--environment folder]\nDefault: main menu. "
                       "Relative level paths use the active asset folder's parent.\nFor authoring, run "
                       "GrimlockEditor.\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--environment" || argument == "--assets") {
                auto& destination = argument == "--assets" ? assets : environment;
                if (++i >= argc || !destination.empty()) {
                    throw std::invalid_argument("Option requires one folder.");
                }
                const std::string_view value{argv[i]};
                destination = std::filesystem::path{std::u8string{value.begin(), value.end()}};
            } else {
                if (argument.starts_with("--") || !level.empty()) {
                    throw std::invalid_argument(
                        "Usage: Grimlock [level.json] [--assets folder] [--environment folder]");
                }
                level = std::filesystem::path{std::u8string{argument.begin(), argument.end()}};
            }
        }
        grimlock::game::Game game{level, environment, assets};
        game.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
