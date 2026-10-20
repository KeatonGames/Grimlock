#include "editor.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <grimlock/editor.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h> // DockBuilder is used only for the initial pinned-version layout.
#include <imgui_stdlib.h>
#include <stdexcept>
#include <utility>

namespace grimlock::editor {

namespace {

std::string text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();

    return {value.begin(), value.end()};
}

std::filesystem::path path(const std::string& value) {
    return std::filesystem::path{std::u8string{value.begin(), value.end()}};
}

void check(VkResult result) {
    if (result < 0) {
        throw std::runtime_error("Dear ImGui Vulkan backend failed (VkResult " + std::to_string(result) + ").");
    }
}

void theme() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.FontSizeBase = 15;

    ImFontConfig font;
    font.SizePixels = 15;
    ImGui::GetIO().Fonts->AddFontDefaultVector(&font); // Embedded scalable font: portable, no system font paths.

    style.WindowPadding = {12, 10};
    style.FramePadding = {8, 4};
    style.ItemSpacing = {8, 7};
    style.ItemInnerSpacing = {5, 4};
    style.CellPadding = {4, 4};
    style.IndentSpacing = 17;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 9;

    style.WindowRounding = 5;
    style.ChildRounding = 4;
    style.FrameRounding = 4;
    style.PopupRounding = 5;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 3;
    style.TabRounding = 4;

    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;

    auto* colors = style.Colors;
    colors[ImGuiCol_Text] = {0.88F, 0.90F, 0.93F, 1};
    colors[ImGuiCol_TextDisabled] = {0.52F, 0.57F, 0.64F, 1};
    colors[ImGuiCol_WindowBg] = {0.105F, 0.12F, 0.145F, 1};
    colors[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    colors[ImGuiCol_PopupBg] = {0.14F, 0.16F, 0.19F, 1};
    colors[ImGuiCol_Border] = {0.25F, 0.29F, 0.35F, 0.6F};
    colors[ImGuiCol_FrameBg] = {0.17F, 0.20F, 0.24F, 1};
    colors[ImGuiCol_FrameBgHovered] = {0.22F, 0.28F, 0.34F, 1};
    colors[ImGuiCol_FrameBgActive] = {0.25F, 0.33F, 0.41F, 1};
    colors[ImGuiCol_TitleBg] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TitleBgActive] = {0.16F, 0.20F, 0.25F, 1};
    colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_MenuBarBg] = {0.12F, 0.14F, 0.17F, 1};
    colors[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};

    colors[ImGuiCol_ScrollbarGrab] = {0.28F, 0.32F, 0.38F, 1};
    colors[ImGuiCol_ScrollbarGrabHovered] = {0.36F, 0.42F, 0.49F, 1};
    colors[ImGuiCol_ScrollbarGrabActive] = {0.42F, 0.49F, 0.57F, 1};
    colors[ImGuiCol_CheckMark] = {0.53F, 0.73F, 0.83F, 1};
    colors[ImGuiCol_SliderGrab] = colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_SliderGrabActive] = {0.67F, 0.83F, 0.90F, 1};

    colors[ImGuiCol_Button] = {0.20F, 0.26F, 0.32F, 1};
    colors[ImGuiCol_ButtonHovered] = {0.28F, 0.37F, 0.45F, 1};
    colors[ImGuiCol_ButtonActive] = {0.32F, 0.44F, 0.53F, 1};
    colors[ImGuiCol_Header] = {0.22F, 0.34F, 0.43F, 1};
    colors[ImGuiCol_HeaderHovered] = {0.23F, 0.30F, 0.37F, 1};
    colors[ImGuiCol_HeaderActive] = {0.29F, 0.41F, 0.50F, 1};

    colors[ImGuiCol_Separator] = {0.23F, 0.27F, 0.33F, 1};
    colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_ButtonActive];

    colors[ImGuiCol_Tab] = {0.14F, 0.17F, 0.21F, 1};
    colors[ImGuiCol_TabHovered] = colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_TabSelected] = {0.20F, 0.27F, 0.34F, 1};
    colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_TabDimmed] = colors[ImGuiCol_Tab];
    colors[ImGuiCol_TabDimmedSelected] = colors[ImGuiCol_TabSelected];
    colors[ImGuiCol_TabDimmedSelectedOverline] = colors[ImGuiCol_Separator];

    colors[ImGuiCol_TextSelectedBg] = {0.29F, 0.46F, 0.59F, 0.65F};
    colors[ImGuiCol_NavCursor] = colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_DockingPreview] = {0.42F, 0.64F, 0.76F, 0.35F};
    colors[ImGuiCol_DragDropTarget] = colors[ImGuiCol_CheckMark];

    // Theme values are sRGB; the existing sRGB swapchain encodes linear shader output.
    const auto linear = [](float value) {
        return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
    };

    for (auto& color : style.Colors) {
        color = {linear(color.x), linear(color.y), linear(color.z), color.w};
    }
}

