#include "scene/picking.hpp"

#include "assets/model_loader.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <grimlock/scene.hpp>
#include <iostream>
#include <stdexcept>

using namespace grimlock;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

picking::Ray ray(glm::dvec3 origin, glm::dvec3 direction = {0, 0, -1}) {
    return {origin, glm::normalize(direction), 0.1, 100};
}

std::shared_ptr<assets::ModelData> triangleModel() {
    auto model = std::make_shared<assets::ModelData>();
    model->meshes.resize(1);
    auto& mesh = model->meshes[0];
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    mesh.vertices[0].position = {-1, -1, 0};
    mesh.vertices[1].position = {1, -1, 0};
    mesh.vertices[2].position = {0, 1, 0};
    model->nodes.resize(1);
    model->nodes[0].meshes = {0};
    model->roots = {0};
    return model;
}

void rays() {
    Camera camera;
    camera.position = {0, 0, 5};
    camera.target = {0, 0, 0};
    const auto center = picking::mouseRay(camera, {400, 300}, {800, 600}, {1600, 1200});
    require(center && glm::length(center->direction - glm::dvec3{0, 0, -1}) < 1e-6,
            "center ray/DPI direction");
    const auto top = picking::mouseRay(camera, {200, 100}, {800, 600}, {1600, 1200});
    require(top && top->direction.x < 0 && top->direction.y > 0, "Vulkan top-left convention");
    const auto scaled = picking::mouseRay(camera, {400, 200}, {1600, 1200}, {3200, 2400});
    require(scaled && glm::length(scaled->direction - top->direction) < 1e-8,
            "DPI/logical coordinate mismatch");
    const glm::vec3 point{0.8F, -0.3F, 0};
    for (const auto extent : {glm::ivec2{800, 600}, glm::ivec2{1500, 700}, glm::ivec2{600, 1000}}) {
        auto clip = camera.projection(static_cast<float>(extent.x) / extent.y) * camera.view() *
                    glm::vec4{point, 1};
        clip /= clip.w;
        const glm::vec2 cursor = (glm::vec2{clip} + 1.0F) * 0.5F * glm::vec2{extent};
        const auto result = picking::mouseRay(camera, cursor, extent, extent * 2);
        require(result && glm::length(glm::cross(result->direction,
                                                 glm::dvec3{point} - result->origin)) < 1e-5,
                "projection/unprojection after resize");
    }
    require(!picking::mouseRay(camera, {-1, 10}, {800, 600}, {800, 600}), "outside click accepted");
    require(!picking::mouseRay(camera, {10, 10}, {800, 600}, {0, 0}), "minimized ray accepted");
    require(center->minimum > 0 && center->maximum > 99, "camera clipping interval");
}

void geometry() {
    auto model = triangleModel();
    assets::ModelInstance instance{model};
    picking::Geometry geometry{*model};
    auto hit = geometry.intersect(ray({0, 0, 5}), instance, glm::mat4{1}, {}, 100);
    require(hit && std::abs(*hit - 5) < 1e-8, "triangle hit/distance");
    require(!geometry.intersect(ray({0.9, 0.9, 5}), instance, glm::mat4{1}, {}, 100),
            "AABB false positive selected");
    require(!geometry.intersect(ray({0, 0, -5}, {0, 0, 1}), instance, glm::mat4{1}, {}, 100),
            "back-facing material selected");
    model->materials[0].doubleSided = true;
    require(
        geometry.intersect(ray({0, 0, -5}, {0, 0, 1}), instance, glm::mat4{1}, {}, 100).has_value(),
        "double-sided triangle missed");
    require(!geometry.intersect(ray({0, 0, 5}), instance, glm::mat4{1}, {}, 4),
            "farther candidate replaced nearest");
    auto nearRay = ray({0, 0, 0.05});
    require(!geometry.intersect(nearRay, instance, glm::mat4{1}, {}, 100),
            "near-clipped geometry selected");
    auto transform = glm::translate(glm::mat4{1}, glm::vec3{1, 0, -2}) *
                     glm::rotate(glm::mat4{1}, 0.4F, glm::vec3{0, 1, 0}) *
                     glm::scale(glm::mat4{1}, glm::vec3{-2, 0.7F, 3});
    hit = geometry.intersect(ray({1, 0, 5}), instance, transform, {}, 100);
    require(hit && std::abs(*hit - 7) < 1e-5,
            "nonuniform mirrored transform changed world distance");
    model->nodes.resize(2);
    model->nodes[0].meshes.clear();
    model->nodes[0].children = {1};
    model->nodes[1].parent = 0;
    model->nodes[1].meshes = {0};
    model->nodes[0].localTransform = glm::translate(glm::mat4{1}, glm::vec3{2, 0, 0});
    model->nodes[1].localTransform = glm::translate(glm::mat4{1}, glm::vec3{1, 0, -1});
    assets::ModelInstance hierarchy{model};
    picking::Geometry hierarchyGeometry{*model};
    hit = hierarchyGeometry.intersect(ray({3, 0, 5}), hierarchy, glm::mat4{1}, {}, 100);
    require(hit && std::abs(*hit - 6) < 1e-8, "imported child hierarchy missed");
    assets::ModelInstance second{model};
    hit = hierarchyGeometry.intersect(ray({3, 0, 5}), second,
                                      glm::translate(glm::mat4{1}, glm::vec3{0, 0, 2}), {}, 100);
    require(hit && std::abs(*hit - 4) < 1e-8, "shared mesh independent instance transform");
}

