#include "game.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        std::filesystem::path level, environment;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--help") {
                std::cout
                    << "Usage: Grimlock [level.json] [--environment folder]\nDefault: assets/levels/first_watch.json. "
                       "Paths are executable-relative.\nFor authoring, run GrimlockEditor.\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--environment") {
                if (++i >= argc || !environment.empty()) {
                    throw std::invalid_argument("--environment requires one folder.");
                }
                const std::string_view value{argv[i]};
                environment = std::filesystem::path{std::u8string{value.begin(), value.end()}};
            } else {
                if (argument.starts_with("--") || !level.empty()) {
                    throw std::invalid_argument("Usage: Grimlock [level.json] [--environment folder]");
                }
                level = std::filesystem::path{std::u8string{argument.begin(), argument.end()}};
            }
        }
        grimlock::game::Game game{level, environment};
        game.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
