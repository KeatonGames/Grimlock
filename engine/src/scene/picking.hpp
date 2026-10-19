#pragma once
#include "grimlock/assets/animation.hpp"
#include "grimlock/camera.hpp"
#include <optional>

namespace grimlock::picking {
struct Ray {
    glm::dvec3 origin{}, direction{0,0,-1}; // Unit world direction: t is world distance.
    double minimum = 0, maximum = 0;
};
// Cursor/window use logical SDL coordinates; drawable size supplies render aspect.
[[nodiscard]] std::optional<Ray> mouseRay(const Camera& camera, glm::vec2 cursor,
    glm::ivec2 window, glm::ivec2 drawable);

// One static bounds cache per shared CPU resource, not per scene instance.
class Geometry final {
public:
    Geometry() = default;
    explicit Geometry(const assets::ModelData& model);
    [[nodiscard]] std::optional<double> intersect(const Ray& ray, const assets::ModelInstance& instance,
        const glm::mat4& world, std::optional<std::size_t> material, double closest, float opacity = 1);
private:
    struct Bounds { glm::dvec3 minimum{}, maximum{}; };
    std::vector<Bounds> bounds_;
    std::vector<glm::dvec3> positions_; // Reused only on clicks for skinned/morphed nodes.
};
}
