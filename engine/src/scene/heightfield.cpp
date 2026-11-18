#include "grimlock/heightfield.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock {
namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
}

bool finite(float value) {
    return std::isfinite(value) && std::abs(value) <= 100000;
}

template <glm::length_t N> bool finite(glm::vec<N, float> value) {
    for (int i = 0; i < N; ++i) {
        if (!finite(value[i])) {
            return false;
        }
    }
    return true;
}

void validate(const HeightfieldConfiguration& c) {
    if (!finite(c.size) || glm::any(glm::lessThan(c.size, glm::vec2{1})) ||
        glm::any(glm::greaterThan(c.size, glm::vec2{2048})) || glm::any(glm::lessThan(c.resolution, glm::uvec2{2})) ||
        glm::any(glm::greaterThan(c.resolution, glm::uvec2{513})) || !finite(c.playableSize) ||
        glm::any(glm::lessThan(c.playableSize, glm::vec2{1})) || glm::any(glm::greaterThan(c.playableSize, c.size)) ||
        !finite(c.baseHeight) || !finite(c.noiseAmplitude) || c.noiseAmplitude < 0 || !finite(c.noiseFrequency) ||
        c.noiseFrequency < 0 || !finite(c.boundaryHeight) || c.boundaryHeight < 0 || !finite(c.roadWidth) ||
        c.roadWidth <= 0 || !finite(c.roadEndWidth) || c.roadEndWidth <= 0 || !finite(c.grassColor) ||
        !finite(c.roadColor) || !finite(c.rockColor) || c.hills.size() > 256 || c.pads.size() > 256 ||
        c.road.size() > 256 || c.road.size() == 1) {
        throw std::invalid_argument("Invalid heightfield dimensions, resolution, color or generation parameters");
    }
    for (const auto* features : {&c.hills, &c.pads}) {
        for (const auto& f : *features) {
            if (!finite(f.center) || !finite(f.radius) || f.radius <= 0 || !finite(f.height)) {
                throw std::invalid_argument("Invalid heightfield hill/pad");
            }
        }
    }
    for (const auto p : c.road) {
        if (!finite(p)) {
            throw std::invalid_argument("Invalid heightfield road point");
        }
    }
}

std::pair<float, float> roadDistance(const HeightfieldConfiguration& c, glm::vec2 p) {
    float distance = std::numeric_limits<float>::max(), height = c.baseHeight;
    for (std::size_t i = 1; i < c.road.size(); ++i) {
        const glm::vec2 a{c.road[i - 1].x, c.road[i - 1].z}, b{c.road[i].x, c.road[i].z};
        const auto edge = b - a;
        const float t =
            glm::dot(edge, edge) > 1e-8F ? std::clamp(glm::dot(p - a, edge) / glm::dot(edge, edge), 0.0F, 1.0F) : 0;
        const float d = glm::length(p - glm::mix(a, b, t));
        if (d < distance) {
            distance = d;
            height = glm::mix(c.road[i - 1].y, c.road[i].y, t);
        }
    }
    return {distance, height};
}

float roadWidth(const HeightfieldConfiguration& c, glm::vec2 p) {
    if (c.road.empty()) {
        return c.roadWidth;
    }
    const auto end = c.road.back();
    return glm::mix(c.roadEndWidth, c.roadWidth, smooth(2, 8, glm::length(p - glm::vec2{end.x, end.z})));
}

float elevation(const HeightfieldConfiguration& c, glm::vec2 p) {
    if (c.minimalistic && c.pads.size() == 1 && c.pads.front().center == glm::vec2{0} &&
        c.pads.front().radius >= glm::length(c.playableSize * 0.5F)) {
        // Uneven polygon sides preserve broad planar slopes without a square enclosure.
        float outside = -100000;
        for (int side = 0; side < 9; ++side) {
            const float angle = 0.19F + static_cast<float>(side) * 6.283185307F / 9;
            const glm::vec2 normal{std::cos(angle), std::sin(angle)};
            const float offset = c.pads.front().radius + (side % 3) * 1.8F;
            outside = std::max(outside, glm::dot(p, normal) - offset);
        }
        return c.pads.front().height + c.boundaryHeight * std::clamp(outside / 8.0F, 0.0F, 1.0F);
    }
    const float phase = static_cast<float>(c.seed % 10007) * 0.37F;
    float h = c.baseHeight +
              c.noiseAmplitude *
                  (std::sin(p.x * c.noiseFrequency + phase) * std::cos(p.y * c.noiseFrequency * 0.83F - phase) +
                   0.35F * std::sin(p.x * c.noiseFrequency * 1.9F + p.y * c.noiseFrequency * 1.3F + phase));
    for (const auto& f : c.hills) {
        const auto d = (p - f.center) / f.radius;
        h += f.height * std::exp(-glm::dot(d, d) * 2);
    }
    const auto edge = glm::abs(p) - c.playableSize * 0.5F;
    h += c.boundaryHeight * smooth(-1, 16, std::max(edge.x, edge.y));
    const auto [distance, roadHeight] = roadDistance(c, p);
    const auto width = roadWidth(c, p);
    h = glm::mix(h, roadHeight, 1 - smooth(width * 0.55F, width * 1.25F, distance));
    for (const auto& f : c.pads) {
        h = glm::mix(h, f.height, 1 - smooth(f.radius, f.radius + 2, glm::length(p - f.center)));
    }
    return h;
}
} // namespace

