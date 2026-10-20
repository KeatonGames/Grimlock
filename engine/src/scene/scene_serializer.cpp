#include "core/startup_profile.hpp"
#include "grimlock/scene.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace grimlock {

namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(const std::string& field, const std::string& message) {
    throw std::invalid_argument(field + ": " + message);
}

void fields(const Json& j, const std::string& field, std::initializer_list<std::string_view> allowed) {
    if (!j.is_object()) {
        fail(field, "expected an object");
    }

    for (const auto& [key, value] : j.items()) {
        (void)value;

        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            fail(field + "." + key, "unsupported property");
        }
    }
}

const Json& required(const Json& j, const char* key, const std::string& field) {
    if (!j.contains(key)) {
        fail(field + "." + key, "required property is missing");
    }

    return j.at(key);
}

std::string string(const Json& j, const std::string& field) {
    if (!j.is_string()) {
        fail(field, "expected a string");
    }

    return j.get<std::string>();
}

bool boolean(const Json& j, const std::string& field) {
    if (!j.is_boolean()) {
        fail(field, "expected a boolean");
    }

    return j.get<bool>();
}

float number(const Json& j, const std::string& field) {
    if (!j.is_number()) {
        fail(field, "expected a finite number");
    }

    const auto value = j.get<double>();

    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        fail(field, "number exceeds finite float range");
    }

    return static_cast<float>(value);
}

std::size_t index(const Json& j, const std::string& field) {
    if (!j.is_number_unsigned() && !(j.is_number_integer() && j.get<std::int64_t>() >= 0)) {
        fail(field, "expected a nonnegative integer");
    }

    const auto value = j.get<std::uint64_t>();

    if (value > std::numeric_limits<std::size_t>::max()) {
        fail(field, "integer exceeds platform range");
    }

    return static_cast<std::size_t>(value);
}

ObjectId id(const Json& j, const std::string& field) {
    const auto text = string(j, field);
    ObjectId result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);

    if (text.empty() || text.front() == '0' || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        result == 0) {
        fail(field, "expected a nonzero canonical uint64 decimal string");
    }

    return result;
}

std::optional<ObjectId> reference(const Json& j, const char* key, const std::string& field) {
    if (!j.contains(key) || j.at(key).is_null()) {
        return {};
    }

    return id(j.at(key), field + "." + key);
}

template <glm::length_t N> glm::vec<N, float> vector(const Json& j, const std::string& field) {
    if (!j.is_array() || j.size() != N) {
        fail(field, "expected an array of " + std::to_string(N) + " numbers");
    }

    glm::vec<N, float> result{};

    for (glm::length_t i = 0; i < N; ++i) {
        result[i] = number(j.at(i), field + "[" + std::to_string(i) + "]");
    }

    return result;
}

template <glm::length_t N> Json json(const glm::vec<N, float>& v) {
    auto j = Json::array();

    for (glm::length_t i = 0; i < N; ++i) {
        if (!std::isfinite(v[i])) {
            fail("$", "cannot save non-finite vector");
        }

        j.push_back(v[i]);
    }

    return j;
}

void array(const Json& j, const std::string& field, std::size_t maximum) {
    if (!j.is_array() || j.size() > maximum) {
        fail(field, "expected array with at most " + std::to_string(maximum) + " entries");
    }
}

double gridNumber(const Json& j, const std::string& field) {
    if (!j.is_number()) {
        fail(field, "expected a finite number");
    }

    const auto value = j.get<double>();

    if (!std::isfinite(value)) {
        fail(field, "expected a finite number");
    }

    return value;
}