void deformation() {
    auto model = triangleModel();
    auto& mesh = model->meshes[0];
    mesh.morphVertexCount = 3;
    mesh.morphTargetCount = 1;
    mesh.morphDeltas.resize(3);
    for (auto& delta : mesh.morphDeltas) {
        delta.position = {3, 0, 0, 0};
    }
    model->nodes[0].weights = {1};
    assets::ModelInstance morphed{model};
    picking::Geometry geometry{*model};
    require(geometry.intersect(ray({3, 0, 5}), morphed, glm::mat4{1}, {}, 100).has_value(),
            "morphed bounds/triangle missed");
    require(!geometry.intersect(ray({0, 0, 5}), morphed, glm::mat4{1}, {}, 100),
            "rest triangles retained after morph");
    model->nodes.resize(3);
    model->nodes[0].skin = 0;
    model->nodes[0].children = {1, 2};
    model->nodes[1].parent = 0;
    model->nodes[2].parent = 0;
    model->nodes[2].matrixTransform = false;
    model->nodes[2].translation = {2, 0, 0};
    assets::SkinData skin;
    skin.joints = {1, 2};
    skin.inverseBindMatrices = {glm::mat4{1}, glm::mat4{1}};
    model->skins = {skin};
    for (auto& vertex : mesh.vertices) {
        vertex.joints1.x = 1;
        vertex.weights1.x = 1;
    }
    assets::AnimationData clip;
    clip.end = 1;
    assets::AnimationSampler sampler;
    sampler.times = {0, 1};
    sampler.values = {2, 0, 0, 4, 0, 0};
    sampler.components = 3;
    clip.samplers.push_back(sampler);
    clip.channels.push_back({0, 2, assets::AnimationPath::translation});
    model->animations.push_back(clip);
    assets::ModelInstance animated{model};
    animated.play(0, false);
    animated.seek(1);
    picking::Geometry skinnedGeometry{*model};
    const auto hit = skinnedGeometry.intersect(ray({7, 0, 5}), animated, glm::mat4{1}, {}, 100);
    require(hit && std::abs(*hit - 5) < 1e-6,
            "morph-before-skin / second influence set / current clip pose");
    require(!skinnedGeometry.intersect(ray({3, 0, 5}), animated, glm::mat4{1}, {}, 100),
            "animated bounds used rest skin");
}

void alphaCoverage() {
    auto model = triangleModel();
    auto& mesh = model->meshes[0];
    model->materials[0].alphaMode = assets::AlphaMode::mask;
    model->images.push_back(
        assets::ImageData{"alpha", 2, 1, {255, 255, 255, 0, 255, 255, 255, 255}});
    assets::TextureData texture;
    texture.sampler.min = texture.sampler.mag = assets::TextureFilter::nearest;
    model->textures.push_back(texture);
    model->materials[0].baseColorTexture = assets::TextureReference{};
    for (auto& vertex : mesh.vertices) {
        vertex.uv = {0.25F, 0.5F};
        vertex.uv1 = {0.75F, 0.5F};
    }
    assets::ModelInstance instance{model};
    picking::Geometry geometry{*model};
    const auto front = ray({0, 0, 5});
    require(!geometry.intersect(front, instance, glm::mat4{1}, {}, 100),
            "transparent cutout picked");
    model->materials[0].baseColorTexture->uvSet = 1;
    require(geometry.intersect(front, instance, glm::mat4{1}, {}, 100).has_value(),
            "opaque UV1 cutout missed");
    require(!geometry.intersect(front, instance, glm::mat4{1}, {}, 100, 0),
            "instance alpha ignored");
    model->materials[0].baseColorTexture->uvSet = 0;
    model->materials[0].baseColorTexture->offset = {0.5F, 0};
    require(geometry.intersect(front, instance, glm::mat4{1}, {}, 100).has_value(),
            "UV transform alpha mismatch");
    model->materials.push_back(assets::MaterialData{});
    require(geometry.intersect(front, instance, glm::mat4{1}, 1, 100, 0).has_value(),
            "opaque override incorrectly rejected alpha");
    model->materials[0].alphaMode = assets::AlphaMode::blend;
    model->materials[0].baseColor.a = 0;
    require(!geometry.intersect(front, instance, glm::mat4{1}, {}, 100), "zero-alpha blend picked");
}

