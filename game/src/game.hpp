#pragma once
#include "grimlock/engine.hpp"

namespace grimlock::game {
class Game final {
public:
    explicit Game(const std::filesystem::path& modelPath = {},
                  const std::filesystem::path& environmentFolder = {});
    void run();

private:
    InstanceId loadExample(const std::filesystem::path& path);
    Engine engine_;
};
} // namespace grimlock::game