void readGrid(Scene& scene, const Json& j) {
    const std::string field = "$.navigation_grid";
    fields(j, field, {"width", "height", "cell_size", "origin", "blocked"});
    const auto width = index(required(j, "width", field), field + ".width"),
               height = index(required(j, "height", field), field + ".height");
    if (width > Grid::maximumCells || height > Grid::maximumCells) {
        fail(field, "dimensions exceed grid cell limit");
    }

    GridConfiguration configuration;
    configuration.width = static_cast<std::uint32_t>(width);
    configuration.height = static_cast<std::uint32_t>(height);

    if (j.contains("cell_size")) {
        configuration.cellSize = gridNumber(j.at("cell_size"), field + ".cell_size");
    }

    if (j.contains("origin")) {
        const auto& origin = j.at("origin");

        if (!origin.is_array() || origin.size() != 3) {
            fail(field + ".origin", "expected [x,y,z]");
        }

        for (int axis = 0; axis < 3; ++axis) {
            configuration.origin[axis] = gridNumber(origin.at(axis), field + ".origin");
        }
    }

    try {
        scene.setNavigationGrid(configuration);
    } catch (const std::exception& error) {
        fail(field, error.what());
    }

    if (j.contains("blocked")) {
        const auto& blocked = j.at("blocked");
        array(blocked, field + ".blocked", Grid::maximumCells);

        for (std::size_t i = 0; i < blocked.size(); ++i) {
            const auto f = field + ".blocked[" + std::to_string(i) + "]";
            const auto& cell = blocked.at(i);

            if (!cell.is_array() || cell.size() != 2) {
                fail(f, "expected [x,z]");
            }

            const auto x = index(cell.at(0), f + "[0]"), z = index(cell.at(1), f + "[1]");

            if (x >= width || z >= height) {
                fail(f, "cell is outside grid dimensions");
            }

            if (!scene.navigationGrid()->setWalkable({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)},
                                                     false)) {
                fail(f, "duplicate blocked cell");
            }
        }
    }
}

template <class F> void context(const std::string& field, F&& operation) {
    try {
        operation();
    } catch (const std::exception& e) {
        fail(field, e.what());
    }
}

void readTransform(Scene& scene, ObjectId object, const Json& j, const std::string& field) {
    fields(j, field, {"position", "rotation", "scale"});
    Transform value;

    if (j.contains("position")) {
        value.position = vector<3>(j.at("position"), field + ".position");
    }

    if (j.contains("rotation")) {
        const auto q = vector<4>(j.at("rotation"), field + ".rotation");
        value.rotation = {q.w, q.x, q.y, q.z};
    }

    if (j.contains("scale")) {
        value.scale = vector<3>(j.at("scale"), field + ".scale");
    }

    context(field, [&] {
        scene.setTransform(object, value);
    });
}

AnimationSettings readAnimation(const Json& j, const std::string& field) {
    fields(j, field, {"clip", "autoplay", "time", "speed", "loop", "playing", "paused", "rest_pose"});
    AnimationSettings value;

    if (j.contains("clip")) {
        if (j.at("clip").is_string()) {
            value.clipName = string(j.at("clip"), field + ".clip");
        } else {
            value.clip = index(j.at("clip"), field + ".clip");
        }
    }

    if (j.contains("time")) {
        value.time = number(j.at("time"), field + ".time");
    }

    if (j.contains("speed")) {
        value.speed = number(j.at("speed"), field + ".speed");
    }

    if (j.contains("loop")) {
        value.loop = boolean(j.at("loop"), field + ".loop");
    }

    if (j.contains("playing")) {
        value.playing = boolean(j.at("playing"), field + ".playing");
    }

    if (j.contains("paused")) {
        value.paused = boolean(j.at("paused"), field + ".paused");
    }

    if (j.contains("rest_pose")) {
        value.restPose = boolean(j.at("rest_pose"), field + ".rest_pose");
    }

    if (j.contains("autoplay")) {
        value.playing = boolean(j.at("autoplay"), field + ".autoplay");
        value.paused = false;
        value.restPose = !value.playing;
        value.time = 0;
    }

    return value;
}

SceneCamera readCamera(const Json& j, const std::string& field) {
    fields(j, field, {"id", "object", "position", "target", "up", "vertical_fov_degrees", "near", "far"});
    SceneCamera value;
    value.object = reference(j, "object", field);

    if (j.contains("position")) {
        value.camera.position = vector<3>(j.at("position"), field + ".position");
    }

    if (j.contains("target")) {
        value.camera.target = vector<3>(j.at("target"), field + ".target");
    }

    if (j.contains("up")) {
        value.camera.up = vector<3>(j.at("up"), field + ".up");
    }

    if (j.contains("vertical_fov_degrees")) {
        value.camera.verticalFovDegrees = number(j.at("vertical_fov_degrees"), field + ".vertical_fov_degrees");
    }

    if (j.contains("near")) {
        value.camera.nearPlane = number(j.at("near"), field + ".near");
    }

    if (j.contains("far")) {
        value.camera.farPlane = number(j.at("far"), field + ".far");
    }

    return value;
}

