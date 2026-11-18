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
        Scene scene{"Level 02 - Gravewatch Hollow"};
        const auto landscape = scene.createObject("Gravewatch continuous landscape");
        const auto scenery = scene.createObject("Graveyard scenery");
        HeightfieldConfiguration c;
        c.size = {300, 260};
        c.resolution = {301, 261};
        c.playableSize = {42, 42};
        c.seed = 2202;
        c.noiseAmplitude = 0.13F;
        c.noiseFrequency = 0.19F;
        c.boundaryHeight = 4;
        c.roadWidth = 3.6F;
        c.roadEndWidth = 4;
        c.grassColor = {0.10F, 0.22F, 0.12F};
        c.roadColor = {0.27F, 0.24F, 0.20F};
        c.rockColor = {0.23F, 0.25F, 0.30F};
        c.hills = {{{-10, 0}, 8, 1.4F},
                   {{7, -6}, 8, 1.8F},
                   {{9, 14}, 11, 2.2F},
                   {{-18, 12}, 7, 3},
                   {{15, -17}, 8, 3},
                   {{-23, -23}, 11, 5},
                   {{26, 21}, 14, 5},
                   {{-31, 16}, 12, 6},
                   {{30, -23}, 12, 5}};
        c.road = {{-15, 0.3F, -14},
                  {-12, 0.35F, -10},
                  {-8, 0.45F, -8},
                  {-5, 0.6F, -4},
                  {-2, 0.7F, -1},
                  {1, 0.8F, 1},
                  {5, 0.9F, 3},
                  {9, 1.2F, 6},
                  {11, 1.7F, 10},
                  {11, 2.0F, 13}};
        const std::array<glm::vec3, 6> zones{
            {{-11, 0.8F, -5}, {-3, 1.2F, -8}, {-5, 1.0F, 2}, {4, 1.3F, -2}, {5, 1.5F, 9}, {15, 2, 10}}};
        c.pads = {{{-15, -14}, 2.2F, 0.3F}, {{11, 15}, 4, 2}};
        for (const auto p : zones) {
            c.pads.push_back({{p.x, p.z}, 1.5F, p.y});
        }
        scene.setHeightfield(landscape, c);
        scene.setRole(landscape, "grimlock.terrain");
        const auto terrain = scene.object(landscape).heightfield;
        const auto height = [&](float x, float z) {
            return terrain->sample({x, z})->height;
        };
        const auto add = [&](const std::string& name,
                             const std::string& model,
                             glm::vec3 position,
                             glm::vec3 scale,
                             float yaw = 0,
                             const std::string& role = "grimlock.decoration",
                             ObjectId parent = 0) {
            const auto id = scene.createObject(name, parent ? std::optional<ObjectId>{parent} : std::nullopt);
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
                       "environment/graveyard/" + model + ".glb",
                       {x, height(x, z), z},
                       glm::vec3{scale},
                       yaw,
                       role,
                       scenery);
        };
        const auto crypt =
            prop("Forgotten Crypt - skeleton portal", "crypt-large", -15, -17, 2.4F, 0, "grimlock.obstacle");
        (void)add("Broken crypt roof",
                  "environment/graveyard/crypt-large-roof.glb",
                  {0, 1, 0},
                  {1, 1, 1},
                  0,
                  "grimlock.decoration",
                  crypt);
        const auto portal = add("Cold crypt entrance",
                                "environment/graveyard/door_glow_warm.glb",
                                {0, 0, 1.22F},
                                {0.65F, 0.8F, 1},
                                0,
                                "grimlock.decoration",
                                crypt);
        scene.setTint(portal, {0.25F, 0.45F, 1.7F, 1});
        for (const float x : {-18.4F, -11.8F}) {
            (void)prop("Crypt broken pillar", "column-large", x, -15, 1.7F);
        }
        for (const float x : {-7.0F, 3.5F}) {
            const auto wall = prop("Ruined Gate wall", "stone-wall-damaged", x, -0.3F, 2.2F, 0, "grimlock.obstacle");
            auto t = scene.object(wall).local;
            t.scale = {2.8F, 1.4F, 1.2F};
            scene.setTransform(wall, t);
        }
        for (const float x : {-4.1F, 0.7F}) {
            (void)prop("Ruined Gate pillar", "pillar-square", x, -0.3F, 1.8F, 0, "grimlock.obstacle");
        }
        (void)prop("Broken gate debris", "debris", -5, 1, 1.2F);
        const auto keep =
            prop("Gravewatch Keep - defense objective", "crypt-large", 11, 16, 2.4F, 180, "grimlock.objective");
        (void)add("Keep slate roof",
                  "environment/graveyard/crypt-large-roof.glb",
                  {0, 1, 0},
                  {1, 1, 1},
                  0,
                  "grimlock.decoration",
                  keep);
        scene.setTint(keep, {0.86F, 0.9F, 1, 1});
        for (const float x : {6.5F, 15.5F}) {
            (void)prop("Keep stone buttress", "pillar-large", x, 15, 2.8F, 0, "grimlock.obstacle");
            (void)prop("Keep outer wall", "stone-wall", x, 18, 2.5F, 0, "grimlock.obstacle");
        }
        for (const float x : {8.2F, 13.8F}) {
            (void)prop("Keep watch brazier", "fire-basket", x, 13.7F, 1.8F);
            const auto fire = add("Blue watch flame",
                                  "environment/graveyard/brazier_flame.glb",
                                  {x, height(x, 13.7F) + 0.7F, 13.7F},
                                  {1, 1, 1});
            scene.setTint(fire, {0.3F, 0.65F, 1.5F, 1});
        }
        for (std::size_t i = 0; i < zones.size(); ++i) {
            const auto zone = scene.createObject("Gravewatch construction area " + std::to_string(i + 1));
            scene.setRole(zone, "grimlock.build_zone");
            scene.setTransform(zone, {zones[i], {1, 0, 0, 0}, {1.35F, 1, 1.35F}});
            const auto anchor = scene.createObject("Construction anchor", zone);
            scene.setRole(anchor, "grimlock.construction_anchor");
            scene.setTransform(anchor, {{0, 0, 0}, {1, 0, 0, 0}, {1 / 1.35F, 1, 1 / 1.35F}});
            const auto point = scene.createObject("Construction point", anchor);
            scene.setRole(point, "grimlock.construction_point");
        }
        std::uint32_t random = 2202;
        const auto unit = [&]() {
            random = random * 1664525U + 1013904223U;
            return static_cast<float>(random >> 8) / 16777216.0F;
        };
        const auto clear = [&](float x, float z) {
            for (const auto p : c.road) {
                if (glm::length(glm::vec2{x - p.x, z - p.z}) < 3.3F) {
                    return false;
                }
            }
            for (const auto p : zones) {
                if (glm::length(glm::vec2{x - p.x, z - p.z}) < 2.6F) {
                    return false;
                }
            }
            return glm::length(glm::vec2{x - 11, z - 16}) > 5 && glm::length(glm::vec2{x + 15, z + 17}) > 4;
        };
        const std::array<std::string, 5> graves{"gravestone-cross",
                                                "gravestone-round",
                                                "gravestone-broken",
                                                "gravestone-bevel",
                                                "cross-wood"};
        for (int i = 0; i < 110; ++i) {
            const float x = -18 + 36 * unit(), z = -17 + 35 * unit();
            if (clear(x, z)) {
                (void)
                    prop("Old burial ground", graves[i % graves.size()], x, z, 0.9F + unit() * 0.5F, 10 + unit() * 25);
            }
        }
        for (int i = 0; i < 240; ++i) {
            const float x = -50 + 100 * unit(), z = -42 + 87 * unit();
            if (std::abs(x) < 19 && std::abs(z) < 20) {
                continue;
            }
            const auto tree = add("Dark perimeter pine",
                                  "environment/platformer/tree-pine.glb",
                                  {x, height(x, z), z},
                                  glm::vec3{2 + unit() * 2},
                                  unit() * 360,
                                  "grimlock.decoration",
                                  scenery);
            scene.setTint(tree, {0.5F, 0.64F, 0.7F, 1});
            if (i % 7 == 0) {
                (void)add("Ancient ridge rocks",
                          "environment/platformer/rocks.glb",
                          {x, height(x, z), z},
                          glm::vec3{2 + unit() * 2},
                          unit() * 360,
                          "grimlock.decoration",
                          scenery);
            }
        }
        // Bare trunks use the existing primitive asset, with low branch counts and no foliage.
        for (int i = 0; i < 24; ++i) {
            const float x = -19 + 38 * unit(), z = -18 + 36 * unit();
            if (!clear(x, z)) {
                continue;
            }
            const float y = height(x, z);
            const auto trunk = add("Dead graveyard tree",
                                   "towers/structures/primitives/cube.glb",
                                   {x, y + 1.1F, z},
                                   {0.12F, 1.1F, 0.12F});
            scene.setTint(trunk, {0.16F, 0.13F, 0.16F, 1});
            for (int b = 0; b < 2; ++b) {
                const auto branch = add("Bare branch",
                                        "towers/structures/primitives/cube.glb",
                                        {x + (b ? 0.3F : -0.3F), y + 1.6F, z},
                                        {0.065F, 0.6F, 0.065F});
                auto t = scene.object(branch).local;
                t.rotation = glm::angleAxis(b ? 0.8F : -0.8F, glm::vec3{0, 0, 1});
                scene.setTransform(branch, t);
                scene.setTint(branch, {0.16F, 0.13F, 0.16F, 1});
            }
        }
        (void)prop("Collapsed chapel wall", "stone-wall-damaged", -14, 5, 2.5F, 25, "grimlock.obstacle");
        (void)prop("Forgotten chapel altar", "altar-stone", -15, 8, 2.2F);
        (void)prop("Collapsed chapel pillar", "gravestone-debris", -12, 7, 1.8F);
        Camera camera;
        camera.position = {-36, 44, -44};
        camera.target = {0, 1, 1};
        camera.verticalFovDegrees = 42;
        camera.farPlane = 240;
        scene.setCamera("main", {camera, {}});
        SceneLighting lighting;
        lighting.values.directional = {{-0.4F, -0.85F, -0.3F}, 2.6F, {0.85F, 0.9F, 1}};
        lighting.values.environmentIntensity = 0.95F;
        lighting.values.pointCount = 3;
        lighting.values.points[0] = {{-15, 1.6F, -14.5F}, 7, {0.3F, 0.4F, 1}, 4};
        lighting.values.points[1] = {{-2, 2, -0.3F}, 5, {0.5F, 0.25F, 1}, 3};
        lighting.values.points[2] = {{11, 3.5F, 13}, 7, {0.3F, 0.6F, 1}, 4};
        scene.setLighting(lighting);
        scene.save(root / "scenes/gravewatch_hollow.json");
        nlohmann::json level{
            {"version", 1},
            {"id", "38d63a72-c95f-44be-85b1-e303f0c5e002"},
            {"required_level", "68369f21-3bfd-4d83-a5c2-c09c52922401"},
            {"name", "Level 02 - Gravewatch Hollow"},
            {"description", "Hold the ruined gate and defend Gravewatch Keep against the waking dead."},
            {"scene", "scenes/gravewatch_hollow.json"},
            {"available_heroes", {"knight"}},
            {"hero_spawns", {{-1, 0.7, 0}}},
            {"starting_gold", 40},
            {"objective",
             {{"position", {11, 12}}, {"height", height(11, 12)}, {"health", 14}, {"damage_per_arrival", 1}}},
            {"spawns",
             {{{"name", "Forgotten Crypt"},
               {"minimum", {-15.4, -12.8}},
               {"maximum", {-14.6, -12.2}},
               {"height", 0.3}}}},
            {"waves", nlohmann::json::array()}};
        const auto group = [](const char* enemy, int count, double start, double interval) {
            return nlohmann::json{{"enemy", enemy},
                                  {"spawn", "Forgotten Crypt"},
                                  {"count", count},
                                  {"batch_size", 1},
                                  {"start", start},
                                  {"interval", interval}};
        };
        const auto wave = [&](const char* name, int reward, nlohmann::json groups) {
            if (groups.is_object()) {
                groups = nlohmann::json::array({groups});
            }
            level["waves"].push_back({{"name", name}, {"gold_reward", reward}, {"groups", groups}});
        };
        wave("The Awakening", 16, {group("Skeleton", 6, 0, 2)});
        wave("The Restless Dead", 20, {group("Skeleton", 7, 0, 1.8), group("Skeleton", 3, 4, 3)});
        wave("The Graveguard", 24, {group("Skeleton", 9, 0, 1.8), group("Skeleton", 3, 2, 4)});
        wave("The Undead Assault",
             30,
             {group("Skeleton", 9, 0, 1.4), group("Skeleton", 4, 4, 2.8), group("Skeleton", 3, 1, 5)});
        wave("The Skeleton Captain",
             0,
             {group("Skeleton Captain", 1, 5, 1),
              group("Skeleton", 7, 0, 2),
              group("Skeleton", 3, 8, 3),
              group("Skeleton", 2, 2, 5)});
        std::ofstream{root / "levels/gravewatch_hollow.json"} << level.dump(2) << '\n';
        std::cout << "Authored Gravewatch Hollow without modifying Greenhaven.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