Heightfield::Heightfield(HeightfieldConfiguration configuration) : configuration_(std::move(configuration)) {
    validate(configuration_);
    const auto& c = configuration_;
    auto model = std::make_shared<assets::ModelData>();
    auto& mesh = model->meshes.emplace_back();
    mesh.name = "Continuous heightfield";
    const auto width = c.resolution.x, depth = c.resolution.y;
    mesh.vertices.resize(static_cast<std::size_t>(width) * depth);
    model->minimum = {-c.size.x / 2, std::numeric_limits<float>::max(), -c.size.y / 2};
    model->maximum = {c.size.x / 2, -std::numeric_limits<float>::max(), c.size.y / 2};
    for (std::uint32_t z = 0; z < depth; ++z) {
        for (std::uint32_t x = 0; x < width; ++x) {
            auto& v = mesh.vertices[z * width + x];
            v.uv = {static_cast<float>(x) / (width - 1), static_cast<float>(z) / (depth - 1)};
            const auto p = (v.uv - glm::vec2{0.5F}) * c.size;
            v.position = {p.x, elevation(c, p), p.y};
            model->minimum.y = std::min(model->minimum.y, v.position.y);
            model->maximum.y = std::max(model->maximum.y, v.position.y);
            const auto [distance, unused] = roadDistance(c, p);
            (void)unused;
            const float variation = 0.94F + 0.06F * std::sin(p.x * 0.7F + p.y * 0.3F);
            auto color = c.grassColor * variation;
            const auto edge = glm::abs(p) - c.playableSize * 0.5F;
            color = glm::mix(color, c.rockColor, smooth(1, 7, std::max(edge.x, edge.y)) * 0.65F);
            const auto roadSpan = roadWidth(c, p);
            color = glm::mix(color, c.roadColor, 1 - smooth(roadSpan * 0.38F, roadSpan * 0.65F, distance));
            v.color = glm::vec4{color, 1};
        }
    }
    for (std::uint32_t z = 0; z + 1 < depth; ++z) {
        for (std::uint32_t x = 0; x + 1 < width; ++x) {
            const auto a = z * width + x, b = a + 1, d = a + width, e = d + 1;
            mesh.indices.insert(mesh.indices.end(), {a, d, b, b, d, e});
        }
    }
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto& a = mesh.vertices[mesh.indices[i]];
        auto& b = mesh.vertices[mesh.indices[i + 1]];
        auto& d = mesh.vertices[mesh.indices[i + 2]];
        const auto normal = glm::cross(b.position - a.position, d.position - a.position);
        a.normal += normal;
        b.normal += normal;
        d.normal += normal;
    }
    for (auto& v : mesh.vertices) {
        v.normal = glm::normalize(v.normal);
        const float rock = smooth(0.15F, 0.6F, 1 - v.normal.y);
        v.color = glm::mix(v.color, glm::vec4{c.rockColor, 1}, rock);
    }
    if (c.minimalistic) {
        // Keep the grid vertices for exact height queries; render independent flat-shaded faces.
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices[mesh.indices[i]].position;
            const auto b = mesh.vertices[mesh.indices[i + 1]].position;
            const auto d = mesh.vertices[mesh.indices[i + 2]].position;
            const auto normal = glm::normalize(glm::cross(b - a, d - a));
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto vertex = mesh.vertices[mesh.indices[i + corner]];
                vertex.normal = normal;
                vertex.color = glm::vec4{c.grassColor, 1};
                mesh.indices[i + corner] = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
            }
        }
    }
    if (c.minimalistic && c.pads.size() == 1 && c.pads.front().center == glm::vec2{0} &&
        c.pads.front().radius >= glm::length(c.playableSize * 0.5F)) {
        // Build the banks on their exact polygon edges instead of cutting a sampled grid.
        mesh.indices.clear();
        const float base = c.pads.front().height;
        const auto corner = [&](int side, float expansion) {
            const int next = (side + 1) % 9;
            const float a = 0.19F + side * 6.283185307F / 9;
            const float b = 0.19F + next * 6.283185307F / 9;
            const glm::vec2 n{std::cos(a), std::sin(a)}, m{std::cos(b), std::sin(b)};
            const float u = c.pads.front().radius + (side % 3) * 1.8F + expansion;
            const float v = c.pads.front().radius + (next % 3) * 1.8F + expansion;
            const float determinant = n.x * m.y - n.y * m.x;
            return glm::vec2{(u * m.y - n.y * v) / determinant, (n.x * v - u * m.x) / determinant};
        };
        const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 d) {
            auto normal = glm::normalize(glm::cross(b - a, d - a));
            if (normal.y < 0) {
                std::swap(b, d);
                normal = -normal;
            }
            for (const auto position : {a, b, d}) {
                assets::Vertex vertex;
                vertex.position = position;
                vertex.normal = normal;
                vertex.color = glm::vec4{c.grassColor, 1};
                vertex.uv = {position.x / c.size.x + 0.5F, position.z / c.size.y + 0.5F};
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(vertex);
                model->minimum = glm::min(model->minimum, position);
                model->maximum = glm::max(model->maximum, position);
            }
        };
        const auto point = [](glm::vec2 p, float y) {
            return glm::vec3{p.x, y, p.y};
        };
        for (int side = 0; side < 9; ++side) {
            const int next = (side + 1) % 9;
            const auto a = point(corner(side, 0), base), b = point(corner(next, 0), base);
            const auto d = point(corner(side, 8), base + c.boundaryHeight);
            const auto e = point(corner(next, 8), base + c.boundaryHeight);
            const auto f = point(corner(side, 150), base + c.boundaryHeight);
            const auto g = point(corner(next, 150), base + c.boundaryHeight);
            triangle({0, base, 0}, a, b);
            triangle(a, d, b);
            triangle(b, d, e);
            triangle(d, f, e);
            triangle(e, f, g);
        }
    }
    model->materials[0].name = "Terrain vertex colors";
    model->materials[0].metallic = 0;
    model->materials[0].roughness = 0.95F;
    assets::ModelNode node;
    node.name = "Heightfield";
    node.meshes = {0};
    model->nodes.push_back(node);
    model->roots = {0};
    model_ = std::move(model);
}