bool vectorControl(const char* label, glm::vec3& value, float speed) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const char* axes[]{"X", "Y", "Z"};

    // Linear colors for the sRGB attachment, with readable, subdued axis accents.
    const ImVec4 accents[]{{0.65F, 0.23F, 0.22F, 1}, {0.24F, 0.52F, 0.32F, 1}, {0.25F, 0.43F, 0.68F, 1}};

    if (ImGui::BeginTable("##axes", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::TableNextColumn();
            ImGui::PushID(axis);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(accents[axis], "%s", axes[axis]);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(-1);
            changed = ImGui::DragFloat("##value", &value[axis], speed, 0, 0, "%.3f") || changed;
            ImGui::SetItemTooltip("%s %s: drag or Ctrl+click to enter a number", label, axes[axis]);
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopID();

    return changed;
}

} // namespace

Editor::Editor(Engine& engine, const std::filesystem::path& initialScene) : engine_(engine), document_(engine) {
    if (!engine_.scene()) {
        document_.newScene();
    }

    attempt([&] {
        document_.refreshModels();
    });

    if (!initialScene.empty()) {
        attempt([&] {
            document_.open(initialScene);
        });
    }

    engine_.setCameraNavigationEnabled();
    engine_.setObjectSelectionEnabled();
    engine_.attachOverlay(*this);
}

Editor::~Editor() {
    engine_.detachOverlay();
}

void Editor::initialize(const OverlayContext& context) {
    native_ = context;
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr; // Session layout; no source-tree or user-profile artifacts.
    theme();

    if (!ImGui_ImplSDL3_InitForVulkan(native_.window)) {
        throw std::runtime_error("Dear ImGui SDL3 initialization failed.");
    }

    platformReady_ = true;
    initializeRenderer();
}

