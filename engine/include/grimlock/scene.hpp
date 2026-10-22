#pragma once

#include "grimlock/assets/animation.hpp"
#include "grimlock/camera.hpp"
#include "grimlock/grid.hpp"
#include "grimlock/lighting.hpp"
#include "grimlock/transform.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace grimlock {

using ObjectId = std::uint64_t; // Nonzero, never reused within a Scene; serialized as decimal strings.

// Live playback snapshot / legacy initial-state input. Scene saves authored configuration instead.
struct AnimationSettings {
    std::size_t clip = 0;
    float time = 0, speed = 1;
    bool loop = true, playing = true, paused = false, restPose = false;
    std::optional<std::string> clipName; // Legacy initial-state records may still address clips by index.
};

// Authored configuration, separate from the advancing per-instance playback state.
struct AnimationConfiguration {
    std::size_t clip = 0;
    bool autoplay = false, loop = true;
    float speed = 1;
    std::optional<std::string> clipName; // When supplied, a unique imported name takes precedence over clip.
};

struct SceneObject {
    ObjectId id = 0;
    std::string name;
    std::string role; // Opaque application classification; Engine does not interpret it.
    Transform local;
    std::optional<ObjectId> parent;
    std::optional<std::filesystem::path> model;
    bool enabled = true;
    bool selectable = true; // Runtime policy only; intentionally not serialized.
    glm::vec4 tint{1};
    std::optional<std::size_t> materialOverride;
    std::optional<AnimationSettings> initialAnimation;

private:
    friend class Scene;
    friend class Engine;
    std::unique_ptr<assets::ModelInstance> instance_;
    std::vector<ObjectId> children_;
    mutable glm::mat4 world_{1};
    mutable bool dirty_ = true, visibilityDirty_ = true, visible_ = true;
};

struct SceneCamera {
    Camera camera;
    std::optional<ObjectId> object; // Camera vectors are local to this object if attached.
};

struct SceneLighting {
    Lighting values;
    std::optional<ObjectId> directionalObject;
    std::array<std::optional<ObjectId>, maxPointLights> pointObjects{}, spotObjects{};
};

// CPU-only scene ownership. Access objects by ID rather than keeping references
// across deletion/replacement. Reparenting preserves local TRS; deletion cascades.
class Scene final {
public:
    explicit Scene(std::string name = "Untitled");
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }

    void setName(std::string name);
    [[nodiscard]] ObjectId createObject(std::string name = {}, std::optional<ObjectId> parent = {});
    void removeObject(ObjectId id);

    [[nodiscard]] bool contains(ObjectId id) const noexcept {
        return objects_.contains(id);
    }

    [[nodiscard]] const SceneObject& object(ObjectId id) const;

    [[nodiscard]] const std::map<ObjectId, SceneObject>& objects() const noexcept {
        return objects_;
    }

    [[nodiscard]] std::span<const ObjectId> children(ObjectId id) const {
        return object(id).children_;
    }

    void setName(ObjectId id, std::string name);
    void setRole(ObjectId id, std::string role);
    void setTransform(ObjectId id, const Transform& local);
    void setParent(ObjectId id, std::optional<ObjectId> parent);
    void setEnabled(ObjectId id, bool enabled);
    void setSelectable(ObjectId id, bool selectable);

    // Including inactive/empty objects is useful for tools; mouse picking keeps the default policy.
    void selectObject(std::optional<ObjectId> id, bool includeInactive = false);
    [[nodiscard]] std::optional<ObjectId> selectedObject() const;

    void clearSelection() noexcept {
        selected_.reset();
        selectionIncludesInactive_ = false;
    }

    void setModel(ObjectId id, std::optional<std::filesystem::path> model);
    void setTint(ObjectId id, const glm::vec4& tint);
    void setMaterialOverride(ObjectId id, std::optional<std::size_t> material);

    // Legacy initial-state input, also usable before model binding; retained for existing
    // scenes/code.
    void setAnimation(ObjectId id, std::optional<AnimationSettings> settings);
    [[nodiscard]] assets::ModelInstance& animation(ObjectId id);
    [[nodiscard]] AnimationSettings animationSettings(ObjectId id) const; // Current playback, not saved configuration.

    // Read-only shared clips. The span remains valid while this object's model is bound.
    [[nodiscard]] std::span<const assets::AnimationData> availableAnimations(ObjectId id) const;

    // A missing named clip resolves to clip == availableAnimations(id).size(); it stays at rest.
    [[nodiscard]] AnimationConfiguration animationConfiguration(ObjectId id) const;
    void setAnimationConfiguration(ObjectId id, AnimationConfiguration configuration);

    // Runtime/preview controls never change authored configuration. play restarts; resume retains
    // time.
    void playAnimation(ObjectId id, std::size_t clip, bool loop = true);
    void playAnimation(ObjectId id, std::string_view clipName, bool loop = true);
    void pauseAnimation(ObjectId id);
    void resumeAnimation(ObjectId id);
    void stopAnimation(ObjectId id);
    void setAnimationSpeed(ObjectId id, float speed);
    [[nodiscard]] glm::mat4 worldTransform(ObjectId id) const;
    [[nodiscard]] bool enabledInHierarchy(ObjectId id) const;
    void updateTransforms() const;
    void setCamera(std::string id, const SceneCamera& camera);
    void setActiveCamera(const std::string& id);

    [[nodiscard]] const std::map<std::string, SceneCamera>& cameras() const noexcept {
        return cameras_;
    }

    [[nodiscard]] const std::string& activeCamera() const noexcept {
        return activeCamera_;
    }

    [[nodiscard]] Camera resolvedCamera() const;

    // Writes a world-space view into the selected camera, retaining its attachment.
    void setResolvedCamera(const Camera& camera);
    void setLighting(const SceneLighting& lighting);

    [[nodiscard]] const SceneLighting& lighting() const noexcept {
        return lighting_;
    }

    [[nodiscard]] Lighting resolvedLighting() const;

    // One optional generic grid. Game authors configuration/rules; rendering never mutates it.
    [[nodiscard]] Grid* navigationGrid() noexcept {
        return navigationGrid_ ? &*navigationGrid_ : nullptr;
    }

    [[nodiscard]] const Grid* navigationGrid() const noexcept {
        return navigationGrid_ ? &*navigationGrid_ : nullptr;
    }

    void setNavigationGrid(std::optional<GridConfiguration> configuration);

    // Native filesystem paths; Engine's wrappers add executable-relative resolution.
    [[nodiscard]] static std::unique_ptr<Scene> load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

private:
    friend class Engine;
    void invalidate(ObjectId id, bool transforms = true);
    void bindModel(ObjectId id, std::shared_ptr<const assets::ModelData> model);
    void updateAnimations(double delta);
    void applyAnimation(SceneObject& object);
    void insertObject(ObjectId id, std::string name);
    std::string name_, activeCamera_ = "main";
    std::filesystem::path sourcePath_;
    std::map<ObjectId, SceneObject> objects_;
    std::map<std::string, SceneCamera> cameras_;
    SceneLighting lighting_;
    std::optional<Grid> navigationGrid_;
    ObjectId nextId_ = 1;
    mutable std::optional<ObjectId> selected_;
    bool selectionIncludesInactive_ = false;
    mutable std::vector<ObjectId> scratch_;
};

} // namespace grimlock
