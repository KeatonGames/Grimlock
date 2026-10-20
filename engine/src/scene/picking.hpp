#pragma once
#include "grimlock/assets/animation.hpp"
#include "grimlock/ray.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace grimlock::picking {
using grimlock::mouseRay; // Existing picking users share the public unprojection utility.
using grimlock::Ray;

// One static bounds cache per shared CPU resource, not per scene instance.
class Geometry final {
public:
    Geometry() = default;
    explicit Geometry(const assets::ModelData& model);
    [[nodiscard]] std::optional<double>
    intersect(const Ray& ray, const assets::ModelInstance& instance, const glm::mat4& world,
              std::optional<std::size_t> material, double closest, float opacity = 1);

private:
    struct Bounds {
        glm::dvec3 minimum{}, maximum{};
    };

    std::vector<Bounds> bounds_;
    std::vector<glm::dvec3> positions_; // Reused only on clicks for skinned/morphed nodes.
};
} // namespace grimlock::picking
