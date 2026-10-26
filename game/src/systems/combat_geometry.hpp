#pragma once

#include "entities/enemy.hpp"
#include "entities/tower.hpp"
#include "navigation/level_geometry.hpp"

namespace grimlock::game {

struct CombatHit {
    glm::dvec3 position{0};
    double fraction = 0;
    ObjectId object = 0;
};

// One static triangle BVH and inexpensive dynamic footprint queries, shared by combat systems.
class CombatGeometry final {
public:
    void synchronize(Engine& engine, std::span<const Tower> towers = {});
    void build(const NavigationGeometry& geometry);
    [[nodiscard]] std::optional<CombatHit> trace(glm::dvec3 start,
                                                 glm::dvec3 end,
                                                 double radius = 0,
                                                 ObjectId ignore = 0,
                                                 ObjectId target = 0) const;

    [[nodiscard]] bool visible(glm::dvec3 start, glm::dvec3 end, ObjectId ignore = 0, ObjectId target = 0) const {
        return !trace(start, end, 0, ignore, target);
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

private:
    struct Triangle {
        std::array<glm::dvec3, 3> vertices;
        glm::dvec3 minimum, maximum;
    };

    struct Node {
        glm::dvec3 minimum, maximum;
        std::size_t first = 0, count = 0, left = 0, right = 0;
    };

    std::size_t partition(std::size_t first, std::size_t count);
    void visit(std::size_t node, glm::dvec3 start, glm::dvec3 displacement, double radius, double& closest) const;
    LevelGeometry source_;
    std::uint64_t sourceRevision_ = 0, revision_ = 0;
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    std::vector<Tower> towers_;
};

[[nodiscard]] inline glm::dvec3 combatAim(const Enemy& enemy) {
    return enemy.position + glm::dvec3{0, enemy.height * 0.5, 0};
}

[[nodiscard]] inline glm::dvec3 firingOrigin(const Tower& tower) {
    return tower.position + glm::dvec3{0, tower.halfExtent.y + 0.1, 0};
}

} // namespace grimlock::game