SceneLighting readLighting(const Json& j, const std::string& field) {
    fields(j, field, {"directional", "points", "spots", "environment_intensity", "environment_rotation", "exposure"});
    SceneLighting value;

    if (j.contains("environment_intensity")) {
        value.values.environmentIntensity = number(j.at("environment_intensity"), field + ".environment_intensity");
    }

    if (j.contains("environment_rotation")) {
        value.values.environmentRotation = number(j.at("environment_rotation"), field + ".environment_rotation");
    }

    if (j.contains("exposure")) {
        value.values.exposure = number(j.at("exposure"), field + ".exposure");
    }

    if (j.contains("directional")) {
        const auto& light = j.at("directional");
        const auto f = field + ".directional";
        fields(light, f, {"object", "direction", "color", "intensity"});
        auto& d = value.values.directional;
        value.directionalObject = reference(light, "object", f);

        if (light.contains("direction")) {
            d.direction = vector<3>(light.at("direction"), f + ".direction");
        }

        if (light.contains("color")) {
            d.color = vector<3>(light.at("color"), f + ".color");
        }

        if (light.contains("intensity")) {
            d.intensity = number(light.at("intensity"), f + ".intensity");
        }

        Lighting test;
        test.directional = d;
        context(f, [&] {
            validateLighting(test);
        });
    }

    for (const auto* key : {"points", "spots"}) {
        if (!j.contains(key)) {
            continue;
        }

        const auto& lights = j.at(key);
        const bool spot = std::string_view{key} == "spots";
        array(lights, field + "." + key, maxPointLights);

        for (std::size_t i = 0; i < lights.size(); ++i) {
            const auto& light = lights[i];
            const auto f = field + "." + key + "[" + std::to_string(i) + "]";

            if (spot) {
                fields(light,
                       f,
                       {"object", "position", "color", "intensity", "range", "direction", "inner_cone", "outer_cone"});
            } else {
                fields(light, f, {"object", "position", "color", "intensity", "range"});
            }

            const auto read = [&](auto& result) {
                if (light.contains("position")) {
                    result.position = vector<3>(light.at("position"), f + ".position");
                }

                if (light.contains("color")) {
                    result.color = vector<3>(light.at("color"), f + ".color");
                }

                if (light.contains("intensity")) {
                    result.intensity = number(light.at("intensity"), f + ".intensity");
                }

                if (light.contains("range")) {
                    result.range = number(light.at("range"), f + ".range");
                }
            };

            if (spot) {
                auto& s = value.values.spots[i];
                read(s);
                value.spotObjects[i] = reference(light, "object", f);

                if (light.contains("direction")) {
                    s.direction = vector<3>(light.at("direction"), f + ".direction");
                }

                if (light.contains("inner_cone")) {
                    s.innerCone = number(light.at("inner_cone"), f + ".inner_cone");
                }

                if (light.contains("outer_cone")) {
                    s.outerCone = number(light.at("outer_cone"), f + ".outer_cone");
                }

                Lighting test;
                test.spots[0] = s;
                test.spotCount = 1;
                context(f, [&] {
                    validateLighting(test);
                });
            } else {
                read(value.values.points[i]);
                value.pointObjects[i] = reference(light, "object", f);
                Lighting test;
                test.points[0] = value.values.points[i];
                test.pointCount = 1;
                context(f, [&] {
                    validateLighting(test);
                });
            }
        }

        if (spot) {
            value.values.spotCount = static_cast<std::uint32_t>(lights.size());
        } else {
            value.values.pointCount = static_cast<std::uint32_t>(lights.size());
        }
    }

    return value;
}

Json json(const AnimationConfiguration& a) {
    Json result = {{"autoplay", a.autoplay}, {"speed", a.speed}, {"loop", a.loop}};

    if (a.clipName) {
        result["clip"] = *a.clipName;
    } else {
        result["clip"] = a.clip;
    }

    return result;
}

void attach(Json& j, std::optional<ObjectId> object) {
    if (object) {
        j["object"] = std::to_string(*object);
    }
}

} // namespace