void Editor::initializeRenderer() {
    if (native_.target.images < native_.target.minimumImages) {
        throw std::runtime_error("Editor requires at least two swapchain images.");
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = native_.instance;
    info.PhysicalDevice = native_.physicalDevice;
    info.Device = native_.device;
    info.QueueFamily = native_.queueFamily;
    info.Queue = native_.queue;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = native_.target.minimumImages;
    info.ImageCount = native_.target.images;
    info.UseDynamicRendering = true;
    info.MinAllocationSize = 1024 * 1024;
    info.CheckVkResultFn = check;
    auto& pipeline = info.PipelineInfoMain;
    pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    pipeline.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipeline.PipelineRenderingCreateInfo.pColorAttachmentFormats = &native_.target.colorFormat;

    // If initialization throws after allocating backend state, shutdown must still clean it.
    try {
        if (!ImGui_ImplVulkan_Init(&info)) {
            throw std::runtime_error("Dear ImGui Vulkan initialization failed.");
        }

        rendererReady_ = true;
    } catch (...) {
        rendererReady_ = ImGui::GetIO().BackendRendererUserData != nullptr;
        throw;
    }
}

void Editor::shutdown() noexcept {
    if (!context_) {
        return;
    }

    ImGui::SetCurrentContext(context_);

    if (rendererReady_) {
        ImGui_ImplVulkan_Shutdown();
        rendererReady_ = false;
    }

    if (platformReady_) {
        ImGui_ImplSDL3_Shutdown();
        platformReady_ = false;
    }

    ImGui::DestroyContext(context_);
    context_ = nullptr;
}

void Editor::targetChanged(const OverlayTarget& target) {
    if (native_.target.colorFormat == target.colorFormat && native_.target.images == target.images &&
        native_.target.minimumImages == target.minimumImages) {
        return;
    }

    ImGui::SetCurrentContext(context_);
    ImGui_ImplVulkan_Shutdown();
    rendererReady_ = false;
    native_.target = target;
    initializeRenderer(); // CPU context, panels and document survive.
}

void Editor::processEvent(const SDL_Event& event) {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool Editor::requestClose() {
    request(Operation::close);

    return close_;
}

void Editor::attempt(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const std::exception& error) {
        error_ = error.what();
        openError_ = true;
    }
}

void Editor::request(Operation operation, std::filesystem::path filename) {
    if (pending_ != Operation::none) {
        return;
    }

    document_.observeCamera();
    pending_ = operation;
    pendingPath_ = std::move(filename);
    dialogError_.clear();

    if (document_.dirty()) {
        confirm_ = true;
    } else {
        executePending();
    }
}

void Editor::executePending() {
    const auto operation = pending_;
    auto filename = std::move(pendingPath_);
    pending_ = Operation::none;
    confirm_ = false;
    attempt([&] {
        if (operation == Operation::newScene) {
            document_.newScene();
        } else if (operation == Operation::open) {
            document_.open(filename);
        } else if (operation == Operation::close) {
            close_ = true;
        }

        if (operation == Operation::newScene || operation == Operation::open) {
            gridTools_.resetInteraction();
            renameObject_.reset();
            deleteObject_.reset();
            hierarchySelection_.reset();
            rotationObject_.reset();
            inspectorSelection_.reset();
        }
    });
}

void Editor::fileDialog(FileMode mode) {
    fileMode_ = mode;
    filePath_ =
        document_.path().empty() ? text(engine_.assetRoot() / "scenes" / "untitled.json") : text(document_.path());
    dialogError_.clear();
    openFilePopup_ = true;
}

void Editor::save() {
    if (document_.path().empty()) {
        fileDialog(FileMode::save);
    } else {
        attempt([&] {
            document_.save(document_.path());
        });
    }
}

void Editor::layout() {
    const auto dock = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (layoutReady_) {
        return;
    }

    layoutReady_ = true;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock,
                              static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
                                  ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
    auto center = dock;
    ImGuiID left = 0, right = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.21F, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.29F, &right, &center);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderFinish(dock);
}

void Editor::menu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene")) {
            request(Operation::newScene);
        }

        if (ImGui::MenuItem("Open Scene...")) {
            fileDialog(FileMode::open);
        }

        if (ImGui::MenuItem("Save Scene")) {
            save();
        }

        if (ImGui::MenuItem("Save Scene As...")) {
            fileDialog(FileMode::save);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit")) {
            request(Operation::close);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Performance", nullptr, &showPerformance_);
        ImGui::EndMenu();
    }

    gridTools_.menu(*engine_.scene());
    ImGui::Separator();
    ImGui::Text("%s%s", engine_.scene()->name().c_str(), document_.dirty() ? " *" : "");

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", document_.path().empty() ? "Unsaved scene" : text(document_.path()).c_str());
    }

    ImGui::EndMainMenuBar();
}

void Editor::beginRename(ObjectId id) {
    renameObject_ = id;
    renameText_ = engine_.scene()->object(id).name;
    focusRename_ = true;
    engine_.scene()->selectObject(id, true);
}

void Editor::requestDelete(ObjectId id) {
    deleteObject_ = id;
    openDelete_ = true;
}

void Editor::dropTarget(std::optional<ObjectId> parent) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }

    if (const auto* payload = ImGui::AcceptDragDropPayload("GRIMLOCK_OBJECT")) {
        if (payload->DataSize == sizeof(ObjectId)) {
            ObjectId id{};
            std::memcpy(&id, payload->Data, sizeof(id));
            hierarchyAction_ = [this, id, parent] {
                document_.reparent(id, parent);
                hierarchySelection_.reset();
            };
        }
    }

    ImGui::EndDragDropTarget();
}

