#include <cmath>
#include <fstream>
#include <grimlock/scene.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace grimlock;

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("Expected game/assets directory");
        }
        const std::filesystem::path root{argv[1]};
        Scene scene{"Level 01 - Greenhaven Valley"};
        // Preserve the shipped objective and construction IDs used by saves and editor tools.
        for (int i = 1; i <= 111; ++i) {
            (void)scene.createObject();
        }
        for (int i = 5; i <= 111; ++i) {
            if (i != 105 && (i < 108 || i > 111)) {
                scene.removeObject(i);
            }
        }
        scene.setName(1, "Greenhaven continuous landscape");
        scene.setName(2, "Woodland scenery");
        scene.setName(3, "Western forest portal");
        scene.setName(4, "Castle hill courtyard");

        HeightfieldConfiguration c;
        c.size = {160, 120};
        c.resolution = {161, 121};
        c.minimalistic = true;
        c.playableSize = {38, 38};
        c.seed = 1701;
        c.noiseAmplitude = 0;
        c.noiseFrequency = 0.25F;
        c.boundaryHeight = 7;
        c.roadWidth = 3.2F;
        c.grassColor = {0.13F, 0.40F, 0.065F};
        c.roadColor = {0.46F, 0.31F, 0.15F};
        c.rockColor = {0.28F, 0.31F, 0.27F};
        c.hills = {{{7, 5}, 6, 1.2F},
                   {{-8.5F, 13}, 9, 2.4F},
                   {{0, 4.7F}, 4, 2.3F},
                   {{-1, -4.5F}, 4, 2.5F},
                   {{18, 16}, 8, 4},
                   {{-19, 6}, 8, 6},
                   {{11, -19}, 7, 4},
                   {{-16, -18}, 8, 5},
                   {{-30, 25}, 12, 6},
                   {{29, 29}, 13, 8},
                   {{34, -23}, 11, 6},
                   {{-36, -24}, 14, 7},
                   {{-14, -7}, 3, -0.5F}};
        c.road = {{12, 0, -3.7F},
                  {10, 0, -3.5F},
                  {7, 0, -2.3F},
                  {4, 0, -0.8F},
                  {1, 0, 0},
                  {-2, 0, 0.3F},
                  {-4.5F, 0.35F, 2},
                  {-6.3F, 0.8F, 4.5F},
                  {-7, 1.3F, 7},
                  {-8.5F, 2, 10.8F},
                  {-8.5F, 2, 12}};
        c.pads = {{{11, -3.7F}, 2.4F, 0},
                  {{5.5F, -4.6F}, 2.1F, 0},
                  {{7, 4.6F}, 2.1F, 1},
                  {{-4.5F, 5}, 2.1F, 1},
                  {{-8.5F, 13}, 4.6F, 2}};
        c.pads = {{{0, 0}, 27, 0}};
        scene.setHeightfield(1, c);
        scene.setRole(1, "grimlock.terrain");
        const auto terrain = scene.object(1).heightfield;
        const auto height = [&](float x, float z) {
            return terrain->sample({x, z})->height;
        };
        const auto add = [&](const std::string& name,
                             const std::string& model,
                             glm::vec3 position,
                             glm::vec3 scale,
                             float yaw = 0,
                             const std::string& role = "grimlock.decoration",
                             ObjectId parent = 2) {
            const auto id = scene.createObject(name, parent);
            scene.setModel(id, model);
            scene.setRole(id, role);
            scene.setTransform(id, {position, glm::angleAxis(glm::radians(yaw), glm::vec3{0, 1, 0}), scale});
            return id;
        };
        const auto prop = [&](const std::string& name,
                              const std::string& model,
                              float x,
                              float z,
                              float scale,
                              float yaw = 0,
                              const std::string& role = "grimlock.decoration") {
            return add(name,
                       "environment/platformer/" + model + ".glb",
                       {x, height(x, z), z},
                       glm::vec3{scale},
                       yaw,
                       role);
        };
        const glm::vec4 stone{0.55F, 0.53F, 0.7F, 1};
        for (const float z : {-4.9F, -2.5F}) {
            for (const float y : {0.3F, 0.9F, 1.5F}) {
                const auto id = add("Portal stone jamb",
                                    "towers/structures/primitives/cube.glb",
                                    {12, y, z},
                                    {0.45F, 0.29F, 0.34F},
                                    0,
                                    "grimlock.decoration",
                                    3);
                scene.setTint(id, stone);
            }
        }
        for (int i = 0; i <= 8; ++i) {
            const float angle = i * glm::pi<float>() / 8;
            const auto id = add("Portal arch stone",
                                "towers/structures/primitives/cube.glb",
                                {12, 1.5F + 1.2F * std::sin(angle), -3.7F + 1.2F * std::cos(angle)},
                                {0.45F, 0.33F, 0.27F},
                                0,
                                "grimlock.decoration",
                                3);
            auto t = scene.object(id).local;
            t.rotation = glm::angleAxis(glm::half_pi<float>() - angle, glm::vec3{1, 0, 0});
            scene.setTransform(id, t);
            scene.setTint(id, stone);
        }
        const auto glow = add("Red enemy portal",
                              "environment/graveyard/portal_glow_red.glb",
                              {12.15F, 0, -3.7F},
                              {1.6F, 1.6F, 1.6F},
                              -90,
                              "grimlock.decoration",
                              3);
        scene.setShadowProperties(glow, {false, true});

        // Retain the existing castle/crypt asset, objective footprint, doorway and collapse hierarchy.
        const auto foundation = add("Castle stone foundation",
                                    "towers/structures/primitives/cube.glb",
                                    {-8.5F, 2.06F, 14.2F},
                                    {1.68F, 0.06F, 1.81F},
                                    0,
                                    "grimlock.decoration",
                                    4);
        scene.setTint(foundation, stone);
        scene.setName(105, "Defense objective - hilltop castle");
        scene.setParent(105, 4);
        scene.setModel(105, "environment/graveyard/crypt-large.glb");
        scene.setRole(105, "grimlock.objective");
        scene.setTransform(
            105,
            {{-8.5F, 2.12F, 14.2F}, glm::angleAxis(glm::pi<float>(), glm::vec3{0, 1, 0}), {1.8F, 2, 1.5F}});
        (void)add("Castle pitched roof",
                  "environment/graveyard/crypt-large-roof.glb",
                  {0, 1, 0},
                  {1, 1, 1},
                  0,
                  "grimlock.decoration",
                  105);
        const auto door = add("Warm castle doorway",
                              "environment/graveyard/door_glow_warm.glb",
                              {0, 0, 1.21F},
                              {0.5F, 0.5F, 1},
                              180,
                              "grimlock.decoration",
                              105);
        scene.setShadowProperties(door, {false, true});
        for (int step = 0; step < 3; ++step) {
            const auto id = add("Castle entrance step",
                                "towers/structures/primitives/cube.glb",
                                {-8.5F, 2.025F + step * 0.04F, 11.7F + step * 0.28F},
                                {1.05F, 0.025F + step * 0.02F, 0.2F},
                                0,
                                "grimlock.decoration",
                                4);
            scene.setTint(id, stone);
        }
        for (const float x : {-3.6F, -5.6F, -7.6F, -9.6F, -11.6F}) {
            (void)add("Courtyard rear wall",
                      "environment/graveyard/stone-wall.glb",
                      {x, 2, 16.4F},
                      {2, 1.1F, 1.5F},
                      0,
                      "grimlock.obstacle",
                      4);
        }
        for (const float z : {12.8F, 14.8F}) {
            (void)add("Courtyard outer wall",
                      "environment/graveyard/stone-wall.glb",
                      {-12.4F, 2, z},
                      {2, 1.1F, 1.5F},
                      90,
                      "grimlock.obstacle",
                      4);
        }
        for (const float x : {-6.1F, -10.9F}) {
            (void)add("Castle banner",
                      "environment/castle/flag.glb",
                      {x, 2, 14.2F},
                      {1.6F, 1.6F, 1.6F},
                      -90,
                      "grimlock.decoration",
                      4);
            const auto id = add("Brazier plinth",
                                "towers/structures/primitives/cube.glb",
                                {x, 2.35F, 12.2F},
                                {0.38F, 0.35F, 0.38F},
                                0,
                                "grimlock.decoration",
                                4);
            scene.setTint(id, stone);
            (void)add("Brazier basket",
                      "environment/graveyard/fire-basket.glb",
                      {x, 2.7F, 12.2F},
                      {1.7F, 1.7F, 1.7F},
                      0,
                      "grimlock.decoration",
                      4);
            const auto fire = add("Golden brazier flame",
                                  "environment/graveyard/brazier_flame.glb",
                                  {x, 2.85F, 12.2F},
                                  {1, 1, 1},
                                  0,
                                  "grimlock.decoration",
                                  4);
            scene.setShadowProperties(fire, {false, true});
        }
        const std::array<glm::vec3, 4> zones{{{5.5F, 0, -4.6F}, {7, 0, 4.6F}, {-4.5F, 0, 5}, {-5.8F, 0, 10.8F}}};
        const std::array<glm::vec2, 4> lanes{{{5, -1.5F}, {3, 0}, {-6.3F, 4.5F}, {-7, 7.5F}}};
        for (std::size_t i = 0; i < zones.size(); ++i) {
            const auto id = 108 + i;
            scene.setName(id, "Valley construction area " + std::to_string(i + 1));
            scene.setRole(id, "grimlock.build_zone");
            scene.setTransform(id, {zones[i], {1, 0, 0, 0}, {1.3F, 1, 1.3F}});
            const auto anchor = scene.createObject("Construction anchor", id);
            scene.setRole(anchor, "grimlock.construction_anchor");
            scene.setTransform(anchor, {{0, 0, 0}, {1, 0, 0, 0}, {1 / 1.3F, 1, 1 / 1.3F}});
            const auto point = scene.createObject("Construction point", anchor);
            scene.setRole(point, "grimlock.construction_point");
            const float yaw = std::atan2(lanes[i].x - zones[i].x, lanes[i].y - zones[i].z);
            scene.setTransform(point, {{0, 0, 0}, glm::angleAxis(yaw, glm::vec3{0, 1, 0}), {1, 1, 1}});
        }
        // Low ridges flank the pass. Rocks use the existing obstacle classification.
        for (const auto p : {glm::vec2{0, 4.7F}, {-1, -4.5F}, {-2, -5.8F}, {1, 5.5F}}) {
            (void)prop("Rocky pass landmark", "rocks", p.x, p.y, 2.4F, p.x * 20, "grimlock.obstacle");
        }
        for (const auto p : {glm::vec2{13, -7},
                             {15, -2},
                             {13, 2},
                             {9, 7},
                             {4, 8},
                             {-3, 10},
                             {-14, 10},
                             {-14, 16},
                             {-3, 17},
                             {2, -9},
                             {-8, -10}}) {
            (void)prop("Valley pine", "tree-pine", p.x, p.y, 1.8F, p.x * 23);
        }
        // Deterministic perimeter forest extends far beyond the separate navigation boundary.
        std::uint32_t random = 1701;
        const auto unit = [&]() {
            random = random * 1664525U + 1013904223U;
            return static_cast<float>(random >> 8) / 16777216.0F;
        };
        for (int i = 0; i < 220; ++i) {
            const float x = -45 + 90 * unit(), z = -34 + 72 * unit();
            if (std::abs(x) < 17 && z > -12 && z < 20) {
                continue;
            }
            if (glm::length(glm::vec2{x - 12, z + 3.7F}) < 4) {
                continue;
            }
            const float scale = 1.7F + unit() * 1.8F, yaw = unit() * 360;
            (void)prop("Boundary woodland pine", i % 4 == 0 ? "tree" : "tree-pine", x, z, scale, yaw);
            if (i % 5 == 0) {
                (void)prop("Ridge rocks", "rocks", x + 1, z, 2 + unit() * 2, yaw);
            }
        }
        for (int i = 0; i < 65; ++i) {
            const float x = -14 + 28 * unit(), z = -10 + 27 * unit();
            bool clear = true;
            for (const auto zone : zones) {
                if (glm::length(glm::vec2{x - zone.x, z - zone.z}) < 2.8F) {
                    clear = false;
                }
            }
            for (const auto point : c.road) {
                if (glm::length(glm::vec2{x - point.x, z - point.z}) < 2.3F) {
                    clear = false;
                }
            }
            if (clear && glm::length(glm::vec2{x + 8.5F, z - 14.2F}) > 5) {
                (void)prop("Meadow flowers", i % 2 ? "flowers" : "plant", x, z, 0.8F + unit() * 0.5F, unit() * 360);
            }
        }
        for (const auto p : {glm::vec2{10, -7.2F}, {7, -7.2F}, {-3, 14.8F}, {-3, 17}}) {
            const auto id = prop("Low timber fence", "fence-low-straight", p.x, p.y, 2.3F);
            auto t = scene.object(id).local;
            t.scale = {3, 2, 1.3F};
            scene.setTransform(id, t);
        }
        (void)prop("Portal supply crate", "crate", 9, -6.8F, 1.5F, 12);
        (void)prop("Portal spare crate", "crate", 10.7F, -6.7F, 1.2F, -10);
        (void)prop("Portal supply barrel", "barrel", 8, -6.8F, 1.3F);
        // The shallow eastern swale is reserved for a future stream material, clear of the main road.
        (void)prop("Eastern stream-bank rocks", "rocks", -15, -6, 1.8F, 30);
        (void)prop("Eastern stream-bank reeds", "plant", -14, -7, 1.3F);

        Camera camera;
        camera.position = {-25, 31, -33};
        camera.target = {0, 1.5F, 3};
        camera.verticalFovDegrees = 38;
        camera.farPlane = 220;
        scene.setCamera("main", {camera, {}});
        SceneLighting lighting;
        lighting.values.directional = {{-0.45F, -0.85F, -0.3F}, 2.8F, {1, 0.95F, 0.86F}};
        lighting.values.environmentIntensity = 0.85F;
        lighting.values.pointCount = 3;
        lighting.values.points[0] = {{11.2F, 1.1F, -3.7F}, 8, {1, 0.035F, 0.015F}, 4};
        lighting.values.points[1] = {{-6.1F, 3.1F, 12.2F}, 5, {1, 0.55F, 0.08F}, 3};
        lighting.values.points[2] = {{-10.9F, 3.1F, 12.2F}, 5, {1, 0.55F, 0.08F}, 3};
        scene.setLighting(lighting);
        scene.setTransform(4, {{0, -2, 0}, {1, 0, 0, 0}, {1, 1, 1}});
        scene.save(root / "scenes/forest_defense.json");
        const auto levelPath = root / "levels/forest_defense.json";
        nlohmann::json level;
        {
            std::ifstream input{levelPath};
            input >> level;
        }
        level["objective"]["height"] = 0;
        for (auto& spawn : level["hero_spawns"]) {
            spawn[1] = 0;
        }
        level["name"] = "Level 01 - Greenhaven Valley";
        level["description"] = "Defend the winding valley road, hold the rocky pass, and protect the hilltop castle.";
        std::ofstream{levelPath} << level.dump(4) << '\n';
        std::cout << "Authored Greenhaven Valley using shared Engine heightfield queries.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
