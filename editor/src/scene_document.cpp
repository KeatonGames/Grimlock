#include "scene_document.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace grimlock::editor {

namespace {

std::string text(const std::filesystem::path& path) {
    const auto value = path.u8string();

    return {value.begin(), value.end()};
}

bool sameCamera(const Camera& a, const Camera& b) {
    return a.position == b.position && a.target == b.target && a.up == b.up &&
           a.verticalFovDegrees == b.verticalFovDegrees && a.nearPlane == b.nearPlane && a.farPlane == b.farPlane;
}

Transform localTransform(const glm::dmat4& matrix) {
    Transform result;
    result.position = glm::vec3{matrix[3]};
    glm::dmat3 basis{matrix};
    glm::dvec3 scale;

    for (int axis = 0; axis < 3; ++axis) {
        scale[axis] = glm::length(basis[axis]);

        if (!std::isfinite(scale[axis]) || scale[axis] <= 1e-8) {
            throw std::invalid_argument("Cannot preserve this object's transform: invalid scale.");
        }

        basis[axis] /= scale[axis];
    }

    // Rotated non-uniform parent scales can require shear. Never silently discard it.
    if (std::abs(glm::dot(basis[0], basis[1])) > 2e-5 || std::abs(glm::dot(basis[0], basis[2])) > 2e-5 ||
        std::abs(glm::dot(basis[1], basis[2])) > 2e-5) {
        throw std::invalid_argument("This move requires shear, which scene transforms cannot store. Use uniform group "
                                    "scales or align the rotations, then try again. Nothing was moved.");
    }

    if (glm::determinant(basis) < 0) {
        basis[0] = -basis[0];
        scale.x = -scale.x;
    }

    result.scale = glm::vec3{scale};
    result.rotation = glm::normalize(glm::quat{glm::quat_cast(basis)});
    const glm::dmat4 rebuilt{result.matrix()};

    for (int column = 0; column < 4; ++column) {
        const auto difference = glm::abs(rebuilt[column] - matrix[column]);
        const double tolerance = 2e-5 * std::max(1.0, glm::length(matrix[column]));

        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(rebuilt[column][row]) || difference[row] > tolerance) {
                throw std::invalid_argument("Cannot preserve this object's transform accurately. Nothing was moved.");
            }
        }
    }

    return result;
}

} // namespace

SceneDocument::SceneDocument(Engine& engine) : engine_(engine) {
    rememberCamera();
}

void SceneDocument::configureGrid(std::optional<GridConfiguration> configuration) {
    auto& scene = *engine_.scene();
    const auto* before = scene.navigationGrid();
    const bool existed = before != nullptr;
    const auto revision = before ? before->revision() : 0;
    scene.setNavigationGrid(configuration);
    const auto* after = scene.navigationGrid();

    if (existed != (after != nullptr) || (after && after->revision() != revision)) {
        dirty_ = true;
    }
}

bool SceneDocument::setGridWalkable(GridCell cell, bool walkable) {
    auto* grid = engine_.scene()->navigationGrid();

    if (!grid) {
        throw std::logic_error("Configure a navigation grid first.");
    }

    if (!grid->setWalkable(cell, walkable)) {
        return false;
    }

    dirty_ = true;

    return true;
}

void SceneDocument::rememberCamera() {
    camera_ = engine_.scene() ? std::optional{engine_.scene()->resolvedCamera()} : std::nullopt;
}

void SceneDocument::observeCamera() {
    if (!engine_.scene()) {
        camera_.reset();

        return;
    }

    const auto value = engine_.scene()->resolvedCamera();

    if (camera_ && !sameCamera(*camera_, value)) {
        dirty_ = true;
    }

    camera_ = value;
}

void SceneDocument::newScene() {
    engine_.setScene(std::make_unique<Scene>());
    path_.clear();
    dirty_ = false;
    rememberCamera();
}

void SceneDocument::open(const std::filesystem::path& path) {
    auto preparedPath = path;
    engine_.setScene(engine_.loadScene(preparedPath)); // Candidate activation is transactional.
    path_ = std::move(preparedPath);
    dirty_ = false;
    rememberCamera();
}