std::unique_ptr<Scene> Scene::load(const std::filesystem::path& path) {
    diagnostics::StartupSpan profile{"scene.read_parse_validate"};

    try {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);

        if (!stream) {
            fail("$", "cannot open scene file");
        }

        const auto size = stream.tellg();

        if (size < 0 || size > 16 * 1024 * 1024) {
            fail("$", "scene exceeds 16 MiB");
        }

        stream.seekg(0);
        std::vector<std::set<std::string>> keys;
        const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 128) {
                fail("$", "JSON nesting exceeds 128 levels");
            }

            if (event == Json::parse_event_t::object_start) {
                keys.emplace_back();
            } else if (event == Json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second) {
                fail("$", "duplicate JSON key: " + parsed.get<std::string>());
            } else if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
            }

            return true;
        };

        Json root;
        context("$", [&] {
            root = Json::parse(stream, callback);
        });
        fields(root,
               "$",
               {"version", "name", "next_id", "objects", "cameras", "active_camera", "lighting", "navigation_grid"});
        if (index(required(root, "version", "$"), "$.version") != 1) {
            fail("$.version", "unsupported scene format version (expected 1)");
        }

        auto scene = std::make_unique<Scene>(root.contains("name") ? string(root.at("name"), "$.name") : "Untitled");
        scene->sourcePath_ = path;

        if (root.contains("navigation_grid") && !root.at("navigation_grid").is_null()) {
            readGrid(*scene, root.at("navigation_grid"));
        }

        if (root.contains("objects")) {
            const auto& objects = root.at("objects");
            array(objects, "$.objects", 10000);

            for (std::size_t i = 0; i < objects.size(); ++i) {
                const auto& j = objects[i];
                const auto f = "$.objects[" + std::to_string(i) + "]";
                fields(j,
                       f,
                       {"id",
                        "name",
                        "parent",
                        "model",
                        "transform",
                        "enabled",
                        "tint",
                        "material_override",
                        "animation"});
                const auto object = id(required(j, "id", f), f + ".id");
                context(f + ".id", [&] {
                    scene->insertObject(object, j.contains("name") ? string(j.at("name"), f + ".name") : "");
                });

                if (j.contains("transform")) {
                    readTransform(*scene, object, j.at("transform"), f + ".transform");
                }

                if (j.contains("enabled")) {
                    scene->setEnabled(object, boolean(j.at("enabled"), f + ".enabled"));
                }

                if (j.contains("model") && !j.at("model").is_null()) {
                    const auto text = string(j.at("model"), f + ".model");
                    context(f + ".model", [&] {
                        scene->setModel(object, std::filesystem::path{std::u8string{text.begin(), text.end()}});
                    });
                }

                if (j.contains("tint")) {
                    scene->setTint(object, vector<4>(j.at("tint"), f + ".tint"));
                }

                if (j.contains("material_override")) {
                    scene->setMaterialOverride(object, index(j.at("material_override"), f + ".material_override"));
                }

                if (j.contains("animation")) {
                    scene->setAnimation(object, readAnimation(j.at("animation"), f + ".animation"));
                }

                if (!scene->object(object).model && (j.contains("animation") || j.contains("material_override"))) {
                    fail(f, "animation/material_override requires a model");
                }
            }

            for (std::size_t i = 0; i < objects.size(); ++i) {
                const auto f = "$.objects[" + std::to_string(i) + "]";
                context(f + ".parent", [&] {
                    scene->setParent(id(objects[i].at("id"), f + ".id"), reference(objects[i], "parent", f));
                });
            }
        }

        if (root.contains("next_id")) {
            const auto next = id(root.at("next_id"), "$.next_id");

            if (next < scene->nextId_) {
                fail("$.next_id", "must exceed every object ID");
            }

            scene->nextId_ = next;
        }

        if (root.contains("cameras")) {
            const auto& cameras = root.at("cameras");
            array(cameras, "$.cameras", 10000);

            if (cameras.empty()) {
                fail("$.cameras", "at least one camera is required");
            }

            scene->cameras_.clear();

            for (std::size_t i = 0; i < cameras.size(); ++i) {
                const auto f = "$.cameras[" + std::to_string(i) + "]";
                const auto& j = cameras[i];
                const auto camera = readCamera(j, f);
                const auto key = string(required(j, "id", f), f + ".id");

                if (scene->cameras_.contains(key)) {
                    fail(f + ".id", "duplicate camera ID");
                }

                context(f, [&] {
                    scene->setCamera(key, camera);
                });

                if (i == 0) {
                    scene->activeCamera_ = key;
                }
            }
        }

        if (root.contains("active_camera")) {
            context("$.active_camera", [&] {
                scene->setActiveCamera(string(root.at("active_camera"), "$.active_camera"));
            });
        }

        if (root.contains("lighting")) {
            context("$.lighting", [&] {
                scene->setLighting(readLighting(root.at("lighting"), "$.lighting"));
            });
        }

        context("$.objects", [&] {
            scene->updateTransforms();
        });
        context("$.cameras", [&] {
            (void)scene->resolvedCamera();
        });
        context("$.lighting", [&] {
            (void)scene->resolvedLighting();
        });

        return scene;
    } catch (const std::exception& e) {
        throw std::runtime_error("Scene '" + path.string() + "': " + e.what());
    }
}

