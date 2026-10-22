#pragma once

#include "grid_tools.hpp"
#include "scene_document.hpp"

#include <functional>
#include <grimlock/imgui_overlay.hpp>

struct ImGuiContext;

namespace grimlock::editor {

class Editor final : public ui::ImGuiOverlay {
public:
    explicit Editor(Engine& engine, const std::filesystem::path& initialScene = {}, GridDebug* gridDebug = nullptr);
    ~Editor() override;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    [[nodiscard]] SceneDocument& document() noexcept {
        return document_;
    }

    OverlayInput frame() override;
    void finishFrame(const Camera& camera) override;
    bool requestClose() override;

private:
    enum class Operation {
        none,
        newScene,
        open,
        close
    };

    enum class FileMode {
        none,
        open,
        save
    };

    void configureUi() override;
    void releaseUi() noexcept override;
    void layout();
    void menu();

    void hierarchy();
    void objectRow(ObjectId id);
    void objectMenu(ObjectId id);
    void dropTarget(std::optional<ObjectId> parent);
    void beginRename(ObjectId id);
    void requestDelete(ObjectId id);

    void inspector();
    void animationControls(ObjectId id, bool reveal);
    void performance();

    void dialogs();
    void deleteDialog();
    void sceneFileDialog();
    void sceneFileList();
    void unsavedChangesDialog();
    void errorDialog();
    [[nodiscard]] bool saveSceneFromDialog(const std::filesystem::path& filename);
    void attempt(const std::function<void()>& operation);
    void request(Operation operation, std::filesystem::path path = {});
    void executePending();
    void save();
    void fileDialog(FileMode mode);

    Engine& engine_;
    SceneDocument document_;
    GridTools gridTools_;
    GridDebug* gridDebug_ = nullptr;
    bool viewportMouse_ = false;

    bool layoutReady_ = false;

    bool showPerformance_ = false, close_ = false, confirm_ = false;
    bool openFilePopup_ = false, openError_ = false, resumeAfterSave_ = false;

    Operation pending_ = Operation::none;
    FileMode fileMode_ = FileMode::none;
    std::filesystem::path pendingPath_;
    std::string filePath_, error_, dialogError_;
    std::size_t modelIndex_ = 0;

    std::optional<ObjectId> rotationObject_;
    std::optional<ObjectId> hierarchySelection_;
    std::optional<ObjectId> inspectorSelection_;
    float inspectorHeight_ = 0;
    std::vector<ObjectId> revealParents_;

    // Tree edits run after traversal, never while iterating a child span.
    std::function<void()> hierarchyAction_;
    std::optional<ObjectId> renameObject_, deleteObject_;
    std::string renameText_;
    bool focusRename_ = false, openDelete_ = false;
    glm::vec3 rotation_{};
    glm::quat lastRotation_{1, 0, 0, 0};
};

} // namespace grimlock::editor
