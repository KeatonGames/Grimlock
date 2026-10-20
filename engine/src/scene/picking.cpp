#include "scene/picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace grimlock::picking {

namespace {

bool box(glm::dvec3 origin, glm::dvec3 direction, glm::dvec3 low, glm::dvec3 high, double start, double end) {
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return false;
            }

            continue;
        }

        double a = (low[axis] - origin[axis]) / direction[axis], b = (high[axis] - origin[axis]) / direction[axis];

        if (a > b) {
            std::swap(a, b);
        }

        start = std::max(start, a);
        end = std::min(end, b);

        if (start > end) {
            return false;
        }
    }

    return true;
}

struct TriangleHit {
    double distance, u, v;
};

std::optional<TriangleHit> triangle(glm::dvec3 origin,
                                    glm::dvec3 direction,
                                    glm::dvec3 a,
                                    glm::dvec3 b,
                                    glm::dvec3 c,
                                    bool doubleSided,
                                    double minimum,
                                    double maximum) {
    const auto ab = b - a, ac = c - a, p = glm::cross(direction, ac);
    const double determinant = glm::dot(ab, p);
    const double epsilon = 1e-12 * glm::length(ab) * glm::length(ac) * glm::length(direction);

    if (doubleSided ? std::abs(determinant) <= epsilon : determinant <= epsilon) {
        return {};
    }

    const auto delta = origin - a;
    const double u = glm::dot(delta, p) / determinant;

    if (u < 0 || u > 1) {
        return {};
    }

    const auto q = glm::cross(delta, ab);
    const double v = glm::dot(direction, q) / determinant;

    if (v < 0 || u + v > 1) {
        return {};
    }

    const double distance = glm::dot(ac, q) / determinant;

    if (!std::isfinite(distance) || distance < minimum || distance >= maximum) {
        return {};
    }

    return TriangleHit{distance, u, v};
}

double textureAlpha(const assets::ModelData& model, const assets::TextureReference& reference, glm::dvec2 uv) {
    const auto& texture = model.textures.at(reference.texture);
    const auto& image = model.images.at(texture.image);

    if (!image || !image->width || !image->height) {
        return 1;
    }

    uv *= glm::dvec2{reference.scale};
    const double sine = std::sin(reference.rotation), cosine = std::cos(reference.rotation);
    uv = glm::dvec2{cosine * uv.x - sine * uv.y, sine * uv.x + cosine * uv.y} + glm::dvec2{reference.offset};
    const auto coordinate = [](double value, assets::TextureWrap wrap) {
        if (wrap == assets::TextureWrap::clampToEdge) {
            return std::clamp(value, 0.0, 1.0);
        }

        const double period = wrap == assets::TextureWrap::repeat ? 1.0 : 2.0;
        value = std::fmod(value, period);

        if (value < 0) {
            value += period;
        }

        return value > 1 ? 2 - value : value;
    };

    uv = {coordinate(uv.x, texture.sampler.wrapU), coordinate(uv.y, texture.sampler.wrapV)};
    const auto index = [](int value, int size, assets::TextureWrap wrap) {
        if (wrap == assets::TextureWrap::clampToEdge) {
            return std::clamp(value, 0, size - 1);
        }

        const int period = wrap == assets::TextureWrap::repeat ? size : size * 2;
        value %= period;

        if (value < 0) {
            value += period;
        }

        return value >= size ? period - 1 - value : value;
    };

    const auto alpha = [&](int x, int y) {
        const auto column = index(x, static_cast<int>(image->width), texture.sampler.wrapU),
                   row = index(y, static_cast<int>(image->height), texture.sampler.wrapV);
        return image->pixels.at((static_cast<std::size_t>(row) * image->width + column) * 4 + 3) / 255.0;
    };

    const auto pixel = uv * glm::dvec2{image->width, image->height};

    if (texture.sampler.min == assets::TextureFilter::nearest &&
        texture.sampler.mag == assets::TextureFilter::nearest) {
        return alpha(static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)));
    }

    const auto base = glm::floor(pixel - 0.5), fraction = pixel - 0.5 - base;
    const int x = static_cast<int>(base.x), y = static_cast<int>(base.y);

    return glm::mix(glm::mix(alpha(x, y), alpha(x + 1, y), fraction.x),
                    glm::mix(alpha(x, y + 1), alpha(x + 1, y + 1), fraction.x),
                    fraction.y);
}

