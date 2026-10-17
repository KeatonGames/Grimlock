#pragma once
#include "grimlock/assets/animation.hpp"
#include "grimlock/camera.hpp"
#include "grimlock/lighting.hpp"
#include "grimlock/transform.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace grimlock {
using ObjectId = std::uint64_t; // Nonzero, never reused within a Scene; serialized as decimal strings.
struct AnimationSettings {
    std::size_t clip = 0;
    float time = 0, speed = 1;
    bool loop = true, playing = true, paused = false, restPose = false;
};
struct SceneObject {
    ObjectId id = 0;
    std::string name;
    Transform local;
    std::optional<ObjectId> parent;
    std::optional<std::filesystem::path> model;
    bool enabled = true;
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
    std::array<std::optional<ObjectId>,maxPointLights> pointObjects{}, spotObjects{};
};

// CPU-only scene ownership. Access objects by ID rather than keeping references
// across deletion/replacement. Reparenting preserves local TRS; deletion cascades.
class Scene final {
public:
    explicit Scene(std::string name = "Untitled");
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name);
    [[nodiscard]] ObjectId createObject(std::string name = {}, std::optional<ObjectId> parent = {});
    void removeObject(ObjectId id);
    [[nodiscard]] bool contains(ObjectId id) const noexcept { return objects_.contains(id); }
    [[nodiscard]] const SceneObject& object(ObjectId id) const;
    [[nodiscard]] const std::map<ObjectId,SceneObject>& objects() const noexcept { return objects_; }
    void setName(ObjectId id, std::string name);
    void setTransform(ObjectId id, const Transform& local);
    void setParent(ObjectId id, std::optional<ObjectId> parent);
    void setEnabled(ObjectId id, bool enabled);
    void setModel(ObjectId id, std::optional<std::filesystem::path> model);
    void setTint(ObjectId id, const glm::vec4& tint);
    void setMaterialOverride(ObjectId id, std::optional<std::size_t> material);
    void setAnimation(ObjectId id, std::optional<AnimationSettings> settings);
    [[nodiscard]] assets::ModelInstance& animation(ObjectId id);
    [[nodiscard]] AnimationSettings animationSettings(ObjectId id) const;
    [[nodiscard]] glm::mat4 worldTransform(ObjectId id) const;
    [[nodiscard]] bool enabledInHierarchy(ObjectId id) const;
    void updateTransforms() const;
    void setCamera(std::string id, const SceneCamera& camera);
    void setActiveCamera(const std::string& id);
    [[nodiscard]] const std::map<std::string,SceneCamera>& cameras() const noexcept { return cameras_; }
    [[nodiscard]] const std::string& activeCamera() const noexcept { return activeCamera_; }
    [[nodiscard]] Camera resolvedCamera() const;
    void setLighting(const SceneLighting& lighting);
    [[nodiscard]] const SceneLighting& lighting() const noexcept { return lighting_; }
    [[nodiscard]] Lighting resolvedLighting() const;
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
    std::map<ObjectId,SceneObject> objects_;
    std::map<std::string,SceneCamera> cameras_;
    SceneLighting lighting_;
    ObjectId nextId_ = 1;
    mutable std::vector<ObjectId> scratch_;
};
}