void SceneDocument::save(const std::filesystem::path& path) {
    if (path.empty() || path.extension() != ".json") {
        throw std::invalid_argument("Choose a .json scene filename.");
    }

    auto preparedPath = path;
    engine_.saveScene(preparedPath);
    path_ = std::move(preparedPath);
    dirty_ = false;
    rememberCamera();
}

ObjectId SceneDocument::createGroup() {
    auto& scene = *engine_.scene();
    const auto id = scene.createObject("Group");
    scene.selectObject(id, true);
    dirty_ = true;

    return id;
}

void SceneDocument::reparent(ObjectId id, std::optional<ObjectId> parent) {
    auto& scene = *engine_.scene();
    const auto previousParent = scene.object(id).parent;

    if (parent) {
        if (scene.object(*parent).model) {
            throw std::invalid_argument("Drop onto a group, not a model object.");
        }

        for (auto ancestor = parent; ancestor; ancestor = scene.object(*ancestor).parent) {
            if (*ancestor == id) {
                throw std::invalid_argument("A group cannot be moved into itself or one of its children.");
            }
        }
    }

    if (previousParent == parent) {
        return;
    }

    const auto previousTransform = scene.object(id).local;
    const auto world = glm::dmat4{scene.worldTransform(id)};
    const auto parentWorld = parent ? glm::dmat4{scene.worldTransform(*parent)} : glm::dmat4{1};
    const auto local = localTransform(glm::inverse(parentWorld) * world);

    // Check float composition too: large parent translations/scales can amplify rounding.
    const glm::dmat4 composed{glm::mat4{parentWorld} * local.matrix()};

    for (int column = 0; column < 4; ++column) {
        const auto difference = glm::abs(composed[column] - world[column]);
        const double tolerance = 2e-5 * std::max(1.0, glm::length(world[column]));

        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(composed[column][row]) || difference[row] > tolerance) {
                throw std::invalid_argument("This move would lose world-transform precision. Nothing was moved.");
            }
        }
    }

    try {
        scene.setParent(id, parent);
        scene.setTransform(id, local);
        engine_.prepareScene(scene);
    } catch (...) {
        scene.setParent(id, previousParent);
        scene.setTransform(id, previousTransform);
        throw;
    }

    scene.selectObject(id, true);
    dirty_ = true;
}

ObjectId SceneDocument::groupObject(ObjectId object) {
    auto& scene = *engine_.scene();
    const auto parent = scene.object(object).parent;
    const auto group = scene.createObject("Group", parent);

    try {
        reparent(object, group);
    } catch (...) {
        scene.removeObject(group);
        throw;
    }

    scene.selectObject(group, true);
    dirty_ = true;

    return group;
}

ObjectId SceneDocument::addModel(const std::filesystem::path& model) {
    auto& scene = *engine_.scene();
    const auto id = scene.createObject(text(model.stem()));

    try {
        scene.setModel(id, model);
        engine_.prepareScene(scene);
        const auto& data = scene.animation(id).model();
        const auto size = data.maximum - data.minimum;
        const float largest = std::max({size.x, size.y, size.z});

        if (!std::isfinite(largest) || largest <= 1e-8F) {
            throw std::invalid_argument("Model has invalid placement bounds.");
        }

        // New instances start grounded, centered at the view target, and about two units wide/tall.
        const float scale = 2 / largest;
        const auto center = (data.minimum + data.maximum) * 0.5F;
        const auto target = scene.resolvedCamera().target;
        Transform transform;
        transform.scale = glm::vec3{scale};
        transform.position = {target.x - center.x * scale, -data.minimum.y * scale, target.z - center.z * scale};
        scene.setTransform(id, transform);
        engine_.prepareScene(scene);
        scene.selectObject(id, true);
    } catch (...) {
        scene.removeObject(id);
        throw;
    }

    dirty_ = true;

    return id;
}

