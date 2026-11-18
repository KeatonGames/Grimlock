#pragma once

#include "grimlock/assets/model_data.hpp"
#include "grimlock/ray.hpp"

#include <memory>
#include <optional>

namespace grimlock {

struct TerrainFeature {
    glm::vec2 center{0};
    float radius = 4, height = 1;
};

struct HeightfieldConfiguration {
    glm::vec2 size{160, 120};
    glm::uvec2 resolution{161, 121};
    glm::vec2 playableSize{38, 32};
    float baseHeight = 0, noiseAmplitude = 0.15F, noiseFrequency = 0.2F;
    std::uint32_t seed = 1;
    bool minimalistic = false;
    float boundaryHeight = 8, roadWidth = 2.8F, roadEndWidth = 4.6F;
    glm::vec3 grassColor{0.17F, 0.48F, 0.10F}, roadColor{0.43F, 0.28F, 0.12F}, rockColor{0.29F, 0.32F, 0.27F};
    std::vector<TerrainFeature> hills, pads;
    std::vector<glm::vec3> road;
};

struct TerrainSample {
    float height;
    glm::vec3 normal;
};

// An immutable generated surface. Queries interpolate the rendered triangles, not the generating function.
class Heightfield final {
public:
    explicit Heightfield(HeightfieldConfiguration configuration);

    [[nodiscard]] const HeightfieldConfiguration& configuration() const noexcept {
        return configuration_;
    }

    [[nodiscard]] std::shared_ptr<const assets::ModelData> model() const noexcept {
        return model_;
    }

    [[nodiscard]] std::optional<TerrainSample> sample(glm::vec2 position) const;
    [[nodiscard]] std::optional<glm::dvec3> intersect(const Ray& ray) const;
    [[nodiscard]] bool playable(glm::vec2 position) const noexcept;

private:
    HeightfieldConfiguration configuration_;
    std::shared_ptr<const assets::ModelData> model_;
};

} // namespace grimlock
