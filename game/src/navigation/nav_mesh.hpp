#pragma once

#include "navigation/navigation_geometry.hpp"

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>
#include <optional>
#include <string>

namespace grimlock::game {

struct NavMeshConfiguration {
    float cellSize = 0.1F;
    float cellHeight = 0.05F;
    float agentRadius = 0.22F;
    float agentHeight = 0.5F;
    float maximumSlope = 45;
    float stepHeight = 0.15F;
};

struct NavigationPoint {
    glm::dvec3 position{0};
    std::uint32_t polygon = 0;
};

struct NavigationPath {
    std::vector<std::uint32_t> polygons;
    std::uint64_t revision = 0;
    glm::dvec3 destination{0};
    bool valid = false;
};

struct NavMeshStatistics {
    std::size_t triangles = 0, polygons = 0;
    double buildMilliseconds = 0;
    std::uint64_t pathQueries = 0;
};

// Owns a shared mesh and reusable single-threaded Detour query workspace.
// Rebuild/reset always changes revision, including failed builds.
class NavMesh final {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    bool build(const NavigationGeometry& geometry, NavMeshConfiguration configuration = {});
    void reset();
    [[nodiscard]] bool ready() const noexcept;

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

    [[nodiscard]] const std::string& status() const noexcept {
        return status_;
    }

    [[nodiscard]] const NavMeshConfiguration& configuration() const noexcept {
        return configuration_;
    }

    [[nodiscard]] const NavMeshStatistics& statistics() const noexcept {
        return statistics_;
    }

    [[nodiscard]] std::span<const std::array<glm::vec3, 3>> debugTriangles() const noexcept {
        return triangles_;
    }

    [[nodiscard]] std::optional<NavigationPoint> project(glm::dvec3 position, double horizontalExtent = 1) const;
    [[nodiscard]] bool contains(glm::dvec3 position, double tolerance = 0.02) const;
    [[nodiscard]] bool findPath(glm::dvec3 start, glm::dvec3 destination, NavigationPath& path) const;
    [[nodiscard]] std::optional<glm::dvec3> steering(glm::dvec3 position,
                                                     NavigationPath& path,
                                                     double* remaining = nullptr) const;
    [[nodiscard]] glm::dvec3 move(glm::dvec3 position, glm::dvec2 displacement) const;
    [[nodiscard]] bool visible(glm::dvec3 start, glm::dvec3 destination) const;
    [[nodiscard]] std::vector<glm::vec3> corners(glm::dvec3 position, const NavigationPath& path) const;

private:
    struct Data;
    std::unique_ptr<Data> data_;
    NavMeshConfiguration configuration_;
    mutable NavMeshStatistics statistics_;
    std::uint64_t revision_ = 0;
    std::string status_ = "No navigation geometry";
    std::vector<std::array<glm::vec3, 3>> triangles_;
};

} // namespace grimlock::game
