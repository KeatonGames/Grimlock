#pragma once

#include "navigation/nav_mesh.hpp"

namespace grimlock::game {

struct RouteDash {
    glm::dvec3 from, to;
};

// Keeps the untravelled route stable; only its leading edge follows the hero.
class HeroRouteVisual final {
public:
    void update(const NavMesh& mesh,
                glm::dvec3 position,
                const NavigationPath& path,
                std::optional<glm::dvec3> waypoint = {});
    void clear();

    [[nodiscard]] std::span<const glm::dvec3> points() const {
        return remaining_;
    }

    [[nodiscard]] std::vector<RouteDash> dashes() const;

private:
    std::vector<glm::dvec3> route_, remaining_;
    glm::dvec3 destination_{0};
    std::optional<glm::dvec3> waypoint_;
    std::uint64_t revision_ = 0;
    std::size_t segment_ = 0;
};

} // namespace grimlock::game