ObjectId SceneDocument::duplicate(ObjectId source) {
    auto& scene = *engine_.scene();
    const auto& original = scene.object(source);
    const auto id = scene.createObject(original.name + " Copy", original.parent);

    try {
        scene.setTransform(id, original.local);
        scene.setEnabled(id, original.enabled);
        scene.setSelectable(id, original.selectable);
        scene.setTint(id, original.tint);
        scene.setModel(id, original.model);
        scene.setMaterialOverride(id, original.materialOverride);

        if (original.model) {
            scene.setAnimation(id, original.initialAnimation);
        }

        engine_.prepareScene(scene);
        scene.selectObject(id, true);
    } catch (...) {
        scene.removeObject(id);
        throw;
    }

    dirty_ = true;

    return id;
}

void SceneDocument::remove(ObjectId object) {
    engine_.scene()->removeObject(object);
    dirty_ = true;
}

void SceneDocument::rename(ObjectId object, const std::string& name) {
    auto& scene = *engine_.scene();

    if (scene.object(object).name == name) {
        return;
    }

    scene.setName(object, name);
    dirty_ = true;
}

void SceneDocument::setEnabled(ObjectId object, bool enabled) {
    auto& scene = *engine_.scene();

    if (scene.object(object).enabled == enabled) {
        return;
    }

    const bool previous = scene.object(object).enabled;
    scene.selectObject(object, true);

    try {
        scene.setEnabled(object, enabled);
        engine_.prepareScene(scene);
    } catch (...) {
        scene.setEnabled(object, previous);
        throw;
    }

    dirty_ = true;
}

void SceneDocument::setAnimationConfiguration(ObjectId object, AnimationConfiguration configuration) {
    auto& scene = *engine_.scene();
    const auto previous = scene.animationConfiguration(object);
    scene.setAnimationConfiguration(object, std::move(configuration));
    const auto current = scene.animationConfiguration(object);

    if (previous.clip != current.clip || previous.clipName != current.clipName ||
        previous.autoplay != current.autoplay || previous.loop != current.loop || previous.speed != current.speed) {
        dirty_ = true;
    }
}

glm::vec3 SceneDocument::rotationDegrees(const glm::quat& rotation) {
    return glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
}

glm::quat SceneDocument::rotationQuaternion(glm::vec3 degrees) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(degrees[axis])) {
            throw std::invalid_argument("Rotation must be finite.");
        }

        degrees[axis] = std::remainder(degrees[axis], 360.0F);
    }

    return glm::normalize(glm::quat{glm::radians(degrees)});
}

void SceneDocument::setTransform(ObjectId object, Transform transform) {
    auto& scene = *engine_.scene();
    const auto original = scene.object(object).local;

    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(transform.position[axis]) || !std::isfinite(transform.scale[axis])) {
            throw std::invalid_argument("Position and scale must be finite.");
        }

        if (std::abs(transform.scale[axis]) < 1e-4F) {
            transform.scale[axis] =
                std::copysign(1e-4F, transform.scale[axis] == 0 ? original.scale[axis] : transform.scale[axis]);
        }
    }

    try {
        scene.setTransform(object, transform);
        engine_.prepareScene(scene);
    } catch (...) {
        scene.setTransform(object, original);
        throw;
    }

    if (scene.object(object).local.matrix() != original.matrix()) {
        dirty_ = true;
    }
}

void SceneDocument::refreshModels() {
    std::vector<std::filesystem::path> result;
    const auto directory = engine_.assetRoot() / "models";
    std::error_code error;

    if (std::filesystem::exists(directory, error)) {
        for (std::filesystem::recursive_directory_iterator
                 entry{directory, std::filesystem::directory_options::skip_permission_denied, error},
             end;
             entry != end;
             entry.increment(error)) {
            if (error) {
                throw std::filesystem::filesystem_error("Cannot list models", directory, error);
            }

            if (!entry->is_regular_file(error)) {
                continue;
            }

            auto extension = entry->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (extension == ".glb" || extension == ".gltf" || extension == ".obj") {
                result.push_back(entry->path().lexically_relative(engine_.assetRoot()));
            }
        }
    }

    if (error) {
        throw std::filesystem::filesystem_error("Cannot list models", directory, error);
    }

    std::sort(result.begin(), result.end());
    models_ = std::move(result);
}

} // namespace grimlock::editor
