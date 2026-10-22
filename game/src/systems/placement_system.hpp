#pragma once

#include "entities/build_zone.hpp"

namespace grimlock::game {

enum class PlacementMode {
    gameplay,
    authoring
};

// World-space construction policy; no grid coordinates, rendering or navigation bakes.
class PlacementSystem final {
public:
    static constexpr const char* zoneRole = "grimlock.build_zone";
    void synchronize(const Scene* scene);
    [[nodiscard]] std::string check(const Tower& tower) const;

    [[nodiscard]] std::span<const BuildZone> zones() const noexcept {
        return zones_;
    }

    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

    [[nodiscard]] static ObjectId createZone(Scene& scene, const BuildZone& zone);
    static void editZone(Scene& scene, const BuildZone& zone);

private:
    std::vector<BuildZone> zones_, scratch_;
    std::string error_;
};

} // namespace grimlock::game