void Editor::objectMenu(ObjectId id) {
    auto& scene = *engine_.scene();

    if (!ImGui::BeginPopupContextItem("Object Actions")) {
        return;
    }

    scene.selectObject(id, true);

    if (ImGui::MenuItem("Rename")) {
        beginRename(id);
    }

    if (ImGui::MenuItem("Group Selected Object")) {
        hierarchyAction_ = [this, id] {
            (void)document_.groupObject(id);
        };
    }

    if (ImGui::MenuItem("Move to Scene Root", nullptr, false, scene.object(id).parent.has_value())) {
        hierarchyAction_ = [this, id] {
            document_.reparent(id, {});
            hierarchySelection_.reset();
        };
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Duplicate Object")) {
        hierarchyAction_ = [this, id] {
            (void)document_.duplicate(id);
        };
    }

    if (ImGui::MenuItem(scene.object(id).model ? "Delete Object..." : "Delete Group...")) {
        requestDelete(id);
    }

    ImGui::EndPopup();
}

void Editor::objectRow(ObjectId id) {
    auto& scene = *engine_.scene();
    const auto& object = scene.object(id);
    const auto key = std::to_string(id);
    ImGui::PushID(key.c_str());
    bool enabled = object.enabled;

    if (ImGui::Checkbox("##visible", &enabled)) {
        attempt([&] {
            document_.setEnabled(id, enabled);
        });
    }

    ImGui::SetItemTooltip("Show or hide this object and its children");
    ImGui::SameLine();
    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    const bool group = !object.model;
    const bool leaf = scene.children(id).empty();

    if (leaf) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (leaf && !group) {
        flags |= ImGuiTreeNodeFlags_Bullet;
    }

    if (scene.selectedObject() == id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (std::find(revealParents_.begin(), revealParents_.end(), id) != revealParents_.end()) {
        ImGui::SetNextItemOpen(true);
    }

    if (!scene.enabledInHierarchy(id)) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.55F);
    }

    if (group) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    }

    const bool open =
        ImGui::TreeNodeEx("##object",
                          flags,
                          "%s%s",
                          group ? "[] " : "",
                          renameObject_ == id ? "" : (object.name.empty() ? "(unnamed)" : object.name.c_str()));
    if (group) {
        ImGui::PopStyleColor();
    }

    if (!scene.enabledInHierarchy(id)) {
        ImGui::PopStyleVar();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        scene.selectObject(id, true);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        beginRename(id);
    }

    ImGui::SetItemTooltip("%s\nDouble-click to rename. Right-click for actions.", group ? "Group" : "Model");

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("GRIMLOCK_OBJECT", &id, sizeof(id));
        ImGui::TextUnformatted(object.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (group) {
        dropTarget(id);
    }

    objectMenu(id);

    if (renameObject_ == id) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }

        const bool commit = ImGui::InputText("##rename",
                                             &renameText_,
                                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renameObject_.reset();
        } else if (commit || ImGui::IsItemDeactivatedAfterEdit()) {
            const auto name = renameText_;
            hierarchyAction_ = [this, id, name] {
                document_.rename(id, name);
            };

            renameObject_.reset();
        } else if (ImGui::IsItemDeactivated()) {
            renameObject_.reset();
        }
    }

    if (open) {
        for (const auto child : scene.children(id)) {
            objectRow(child);
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

void Editor::hierarchy() {
    ImGui::Begin("Scene Hierarchy");
    const auto selected = engine_.scene()->selectedObject();

    if (selected != hierarchySelection_) {
        if (renameObject_ != selected) {
            renameObject_.reset();
        }

        hierarchySelection_ = selected;
        revealParents_.clear();

        for (auto ancestor = selected ? engine_.scene()->object(*selected).parent : std::nullopt; ancestor;
             ancestor = engine_.scene()->object(*ancestor).parent) {
            revealParents_.push_back(*ancestor);
        }
    }

    const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;

    if (ImGui::Button("Add Model", {width, 0})) {
        ImGui::OpenPopup("Add Model");
    }

    ImGui::SameLine();

    if (ImGui::Button("Create Group", {width, 0})) {
        attempt([&] {
            (void)document_.createGroup();
        });
    }

    if (ImGui::BeginPopup("Add Model")) {
        const auto& models = document_.models();

        if (modelIndex_ >= models.size()) {
            modelIndex_ = 0;
        }

        ImGui::TextDisabled("Model asset");
        ImGui::SetNextItemWidth(280);

        if (ImGui::BeginCombo("##model", models.empty() ? "No models" : text(models[modelIndex_]).c_str())) {
            for (std::size_t i = 0; i < models.size(); ++i) {
                const auto label = text(models[i]);

                if (ImGui::Selectable(label.c_str(), modelIndex_ == i)) {
                    modelIndex_ = i;
                }
            }

            ImGui::EndCombo();
        }

        ImGui::BeginDisabled(models.empty());

        if (ImGui::Button("Add to Scene")) {
            attempt([&] {
                (void)document_.addModel(models.at(modelIndex_));
            });
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Refresh List")) {
            attempt([&] {
                document_.refreshModels();
            });
        }

        ImGui::EndPopup();
    }

    ImGui::TextDisabled("Drag into groups or Scene Root");
    ImGui::Separator();
    ImGui::Selectable("Scene Root", false);
    dropTarget({});

    if (ImGui::BeginChild("Objects", {0, 0})) {
        for (const auto& [id, object] : engine_.scene()->objects()) {
            if (!object.parent) {
                objectRow(id);
            }
        }

        const auto remaining = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##rootDrop", {std::max(remaining.x, 1.0F), std::max(remaining.y, 28.0F)});
        dropTarget({});

        if (ImGui::BeginPopupContextItem("Scene Actions")) {
            if (ImGui::MenuItem("Create Group")) {
                hierarchyAction_ = [this] {
                    (void)document_.createGroup();
                };
            }

            const auto selection = engine_.scene()->selectedObject();

            if (ImGui::MenuItem("Group Selected Object", nullptr, false, selection.has_value())) {
                hierarchyAction_ = [this, selection] {
                    (void)document_.groupObject(*selection);
                };
            }

            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
    revealParents_.clear();
    ImGui::End();

    if (hierarchyAction_) {
        auto action = std::move(hierarchyAction_);
        hierarchyAction_ = nullptr;
        attempt(action);
    }
}

void Editor::inspector() {
    auto& scene = *engine_.scene();
    const auto selected = scene.selectedObject();
    const bool selectionChanged = inspectorSelection_ != selected;
    inspectorSelection_ = selected;

    if (selectionChanged) {
        ImGui::SetNextWindowScroll({0, 0});
    }

    ImGui::Begin("Inspector");
    const float height = ImGui::GetWindowHeight();
    const bool revealAnimation = selectionChanged || height < inspectorHeight_;
    inspectorHeight_ = height;

    if (!selected) {
        ImGui::TextWrapped("Select an object in the scene or hierarchy.");
        ImGui::End();
        rotationObject_.reset();

        return;
    }

    const auto id = *selected;
    const auto& object = scene.object(id);
    const auto key = std::to_string(id);
    ImGui::PushID(key.c_str());
    ImGui::SeparatorText("General");
    ImGui::TextDisabled("%s", object.model ? "MODEL INSTANCE" : "GROUP");
    ImGui::SetNextItemWidth(-1);
    auto name = object.name;

    if (ImGui::InputText("##Name", &name)) {
        attempt([&] {
            document_.rename(id, name);
        });
    }

    ImGui::SetItemTooltip("Object name");
    bool enabled = object.enabled;

    if (ImGui::Checkbox("Enabled", &enabled)) {
        attempt([&] {
            document_.setEnabled(id, enabled);
        });
    }

    ImGui::TextDisabled("ID %s", key.c_str());

    if (object.model) {
        ImGui::TextWrapped("Model: %s", text(*object.model).c_str());
    } else {
        ImGui::TextDisabled("%zu direct children", scene.children(id).size());
    }

    ImGui::SeparatorText("Transform");
    ImGui::TextDisabled("Local to %s", object.parent ? scene.object(*object.parent).name.c_str() : "Scene Root");
    auto transform = object.local;

    if (rotationObject_ != id || glm::abs(glm::dot(lastRotation_, transform.rotation)) < 0.999999F) {
        rotationObject_ = id;
        rotation_ = SceneDocument::rotationDegrees(transform.rotation);
        lastRotation_ = transform.rotation;
    }

    bool changed = vectorControl("Position", transform.position, 0.02F);

    if (vectorControl("Rotation (deg)", rotation_, 0.5F)) {
        attempt([&] {
            transform.rotation = SceneDocument::rotationQuaternion(rotation_);
        });

        if (openError_) {
            rotation_ = SceneDocument::rotationDegrees(transform.rotation);
        }

        changed = true;
    }

    changed = vectorControl("Scale", transform.scale, 0.01F) || changed;

    if (changed) {
        attempt([&] {
            document_.setTransform(id, transform);
        });
        lastRotation_ = scene.object(id).local.rotation;
    }

    if (object.model) {
        animationControls(id, revealAnimation);
    }

    ImGui::Separator();

    if (ImGui::Button("Duplicate")) {
        attempt([&] {
            (void)document_.duplicate(id);
        });
    }

    ImGui::SetItemTooltip("Copy this object's properties only, not its children");
    ImGui::SameLine();

    if (ImGui::Button(object.model ? "Delete Object..." : "Delete Group...")) {
        requestDelete(id);
    }

    ImGui::PopID();
    ImGui::End();
}

void Editor::animationControls(ObjectId id, bool reveal) {
    auto& scene = *engine_.scene();
    const auto clips = scene.availableAnimations(id);
    const auto sectionStart = ImGui::GetCursorScreenPos();
    ImGui::SeparatorText("Animation");

    if (clips.empty()) {
        ImGui::TextWrapped("No animation clips in this model.");

        return;
    }

    auto configuration = scene.animationConfiguration(id);
    const auto label = [&](std::size_t clip) {
        return clips[clip].name.empty() ? "Clip " + std::to_string(clip + 1) : clips[clip].name;
    };

    const auto preview = configuration.clip < clips.size()
                             ? label(configuration.clip)
                             : "Missing: " + configuration.clipName.value_or(std::to_string(configuration.clip));
    ImGui::TextUnformatted("Animation Clip");
    ImGui::SetNextItemWidth(-1);

    if (ImGui::BeginCombo("##Animation Clip", preview.c_str())) {
        for (std::size_t clip = 0; clip < clips.size(); ++clip) {
            ImGui::PushID(static_cast<int>(clip));
            const auto name = label(clip);

            if (ImGui::Selectable(name.c_str(), configuration.clip == clip)) {
                configuration.clip = clip;
                configuration.clipName.reset();
                attempt([&] {
                    document_.setAnimationConfiguration(id, configuration);
                });
            }

            if (configuration.clip == clip) {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    const auto state = scene.animationSettings(id);

    if (configuration.clip >= clips.size()) {
        ImGui::TextWrapped("Saved clip unavailable. Choose a clip to preview; rest pose retained.");
    } else {
        ImGui::TextDisabled("%s",
                            state.restPose  ? "Rest pose"
                            : state.paused  ? "Paused"
                            : state.playing ? "Playing"
                                            : "Finished (holding pose)");
    }

    ImGui::BeginDisabled(configuration.clip >= clips.size());

    if (ImGui::Button(state.playing && !state.paused ? "Pause" : state.playing && state.paused ? "Resume" : "Play")) {
        attempt([&] {
            if (state.playing && !state.paused) {
                scene.pauseAnimation(id);
            } else if (state.playing && state.paused && state.clip == configuration.clip) {
                scene.resumeAnimation(id);
            } else {
                scene.setAnimationSpeed(id, configuration.speed);
                scene.playAnimation(id, configuration.clip, configuration.loop);
            }
        });
    }

    ImGui::SameLine();

    if (ImGui::Button("Stop")) {
        attempt([&] {
            scene.stopAnimation(id);
        });
    }

    ImGui::SetItemTooltip("Stop preview and restore rest pose. Does not change Autoplay.");

    if (ImGui::Checkbox("Loop", &configuration.loop)) {
        attempt([&] {
            document_.setAnimationConfiguration(id, configuration);
        });
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("Autoplay", &configuration.autoplay)) {
        attempt([&] {
            document_.setAnimationConfiguration(id, configuration);
        });
    }

    ImGui::SetItemTooltip("Start this clip when the saved scene loads. Preview buttons do not change this setting.");
    ImGui::TextUnformatted("Playback Speed");
    ImGui::SetNextItemWidth(-1);

    if (ImGui::DragFloat("##Playback Speed", &configuration.speed, 0.01F, 0, 0, "%.2fx")) {
        attempt([&] {
            document_.setAnimationConfiguration(id, configuration);
        });
    }

    ImGui::SetItemTooltip("1 = normal, 0 = freeze, negative = reverse. Non-looping clips hold their endpoint.");
    ImGui::EndDisabled();

    // General/Transform can put these controls below the fold in short windows.
    // Reveal only on selection/shrink, never fight the user's subsequent scrolling.
    const auto window = ImGui::GetWindowPos();
    const auto padding = ImGui::GetStyle().WindowPadding.y;

    if (reveal && (sectionStart.y < window.y + padding ||
                   ImGui::GetCursorScreenPos().y > window.y + ImGui::GetWindowHeight() - padding)) {
        ImGui::SetScrollFromPosY(sectionStart.y - window.y - padding, 0);
    }
}

void Editor::performance() {
    if (!showPerformance_) {
        return;
    }

    ImGui::SetNextWindowSize({240, 140}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance", &showPerformance_);
    const auto& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("Frame: %.2f ms", 1000.0F / std::max(io.Framerate, 0.01F));
    std::size_t visible = 0;
    const auto& scene = *engine_.scene();

    for (const auto& [id, object] : scene.objects()) {
        (void)object;

        if (scene.enabledInHierarchy(id)) {
            ++visible;
        }
    }

    ImGui::Text("Objects: %zu", scene.objects().size());
    ImGui::Text("Visible objects: %zu", visible);
    ImGui::End();
}

void Editor::dialogs() {
    gridTools_.dialog(document_);
    deleteDialog();
    sceneFileDialog();
    unsavedChangesDialog();
    errorDialog();
}

void Editor::deleteDialog() {
    if (openDelete_) {
        ImGui::OpenPopup("Delete from Scene");
        openDelete_ = false;
    }

    if (!ImGui::BeginPopupModal("Delete from Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto& scene = *engine_.scene();

    if (!deleteObject_ || !scene.contains(*deleteObject_)) {
        deleteObject_.reset();
        ImGui::CloseCurrentPopup();
    } else {
        const auto id = *deleteObject_;
        const auto& object = scene.object(id);
        std::vector<ObjectId> descendants{id};

        for (std::size_t index = 0; index < descendants.size(); ++index) {
            const auto children = scene.children(descendants[index]);
            descendants.insert(descendants.end(), children.begin(), children.end());
        }

        ImGui::Text("Delete '%s'?", object.name.c_str());

        if (descendants.size() > 1) {
            ImGui::Text("This also deletes all %zu descendant objects.", descendants.size() - 1);
        } else {
            ImGui::TextUnformatted("This removes the object from the scene.");
        }

        ImGui::TextUnformatted("Move children to Scene Root first if you want to keep them.");
        ImGui::Spacing();

        if (ImGui::Button("Delete")) {
            attempt([&] {
                document_.remove(id);
            });
            deleteObject_.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            deleteObject_.reset();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void Editor::sceneFileList() {
    std::error_code error;
    const auto folder = engine_.assetRoot() / "scenes";

    if (!std::filesystem::is_directory(folder, error) || !ImGui::BeginListBox("Scenes", {580, 120})) {
        return;
    }

    for (std::filesystem::directory_iterator
             item{folder, std::filesystem::directory_options::skip_permission_denied, error},
         end;
         item != end;
         item.increment(error)) {
        if (error) {
            break;
        }

        if (!item->is_regular_file(error) || item->path().extension() != ".json") {
            continue;
        }

        const auto label = text(item->path().filename());

        if (ImGui::Selectable(label.c_str())) {
            filePath_ = text(item->path());
        }
    }

    if (error) {
        ImGui::TextWrapped("Cannot list scenes: %s", error.message().c_str());
    }

    ImGui::EndListBox();
}

bool Editor::saveSceneFromDialog(const std::filesystem::path& filename) {
    bool saved = false;
    attempt([&] {
        document_.save(filename);
        saved = true;
    });

    if (!saved) {
        dialogError_ = error_;
        openError_ = false;
    }

    return saved;
}

void Editor::sceneFileDialog() {
    if (openFilePopup_) {
        ImGui::OpenPopup("Scene File");
        openFilePopup_ = false;
    }

    if (!ImGui::BeginPopupModal("Scene File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted(fileMode_ == FileMode::open ? "Open a JSON scene" : "Save scene as JSON");
    ImGui::SetNextItemWidth(580);
    ImGui::InputText("Path", &filePath_);
    ImGui::TextWrapped("Enter an absolute path, or an executable-relative path. Existing files "
                       "can be overwritten.");
    ImGui::TextWrapped("Builds restage runtime assets. Use Save As to choose your authored "
                       "source scene when keeping edits across builds.");
    if (!dialogError_.empty()) {
        ImGui::PushTextWrapPos(580);
        ImGui::TextUnformatted(dialogError_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (fileMode_ == FileMode::open) {
        sceneFileList();
    }

    if (ImGui::Button(fileMode_ == FileMode::open ? "Open" : "Save")) {
        const auto selectedPath = path(filePath_);

        if (fileMode_ == FileMode::open) {
            ImGui::CloseCurrentPopup();
            fileMode_ = FileMode::none;
            request(Operation::open, selectedPath);
        } else if (saveSceneFromDialog(selectedPath)) {
            ImGui::CloseCurrentPopup();
            fileMode_ = FileMode::none;

            if (resumeAfterSave_) {
                resumeAfterSave_ = false;
                executePending();
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        fileMode_ = FileMode::none;

        if (resumeAfterSave_) {
            resumeAfterSave_ = false;
            pending_ = Operation::none;
        }
    }

    ImGui::EndPopup();
}

void Editor::unsavedChangesDialog() {
    if (confirm_) {
        ImGui::OpenPopup("Unsaved Changes");
        confirm_ = false;
    }

    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Save scene changes before continuing?");

    if (!dialogError_.empty()) {
        ImGui::PushTextWrapPos(580);
        ImGui::TextUnformatted(dialogError_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (ImGui::Button("Save and Continue")) {
        if (document_.path().empty()) {
            resumeAfterSave_ = true;
            ImGui::CloseCurrentPopup();
            fileDialog(FileMode::save);
        } else if (saveSceneFromDialog(document_.path())) {
            ImGui::CloseCurrentPopup();
            executePending();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Discard")) {
        ImGui::CloseCurrentPopup();
        executePending();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        pending_ = Operation::none;
        pendingPath_.clear();
    }

    ImGui::EndPopup();
}

void Editor::errorDialog() {
    if (openError_) {
        ImGui::OpenPopup("Editor Error");
        openError_ = false;
    }

    if (!ImGui::BeginPopupModal("Editor Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::PushTextWrapPos(600);
    ImGui::TextUnformatted(error_.c_str());
    ImGui::PopTextWrapPos();

    if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

OverlayInput Editor::frame() {
    if (!engine_.scene()) {
        document_.newScene();
    }

    ImGui::SetCurrentContext(context_);
    document_.observeCamera();
    const bool relative = SDL_GetWindowRelativeMouseMode(native_.window);
    auto& io = ImGui::GetIO();

    if (relative) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }

    ImGui_ImplSDL3_SetMouseCaptureMode(relative ? ImGui_ImplSDL3_MouseCaptureMode_Disabled
                                                : ImGui_ImplSDL3_MouseCaptureMode_Enabled);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu();
    layout();
    hierarchy();
    inspector();
    performance();
    dialogs();
    viewportMouse_ = !relative && !io.WantCaptureMouse;
    const OverlayInput result{!relative && io.WantCaptureMouse, io.WantCaptureKeyboard, close_, gridTools_.editing()};

    if (close_) {
        ImGui::Render();
    }

    return result;
}

void Editor::finishFrame(const Camera& camera) {
    ImGui::SetCurrentContext(context_);
    gridTools_.finishFrame(engine_, document_, camera, native_.window, viewportMouse_);
    ImGui::Render();
}

void Editor::render(VkCommandBuffer command) {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void run(const std::filesystem::path& scene, const std::filesystem::path& environment) {
    Engine engine{EngineConfig{.windowTitle = "Grimlock Scene Editor", .environmentFolder = environment}};
    Editor editor{engine, scene.empty() ? engine.assetRoot() / "scenes" / "demo.json" : scene};

    while (engine.update()) {
    }
}

} // namespace grimlock::editor