void Scene::save(const std::filesystem::path& path) const {
    try {
        updateTransforms();
        (void)resolvedCamera();
        (void)resolvedLighting();
        Json root = {{"version", 1},
                     {"name", name_},
                     {"next_id", std::to_string(nextId_)},
                     {"active_camera", activeCamera_},
                     {"objects", Json::array()},
                     {"cameras", Json::array()}};
        if (const auto* grid = navigationGrid()) {
            const auto& c = grid->configuration();
            Json blocked = Json::array();

            for (std::size_t i = 0; i < grid->size(); ++i) {
                const auto cell = *grid->cell(i);

                if (!grid->walkable(cell)) {
                    blocked.push_back({cell.x, cell.z});
                }
            }

            root["navigation_grid"] = {{"width", c.width},
                                       {"height", c.height},
                                       {"cell_size", c.cellSize},
                                       {"origin", Json::array({c.origin.x, c.origin.y, c.origin.z})},
                                       {"blocked", std::move(blocked)}};
        }

        for (const auto& [id, value] : objects_) {
            const auto& t = value.local;
            Json j = {{"id", std::to_string(id)},
                      {"name", value.name},
                      {"enabled", value.enabled},
                      {"tint", json(value.tint)},
                      {"transform",
                       {{"position", json(t.position)},
                        {"rotation", json(glm::vec4{t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w})},
                        {"scale", json(t.scale)}}}};
            if (value.parent) {
                j["parent"] = std::to_string(*value.parent);
            }

            if (value.model) {
                const auto text = value.model->generic_u8string();
                j["model"] = std::string{text.begin(), text.end()};
            }

            if (value.materialOverride) {
                j["material_override"] = *value.materialOverride;
            }

            if (value.initialAnimation || (value.instance_ && !value.instance_->model().animations.empty())) {
                j["animation"] = json(animationConfiguration(id));
            }

            root["objects"].push_back(std::move(j));
        }

        for (const auto& [id, value] : cameras_) {
            const auto& c = value.camera;
            Json j = {{"id", id},
                      {"position", json(c.position)},
                      {"target", json(c.target)},
                      {"up", json(c.up)},
                      {"vertical_fov_degrees", c.verticalFovDegrees},
                      {"near", c.nearPlane},
                      {"far", c.farPlane}};
            attach(j, value.object);
            root["cameras"].push_back(std::move(j));
        }

        const auto& l = lighting_.values;
        const auto& d = l.directional;
        Json lights = {
            {"environment_intensity", l.environmentIntensity},
            {"environment_rotation", l.environmentRotation},
            {"exposure", l.exposure},
            {"directional", {{"direction", json(d.direction)}, {"color", json(d.color)}, {"intensity", d.intensity}}},
            {"points", Json::array()},
            {"spots", Json::array()}};
        attach(lights["directional"], lighting_.directionalObject);

        for (std::uint32_t i = 0; i < l.pointCount; ++i) {
            const auto& p = l.points[i];
            Json j = {{"position", json(p.position)},
                      {"color", json(p.color)},
                      {"intensity", p.intensity},
                      {"range", p.range}};
            attach(j, lighting_.pointObjects[i]);
            lights["points"].push_back(std::move(j));
        }

        for (std::uint32_t i = 0; i < l.spotCount; ++i) {
            const auto& s = l.spots[i];
            Json j = {{"position", json(s.position)},
                      {"color", json(s.color)},
                      {"intensity", s.intensity},
                      {"range", s.range},
                      {"direction", json(s.direction)},
                      {"inner_cone", s.innerCone},
                      {"outer_cone", s.outerCone}};
            attach(j, lighting_.spotObjects[i]);
            lights["spots"].push_back(std::move(j));
        }

        root["lighting"] = std::move(lights);
        const auto text = root.dump(2) + '\n'; // Build/validate before opening the destination.
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);

        if (!stream) {
            fail("$", "cannot open output file");
        }

        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();

        if (!stream) {
            fail("$", "failed writing scene file");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Scene '" + path.string() + "': " + e.what());
    }
}

} // namespace grimlock
