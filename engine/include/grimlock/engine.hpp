#pragma once

#include "grimlock/camera.hpp"
#include "grimlock/lighting.hpp"
#include "grimlock/orbit_camera.hpp"
#include "grimlock/scene.hpp"
#include "grimlock/transform.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace grimlock {

class Scene;
class RenderOverlay;

namespace assets {

struct ModelData;
class ModelInstance;

} // namespace assets

struct EngineConfig {
    std::string windowTitle = "Grimlock";
    int width = 1280, height = 720;
    std::filesystem::path environmentFolder;
    std::filesystem::path assetRoot = "assets";
};

class ModelId final {
public:
    ModelId() = default;
    bool operator==(const ModelId&) const = default;

private:
    friend class Engine;

    explicit ModelId(std::size_t index) : index_(index) {
    }

    std::size_t index_ = static_cast<std::size_t>(-1);
};

class InstanceId final {
public:
    InstanceId() = default;
    bool operator==(const InstanceId&) const = default;

private:
    friend class Engine;

    explicit InstanceId(std::size_t index) : index_(index) {
    }

    std::size_t index_ = static_cast<std::size_t>(-1);
};

struct ModelInfo {
    glm::vec3 minimum{}, maximum{};
    std::size_t primitives = 0, materials = 0;
    std::vector<std::string> animationNames;
};

// Owns one window/device and its resources. Game policy (assets, placement,
// normalization and default animation selection) stays outside this class.
class Engine final {
public:
    explicit Engine(const EngineConfig& config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Relative paths resolve beside the executable; canonical paths reuse one resource.
    [[nodiscard]] ModelId loadModel(const std::filesystem::path& path);
    [[nodiscard]] ModelId addModel(assets::ModelData model);
    [[nodiscard]] ModelInfo modelInfo(ModelId model) const;
    // Scene-visible runtime instances are independent of scene serialization and picking.
    [[nodiscard]] InstanceId createInstance(ModelId model, bool visibleInScenes = false);
    // Invalidates this instance ID; IDs are not reused.
    void removeInstance(InstanceId instance);
    void setTransform(InstanceId instance, const glm::mat4& transform);
    void setTransform(InstanceId instance, const Transform& transform);
    [[nodiscard]] glm::mat4 transform(InstanceId instance) const;
    void setTint(InstanceId instance, const glm::vec4& color);
    void setMaterialOverride(InstanceId instance, std::optional<std::size_t> material);

    // Playback controls are CPU-only; update() advances every instance automatically.
    [[nodiscard]] assets::ModelInstance& animation(InstanceId instance);
    [[nodiscard]] Camera& camera() noexcept;
    [[nodiscard]] Lighting& lighting() noexcept;

    // File paths are executable-relative. Scene model paths are asset-root-relative.
    [[nodiscard]] std::unique_ptr<Scene> loadScene(const std::filesystem::path& path) const;
    void saveScene(const std::filesystem::path& path) const;

    // Validates/loads the candidate before replacing the active scene.
    void setScene(std::unique_ptr<Scene> scene);
    [[nodiscard]] Scene* scene() noexcept;
    [[nodiscard]] const Scene* scene() const noexcept;
    void unloadScene();
    [[nodiscard]] const std::filesystem::path& assetRoot() const noexcept;

    // Load/validate pending model changes before the next update; errors reach the caller.
    void prepareScene(Scene& scene);
    void attachOverlay(RenderOverlay& overlay);
    void detachOverlay() noexcept;
    [[nodiscard]] const InputState& input() const noexcept;

    // Scene-only left-press picking. Disabling clears selection; navigation is independent.
    void setObjectSelectionEnabled(bool enabled = true);
    [[nodiscard]] bool objectSelectionEnabled() const noexcept;
    [[nodiscard]] std::optional<ObjectId> selectedObject() const;
    void clearSelection() noexcept;
    void setCameraNavigationEnabled(bool enabled = true);
    [[nodiscard]] bool cameraNavigationEnabled() const noexcept;
    [[nodiscard]] const OrbitCameraConfig& cameraNavigationConfig() const noexcept;
    void configureCameraNavigation(const OrbitCameraConfig& config);

    // Polls events, advances animations/lights and renders. False only when closing;
    // a minimized window still updates, but rendering is deferred.
    // Game updates run after viewport tools, before animation and draw preparation.
    [[nodiscard]] bool update(const std::function<void(double)>& gameUpdate = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace grimlock
