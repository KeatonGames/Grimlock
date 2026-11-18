#include <algorithm>
#include <cmath>
#include <grimlock/scene.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class F> void rejected(F action) {
    bool invalid = false;
    try {
        action();
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    require(invalid, "Invalid terrain input accepted");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected output path");
        HeightfieldConfiguration c;
        c.size = {24, 20};
        c.playableSize = {16, 14};
        c.resolution = {49, 41};
        c.seed = 19;
        c.hills = {{{-3, 2}, 4, 3}, {{4, -3}, 3, -0.7F}};
        c.road = {{-8, 0, -4}, {0, 0, 0}, {7, 1, 5}};
        c.pads = {{{6, 4}, 2, 1}};
        Heightfield field{c}, same{c};
        const auto& mesh = field.model()->meshes[0];
        require(mesh.indices.size() == 48 * 40 * 6, "Incorrect terrain index count");
        require(mesh.vertices == same.model()->meshes[0].vertices, "Terrain is not deterministic");
        c.seed = 20;
        Heightfield other{c};
        require(mesh.vertices != other.model()->meshes[0].vertices, "Seed does not affect terrain");
        for (const auto& v : mesh.vertices) {
            require(std::abs(glm::length(v.normal) - 1) < 1e-5 && v.normal.y > 0, "Incorrect vertex normal");
            require(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1, "Invalid UV");
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices.at(mesh.indices[i]).position;
            const auto b = mesh.vertices.at(mesh.indices[i + 1]).position;
            const auto d = mesh.vertices.at(mesh.indices[i + 2]).position;
            require(glm::cross(b - a, d - a).y > 0, "Terrain triangle winding is inverted");
            // Exercise both halves of every cell against the actual rendered triangle plane.
            const auto p = a * 0.2F + b * 0.3F + d * 0.5F;
            const auto sample = field.sample({p.x, p.z});
            require(sample && std::abs(sample->height - p.y) < 1e-4, "Height query disagrees with rendered triangle");
            require(glm::dot(sample->normal, glm::normalize(glm::cross(b - a, d - a))) > 0.999F,
                    "Normal query disagrees with triangle");
        }
        const auto sample = field.sample({1.37F, -2.19F});
        const auto hit = field.intersect({{1.37, 100, -2.19}, {0, -1, 0}, 0, 200});
        require(hit && std::abs(hit->y - sample->height) < 1e-4, "Raycast and height query disagree");
        require(field.sample({12, 10}).has_value() && !field.sample({12.01F, 0}), "Terrain edge query failed");
        require(field.playable({0, 0}) && !field.playable({10, 0}) && field.sample({10, 0}),
                "Visual and playable extents not independent");
        require(!field.sample({std::numeric_limits<float>::quiet_NaN(), 0}), "Nonfinite query accepted");
        require(!field.intersect({{0, 10, 0}, {0, -1, 0}, 0, 1}), "Ray interval ignored");
        require(std::abs(field.sample({6, 4})->height - 1) < 1e-5, "Building pad is not flat");
        c.resolution = {0, 3};
        rejected([&] {
            Heightfield invalid{c};
        });
        c.resolution = {514, 3};
        rejected([&] {
            Heightfield invalid{c};
        });
        c.resolution = {49, 41};
        c.pads[0].radius = 0;
        rejected([&] {
            Heightfield invalid{c};
        });
        c.pads[0].radius = 2;
        c.noiseAmplitude = std::numeric_limits<float>::infinity();
        rejected([&] {
            Heightfield invalid{c};
        });
        Scene scene;
        const auto id = scene.createObject("Terrain");
        scene.setHeightfield(id, field.configuration());
        scene.setRole(id, "grimlock.terrain");
        rejected([&] {
            scene.setModel(id, "test.glb");
        });
        const auto previous = scene.object(id).heightfield;
        rejected([&] {
            scene.setHeightfield(id, c);
        });
        require(scene.object(id).heightfield == previous, "Invalid edit changed terrain");
        scene.selectObject(id);
        require(scene.selectedObject() == id, "Heightfield is not selectable");
        scene.save(argv[1]);
        const auto loaded = Scene::load(argv[1]);
        require(loaded->object(id).heightfield->model()->meshes[0].vertices == mesh.vertices,
                "Scene roundtrip changed heightfield");
        require(loaded->object(id).heightfield->model()->meshes[0].indices == mesh.indices,
                "Scene roundtrip changed indices");
        auto dense = field.configuration();
        dense.resolution = {258, 258};
        Heightfield large{dense};
        const auto& indices = large.model()->meshes[0].indices;
        require(*std::max_element(indices.begin(), indices.end()) == 258 * 258 - 1,
                "Heightfield indices overflowed 16 bits");
        std::cout << "PASS heightfield generation, triangles, normals, UVs, queries, raycasts, pads, boundaries and "
                     "serialization\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
