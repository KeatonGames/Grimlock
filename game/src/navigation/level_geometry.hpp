#pragma once

#include "navigation/navigation_geometry.hpp"

#include <grimlock/engine.hpp>
#include <grimlock/ray.hpp>
#include <grimlock/terrain.hpp>
#include <map>

namespace grimlock::game {

struct Tower;

// Authored static geometry. The grid is used only when no terrain objects are present.
class LevelGeometry final {
public:
    void synchronize(Engine& engine, bool legacyProps = false);

    [[nodiscard]] const NavigationGeometry& navigation() const noexcept {
        return geometry_;
    }

    [[nodiscard]] std::span<const TerrainBox> boxes() const noexcept {
        return boxes_;
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

    [[nodiscard]] bool imported() const noexcept {
        return imported_;
    }

    [[nodiscard]] std::optional<glm::dvec3> intersect(const Ray& ray) const;
    [[nodiscard]] std::string supportError(const Tower& tower) const;

private:
    struct ObjectGeometry {
        ObjectId id;
        std::filesystem::path model;
        glm::mat4 world;
        std::string role;
        bool operator==(const ObjectGeometry&) const = default;
    };

    const Scene* scene_ = nullptr;
    const Grid* grid_ = nullptr;
    std::optional<GridConfiguration> configuration_;
    std::optional<std::uint64_t> gridRevision_;
    std::vector<ObjectGeometry> objects_, scratch_;
    std::map<std::filesystem::path, std::vector<std::array<glm::vec3, 3>>> models_;
    NavigationGeometry geometry_;
    std::vector<TerrainBox> boxes_;
    std::uint64_t revision_ = 0;
    bool imported_ = false;
};

} // namespace grimlock::game