bool covered(const assets::ModelData& model,
             const assets::MeshData& mesh,
             const assets::MaterialData& material,
             std::size_t index,
             const TriangleHit& hit,
             float opacity) {
    if (material.alphaMode == assets::AlphaMode::opaque) {
        return true;
    }

    double alpha = material.baseColor.a * opacity;

    if (material.baseColorTexture) {
        const auto& reference = *material.baseColorTexture;
        const auto uv = [&](std::size_t corner) {
            const auto& vertex = mesh.vertices[mesh.indices[index + corner]];

            return glm::dvec2{reference.uvSet ? vertex.uv1 : vertex.uv};
        };

        alpha *= textureAlpha(model, reference, uv(0) * (1 - hit.u - hit.v) + uv(1) * hit.u + uv(2) * hit.v);
    }

    return material.alphaMode == assets::AlphaMode::mask ? alpha >= material.alphaCutoff : alpha > 0;
}

} // namespace

Geometry::Geometry(const assets::ModelData& model) {
    bounds_.reserve(model.meshes.size());

    for (const auto& mesh : model.meshes) {
        Bounds bounds{glm::dvec3{std::numeric_limits<double>::max()},
                      glm::dvec3{std::numeric_limits<double>::lowest()}};
        for (const auto& vertex : mesh.vertices) {
            bounds.minimum = glm::min(bounds.minimum, glm::dvec3{vertex.position});
            bounds.maximum = glm::max(bounds.maximum, glm::dvec3{vertex.position});
        }

        bounds_.push_back(bounds);
    }
}

std::optional<double> Geometry::intersect(const Ray& ray,
                                          const assets::ModelInstance& instance,
                                          const glm::mat4& world,
                                          std::optional<std::size_t> material,
                                          double closest,
                                          float opacity) {
    const auto& model = instance.model();
    const auto& pose = instance.pose();
    double nearest = std::min(closest, ray.maximum);
    std::optional<double> result;

    for (const auto node : pose.activeNodes) {
        const glm::dmat4 transform = glm::dmat4{world} * glm::dmat4{pose.world[node]};
        const double determinant = glm::determinant(transform);

        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-15) {
            continue;
        }

        const auto inverse = glm::inverse(transform);
        const glm::dvec3 origin{inverse * glm::dvec4{ray.origin, 1}}, direction{inverse * glm::dvec4{ray.direction, 0}};

        // Do not normalize the local direction: preserve t across nonuniform/mirrored scales.
        for (const auto meshIndex : model.nodes[node].meshes) {
            const auto& mesh = model.meshes[meshIndex];
            auto bounds = bounds_.at(meshIndex);
            const bool deformed = !pose.joints[node].empty() || mesh.morphTargetCount != 0;

            if (deformed) {
                positions_.resize(mesh.vertices.size());
                bounds = {glm::dvec3{std::numeric_limits<double>::max()},
                          glm::dvec3{std::numeric_limits<double>::lowest()}};
                for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
                    const auto& vertex = mesh.vertices[index];
                    auto position = vertex.position;

                    for (std::uint32_t target = 0; target < mesh.morphTargetCount; ++target) {
                        const auto morphIndex =
                            static_cast<std::size_t>(target) * mesh.morphVertexCount + vertex.morphIndex;
                        const auto& delta = mesh.morphDeltas.at(morphIndex);
                        position += glm::vec3{delta.position} * pose.weights[node].at(target);
                    }

                    if (!pose.joints[node].empty()) {
                        glm::mat4 skin{0};

                        for (int lane = 0; lane < 4; ++lane) {
                            skin += pose.joints[node].at(vertex.joints0[lane]) * vertex.weights0[lane];
                            skin += pose.joints[node].at(vertex.joints1[lane]) * vertex.weights1[lane];
                        }

                        position = glm::vec3{skin * glm::vec4{position, 1}};
                    }

                    positions_[index] = position;
                    bounds.minimum = glm::min(bounds.minimum, positions_[index]);
                    bounds.maximum = glm::max(bounds.maximum, positions_[index]);
                }
            }

            const glm::dvec3 padding = 1e-6 * glm::max(glm::dvec3{1}, glm::abs(bounds.maximum - bounds.minimum));

            if (!box(origin, direction, bounds.minimum - padding, bounds.maximum + padding, ray.minimum, nearest)) {
                continue;
            }

            const auto point = [&](std::uint32_t index) {
                return deformed ? positions_.at(index) : glm::dvec3{mesh.vertices.at(index).position};
            };

            const auto& surface = model.materials.at(material.value_or(mesh.material));

            for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
                if (const auto hit = triangle(origin,
                                              direction,
                                              point(mesh.indices[index]),
                                              point(mesh.indices[index + 1]),
                                              point(mesh.indices[index + 2]),
                                              surface.doubleSided,
                                              ray.minimum,
                                              nearest)) {
                    if (covered(model, mesh, surface, index, *hit, opacity)) {
                        nearest = hit->distance;
                        result = nearest;
                    }
                }
            }
        }
    }

    return result;
}

} // namespace grimlock::picking
