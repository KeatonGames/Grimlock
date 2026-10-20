#include "renderer/grid_instances.hpp"

#include <cmath>
#include <fstream>
#include <grimlock/grid.hpp>
#include <grimlock/ray.hpp>
#include <grimlock/scene.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace grimlock;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F operation) {
    bool rejected = false;

    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }

    require(rejected, "invalid grid accepted");
}

void checkGridInstances() {
    Grid grid{{3, 2, 2, {-4, 3, 5}}};
    renderer::GridInstances instances;
    renderer::updateGridInstances(grid, instances);
    require(instances.floorCount == 6 && instances.cubes.size() == 6, "walkable grid needs one floor per cell");

    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto& cube = instances.cubes[index];
        const glm::vec3 center{*grid.cellCenter(*grid.cell(index))};
        require(cube.position.x == center.x && cube.position.z == center.z &&
                    std::abs(cube.position.y + cube.scale.y - center.y) < 1e-5F && cube.scale == glm::vec3{1, 0.2F, 1},
                "floor cube does not match the grid cell and surface height");
    }

    grid.setWalkable({0, 0}, false);
    grid.setWalkable({2, 1}, false);
    renderer::updateGridInstances(grid, instances);
    require(instances.floorCount == 6 && instances.cubes.size() == 8, "blocked cells lost floors or walls");
    require(instances.cubes[6].position == glm::vec3{-3, 4.5F, 6} &&
                instances.cubes[7].position == glm::vec3{1, 4.5F, 8} &&
                instances.cubes[7].scale == glm::vec3{1, 1.5F, 1},
            "walls do not sit above their blocked cells");

    const auto capacity = instances.cubes.capacity();
    grid.setWalkable({0, 0}, true);
    renderer::updateGridInstances(grid, instances);
    require(instances.cubes.size() == 7 && instances.cubes.capacity() == capacity &&
                instances.cubes.back().position == glm::vec3{1, 4.5F, 8},
            "unblocking failed to remove only its wall or reuse instance storage");

    grid.configure({1, 1, 0.5, {10, -2, 20}});
    renderer::updateGridInstances(grid, instances);
    require(instances.floorCount == 1 && instances.cubes.size() == 1 &&
                instances.cubes[0].position == glm::vec3{10.25F, -2.05F, 20.25F} &&
                instances.cubes[0].scale == glm::vec3{0.25F, 0.05F, 0.25F},
            "grid resizing retained stale geometry");

    Grid maximum{{256, 256, 1, {0, 0, 0}}};
    for (std::size_t index = 0; index < maximum.size(); ++index) {
        maximum.setWalkable(*maximum.cell(index), false);
    }
    renderer::updateGridInstances(maximum, instances);
    require(instances.floorCount == Grid::maximumCells && instances.cubes.size() == Grid::maximumCells * 2,
            "maximum grid lost floor or wall instances");

    rejects([&] {
        renderer::updateGridInstances(Grid{{1, 1, 1e100, {0, 0, 0}}}, instances);
    });
    rejects([&] {
        renderer::updateGridInstances(Grid{{1, 1, 1e-100, {0, 0, 0}}}, instances);
    });
    rejects([&] {
        renderer::updateGridInstances(Grid{{1, 1, 1, {0, 1e100, 0}}}, instances);
    });
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected Game source and output paths");

        const std::filesystem::path game{argv[1]}, output{argv[2]};
        std::filesystem::create_directories(output);
        checkGridInstances();
        Grid grid{{5, 3, 0.5, {-2, 7, -1}}};
        require(grid.size() == 15 && grid.revision() == 1, "grid initialization failed");

        for (std::size_t i = 0; i < grid.size(); ++i) {
            const auto cell = grid.cell(i);
            require(cell && grid.index(*cell) == i && grid.walkable(*cell), "index/initial walkability failed");

            const auto center = grid.cellCenter(*cell);
            require(center && center->y == 7 && grid.worldToCell(*center) == cell, "center/world conversion failed");

            auto high = *center;
            high.y = 1e9;
            require(grid.worldToCell(high) == cell, "grid depended on terrain height");
        }

        require(grid.worldToCell({-2, 0, -1}) == GridCell{0, 0} && grid.worldToCell({-1.5, 0, -0.5}) == GridCell{1, 1},
                "origin/cell boundary mapping failed");
        require(!grid.worldToCell({-2.001, 0, 0}) && !grid.worldToCell({0.5, 0, 0}) && !grid.worldToCell({0, 0, 0.5}),
                "far/negative boundaries accepted");
        require(!grid.worldToCell({std::numeric_limits<double>::quiet_NaN(), 0, 0}) && !grid.index({5, 0}) &&
                    !grid.cell(15) && !grid.cellCenter({0, 3}),
                "invalid query accepted");
        require(!grid.walkable({5, 0}) && !grid.setWalkable({5, 0}, false), "out-of-bounds write accepted");
        require(grid.neighbors4({0, 0}).count == 2 && grid.neighbors4({2, 1}).count == 4 &&
                    grid.neighbors4({5, 0}).count == 0,
                "neighbor bounds failed");

        const auto revision = grid.revision();
        require(grid.setWalkable({1, 1}, false) && grid.revision() == revision + 1, "blocked update did not revise");
        require(!grid.setWalkable({1, 1}, false) && grid.revision() == revision + 1, "unchanged state revised");

        const auto original = grid.configuration();
        grid.configure(original);
        require(grid.revision() == revision + 1, "unchanged configuration revised");

        auto resized = original;
        resized.width = 7;
        resized.height = 4;
        resized.cellSize = 2;
        resized.origin = {10, 0, 20};
        grid.configure(resized);
        require(!grid.walkable({1, 1}) && grid.walkable({6, 3}) &&
                    grid.worldToCell(*grid.cellCenter({6, 3})) == GridCell{6, 3},
                "resize overlap/new cells failed");

        rejects([&] {
            grid.configure({0, 4, 1, {0, 0, 0}});
        });
        rejects([&] {
            grid.configure({65536, 65536, 1, {0, 0, 0}});
        });
        rejects([&] {
            grid.configure({4, 4, 0, {0, 0, 0}});
        });
        rejects([&] {
            grid.configure({4, 4, 1, {std::numeric_limits<double>::infinity(), 0, 0}});
        });
        rejects([&] {
            grid.configure({4, 4, 1, {1e30, 0, 0}});
        });
        require(grid.configuration() == resized && !grid.walkable({1, 1}), "invalid configure changed grid");

        Grid single{{1, 1, 1, {0, 0, 0}}};
        require(single.neighbors4({0, 0}).count == 0, "single-cell neighbors failed");

        Grid maximum{{256, 256, 1, {0, 0, 0}}};
        require(maximum.size() == Grid::maximumCells && maximum.setWalkable({255, 255}, false) &&
                    maximum.neighbors4({255, 255}).count == 2,
                "maximum-size grid failed");

        Grid fractional{{3, 1, 0.1, {-0.2, 0, 0}}};
        const double end = -0.2 + 3 * 0.1;
        require(fractional.worldToCell({std::nextafter(end, -1.0), 0, 0.05}) == GridCell{2, 0} &&
                    !fractional.worldToCell({end, 0, 0.05}),
                "rounded far-edge conversion failed");

        Scene scene;
        scene.setNavigationGrid(original);
        scene.navigationGrid()->setWalkable({1, 1}, false);
        scene.save(output / "grid.json");
        auto loaded = Scene::load(output / "grid.json");
        require(loaded->navigationGrid() && loaded->navigationGrid()->configuration() == original &&
                    !loaded->navigationGrid()->walkable({1, 1}),
                "grid roundtrip failed");

        scene.setNavigationGrid({});
        scene.save(output / "without-grid.json");
        require(!Scene::load(output / "without-grid.json")->navigationGrid(), "optional grid did not disappear");
        require(!Scene::load(game / "assets/scenes/demo.json")->navigationGrid(), "legacy scene gained a grid");

        auto example = Scene::load(game / "assets/scenes/navigation.json");
        require(example->navigationGrid()->size() == 192 && !example->navigationGrid()->walkable({3, 3}) &&
                    example->objects().size() == 4 && example->object(4).model == "models/blackrat.glb",
                "Game test scene failed");

        std::ifstream stream(output / "grid.json");
        nlohmann::json source;
        stream >> source;
        const auto malformed = [&](nlohmann::json value) {
            {
                std::ofstream file(output / "invalid.json");
                file << value.dump();
            }

            rejects([&] {
                (void)Scene::load(output / "invalid.json");
            });
        };

        auto bad = source;
        bad["navigation_grid"]["width"] = 0;
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["width"] = -1;
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["cell_size"] = -1;
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["origin"] = {0, 0};
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["blocked"] = {{5, 0}};
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["blocked"] = {{1, 1}, {1, 1}};
        malformed(bad);

        bad = source;
        bad["navigation_grid"]["geometry"] = true;
        malformed(bad);

        Camera camera;
        camera.position = {2, 7, 9};
        camera.target = {0, 0, 0};
        Grid board{{16, 12, 1, {-8, 0, -6}}};

        for (const auto sizes : {std::array<glm::ivec2, 2>{{{1280, 720}, {1280, 720}}},
                                 std::array<glm::ivec2, 2>{{{900, 600}, {1800, 1200}}}}) {
            const auto center = *board.cellCenter({8, 6});
            const auto clip = glm::dmat4{camera.projection(static_cast<float>(sizes[1].x) / sizes[1].y)} *
                              glm::dmat4{camera.view()} * glm::dvec4{center, 1};
            const auto ndc = glm::dvec2{clip} / clip.w;
            const auto pixel = glm::vec2{(ndc + 1.0) * 0.5 * glm::dvec2{sizes[0]}};
            const auto ray = mouseRay(camera, pixel, sizes[0], sizes[1]);
            require(ray.has_value(), "screen ray failed");

            const auto point = intersectPlaneXZ(*ray, 0);
            require(point && board.worldToCell(*point) == GridCell{8, 6} && glm::length(*point - center) < 1e-4,
                    "projection/plane/grid DPI mapping failed");
        }

        require(!intersectPlaneXZ(Ray{{0, 1, 0}, {1, 0, 0}, 0, 100}, 0) &&
                    !intersectPlaneXZ(Ray{{0, 1, 0}, {0, 1, 0}, 0, 100}, 0),
                "parallel/behind plane hit accepted");

        std::cout << "PASS grid: bounded storage, allocation-free queries, coordinates, neighbors, "
                     "revisions, resize, JSON/legacy/invalid input, shared camera-ray/DPI mapping.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL grid: " << error.what() << '\n';

        return 1;
    }
}