bool Heightfield::playable(glm::vec2 position) const noexcept {
    return finite(position) && glm::all(glm::lessThanEqual(glm::abs(position), configuration_.playableSize * 0.5F));
}

std::optional<TerrainSample> Heightfield::sample(glm::vec2 position) const {
    if (!finite(position) || glm::any(glm::greaterThan(glm::abs(position), configuration_.size * 0.5F))) {
        return {};
    }
    const auto resolution = configuration_.resolution;
    const auto grid = (position / configuration_.size + glm::vec2{0.5F}) * glm::vec2{resolution - glm::uvec2{1}};
    const auto x = std::min(static_cast<std::uint32_t>(grid.x), resolution.x - 2);
    const auto z = std::min(static_cast<std::uint32_t>(grid.y), resolution.y - 2);
    const float u = grid.x - x, v = grid.y - z;
    const auto& vertices = model_->meshes[0].vertices;
    const auto a = z * resolution.x + x, b = a + 1, d = a + resolution.x, e = d + 1;
    const auto p0 = vertices[u + v <= 1 ? a : e].position;
    const auto p1 = vertices[d].position, p2 = vertices[b].position;
    const auto weights = u + v <= 1 ? glm::vec3{1 - u - v, v, u} : glm::vec3{u + v - 1, 1 - u, 1 - v};
    auto n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    if (n.y < 0) {
        n = -n;
    }
    return TerrainSample{p0.y * weights.x + p1.y * weights.y + p2.y * weights.z, n};
}

std::optional<glm::dvec3> Heightfield::intersect(const Ray& ray) const {
    if (!std::isfinite(glm::length(ray.origin)) || !std::isfinite(glm::length(ray.direction)) ||
        !std::isfinite(ray.minimum) || !std::isfinite(ray.maximum) || ray.minimum < 0 || ray.maximum < ray.minimum) {
        return {};
    }
    double nearest = ray.maximum;
    std::optional<glm::dvec3> result;
    const auto& mesh = model_->meshes[0];
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const glm::dvec3 a = mesh.vertices[mesh.indices[i]].position;
        const auto ab = glm::dvec3{mesh.vertices[mesh.indices[i + 1]].position} - a;
        const auto ac = glm::dvec3{mesh.vertices[mesh.indices[i + 2]].position} - a;
        const auto p = glm::cross(ray.direction, ac);
        const double determinant = glm::dot(ab, p);
        if (std::abs(determinant) < 1e-12) {
            continue;
        }
        const auto offset = ray.origin - a;
        const double u = glm::dot(offset, p) / determinant;
        const auto q = glm::cross(offset, ab);
        const double v = glm::dot(ray.direction, q) / determinant;
        const double t = glm::dot(ac, q) / determinant;
        if (u >= 0 && v >= 0 && u + v <= 1 && t >= ray.minimum && t <= nearest) {
            nearest = t;
            result = ray.origin + ray.direction * t;
        }
    }
    return result;
}
} // namespace grimlock
