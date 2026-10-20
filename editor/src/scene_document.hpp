#pragma once

#include <grimlock/engine.hpp>

namespace grimlock::editor {

// Editing commands operate on Engine's real Scene; no parallel object graph.
class SceneDocument final {
public:
    explicit SceneDocument(Engine& engine);
    void newScene();
    void open(const std::filesystem::path& path);
    void save(const std::filesystem::path& path);
    [[nodiscard]] ObjectId createGroup();
    [[nodiscard]] ObjectId groupObject(ObjectId object);

    // Editor moves preserve world space; Scene's lower-level API retains local TRS.
    void reparent(ObjectId object, std::optional<ObjectId> parent);
    [[nodiscard]] ObjectId addModel(const std::filesystem::path& model);
    [[nodiscard]] ObjectId duplicate(ObjectId source);
    void remove(ObjectId object);
    void rename(ObjectId object, const std::string& name);
    void setEnabled(ObjectId object, bool enabled);
    void setTransform(ObjectId object, Transform transform);
    void setAnimationConfiguration(ObjectId object, AnimationConfiguration configuration);
    void configureGrid(std::optional<GridConfiguration> configuration);
    bool setGridWalkable(GridCell cell, bool walkable);
    void observeCamera();
    void refreshModels();

    [[nodiscard]] bool dirty() const noexcept {
        return dirty_;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] const std::vector<std::filesystem::path>& models() const noexcept {
        return models_;
    }

    // GLM's XYZ Euler constructor and eulerAngles use the same convention.
    [[nodiscard]] static glm::vec3 rotationDegrees(const glm::quat& rotation);
    [[nodiscard]] static glm::quat rotationQuaternion(glm::vec3 degrees);

private:
    void rememberCamera();
    Engine& engine_;
    std::filesystem::path path_;
    std::vector<std::filesystem::path> models_;
    std::optional<Camera> camera_;
    bool dirty_ = false;
};

} // namespace grimlock::editor