void selection() {
    Scene scene;
    const auto parent = scene.createObject(), a = scene.createObject("a", parent),
               b = scene.createObject("b");
    scene.setModel(a, "models/cube.obj");
    scene.setModel(b, "models/cube.obj");
    scene.selectObject(a);
    require(scene.selectedObject() == a, "stable selection missing");
    scene.selectObject(b);
    require(scene.selectedObject() == b, "selection not replaced");
    scene.setSelectable(b, false);
    require(!scene.selectedObject(), "non-selectable selection retained");
    scene.selectObject(a);
    scene.setEnabled(parent, false);
    require(!scene.selectedObject(), "disabled ancestor retained selection");
    scene.setEnabled(parent, true);
    require(!scene.selectedObject(), "disabled selection resurrected");
    scene.selectObject(a);
    scene.removeObject(parent);
    require(!scene.selectedObject(), "deleted descendant selection retained");
    scene.setSelectable(b, true);
    scene.selectObject(b);
    scene.clearSelection();
    require(!scene.selectedObject(), "clearSelection failed");
}

void suppliedModels(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file() &&
            (extension == ".glb" || extension == ".gltf" || extension == ".obj")) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    require(!paths.empty(), "no supplied models found");
    for (const auto& path : paths) {
        const auto name = path.filename().string();
        auto model = std::make_shared<assets::ModelData>(assets::loadModel(path));
        assets::ModelInstance instance{model};
        if (!model->animations.empty()) {
            instance.play(model->animations.size() > 1 ? 1 : 0);
            instance.seek(0.3F);
        }
        picking::Geometry geometry{*model};
        const auto& pose = instance.pose();
        bool hit = false;
        for (const auto node : pose.activeNodes) {
            for (const auto meshIndex : model->nodes[node].meshes) {
                const auto& mesh = model->meshes[meshIndex];
                const auto position = [&](std::uint32_t index) {
                    const auto& vertex = mesh.vertices[index];
                    auto value = vertex.position;
                    for (std::uint32_t target = 0; target < mesh.morphTargetCount; ++target) {
                        value += glm::vec3{mesh.morphDeltas[static_cast<std::size_t>(target) *
                                                                mesh.morphVertexCount +
                                                            vertex.morphIndex]
                                               .position} *
                                 pose.weights[node][target];
                    }
                    if (!pose.joints[node].empty()) {
                        glm::mat4 skin{0};
                        for (int lane = 0; lane < 4; ++lane) {
                            skin += pose.joints[node][vertex.joints0[lane]] * vertex.weights0[lane];
                            skin += pose.joints[node][vertex.joints1[lane]] * vertex.weights1[lane];
                        }
                        value = glm::vec3{skin * glm::vec4{value, 1}};
                    }
                    return glm::dvec3{pose.world[node] * glm::vec4{value, 1}};
                };
                const auto stride = std::max<std::size_t>(3, mesh.indices.size() / 30 / 3 * 3);
                for (std::size_t triangle = 0; triangle + 2 < mesh.indices.size() && !hit;
                     triangle += stride) {
                    const auto a = position(mesh.indices[triangle]),
                               b = position(mesh.indices[triangle + 1]),
                               c = position(mesh.indices[triangle + 2]);
                    auto normal = glm::cross(b - a, c - a);
                    if (glm::length(normal) < 1e-10) {
                        continue;
                    }
                    normal = glm::normalize(normal);
                    const auto center = (a + b + c) / 3.0;
                    for (const double side : {1.0, -1.0}) {
                        if (geometry.intersect(ray(center + normal * side * 2.0, -normal * side),
                                               instance, glm::mat4{1}, {}, 100)) {
                            hit = true;
                            break;
                        }
                    }
                }
                if (hit) {
                    break;
                }
            }
            if (hit) {
                break;
            }
        }
        if (!hit) {
            throw std::runtime_error(std::string{"supplied model could not be picked: "} + name);
        }
        std::cout << "Picked supplied model: " << name << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        rays();
        geometry();
        deformation();
        alphaCoverage();
        selection();
        require(argc == 2, "expected model directory");
        suppliedModels(argv[1]);
        std::cout << "PASS picking: DPI/Vulkan rays, clip bounds, triangles, mirrored scale, "
                     "imported hierarchy, shared instances, morph/skin/current pose, stable "
                     "selection lifecycle.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL picking: " << error.what() << '\n';
        return 1;
    }
}
