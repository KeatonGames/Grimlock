#pragma once
#include "scene_document.hpp"
#include <grimlock/render_overlay.hpp>
#include <functional>
struct ImGuiContext;

namespace grimlock::editor {
class Editor final : public RenderOverlay {
public:
    explicit Editor(Engine& engine, const std::filesystem::path& initialScene = {});
    ~Editor() override;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    [[nodiscard]] SceneDocument& document() noexcept { return document_; }
    void initialize(const OverlayContext& context) override;
    void shutdown() noexcept override;
    void processEvent(const SDL_Event& event) override;
    OverlayInput frame() override;
    void render(VkCommandBuffer command) override;
    void targetChanged(const OverlayTarget& target) override;
    bool requestClose() override;
private:
    enum class Operation { none, newScene, open, close };
    enum class FileMode { none, open, save };
    void initializeRenderer();
    void layout();
    void menu();
    void hierarchy();
    void objectRow(ObjectId id);
    void objectMenu(ObjectId id);
    void dropTarget(std::optional<ObjectId> parent);
    void beginRename(ObjectId id);
    void requestDelete(ObjectId id);
    void inspector();
    void animationControls(ObjectId id);
    void performance();
    void dialogs();
    void attempt(const std::function<void()>& operation);
    void request(Operation operation, std::filesystem::path path = {});
    void executePending();
    void save();
    void fileDialog(FileMode mode);
    Engine& engine_;
    SceneDocument document_;
    OverlayContext native_{};
    ImGuiContext* context_ = nullptr;
    bool platformReady_=false,rendererReady_=false,layoutReady_=false;
    bool showPerformance_=false,close_=false,confirm_=false;
    bool openFilePopup_=false,openError_=false,resumeAfterSave_=false;
    Operation pending_=Operation::none;
    FileMode fileMode_=FileMode::none;
    std::filesystem::path pendingPath_;
    std::string filePath_,error_,dialogError_;
    std::size_t modelIndex_=0;
    std::optional<ObjectId> rotationObject_;
    std::optional<ObjectId> hierarchySelection_;
    std::vector<ObjectId> revealParents_;
    // Tree edits run after traversal, never while iterating a child span.
    std::function<void()> hierarchyAction_;
    std::optional<ObjectId> renameObject_,deleteObject_;
    std::string renameText_;
    bool focusRename_=false,openDelete_=false;
    glm::vec3 rotation_{};
    glm::quat lastRotation_{1,0,0,0};
};
}
