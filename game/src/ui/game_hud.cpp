#include "ui/game_hud.hpp"

#include "ui/game_style.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <grimlock/input.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

namespace grimlock::game {

namespace {
constexpr ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
}

GameHud::GameHud(Engine& engine,
                 LevelRuntime& runtime,
                 bool startInMenu,
                 const std::filesystem::path& initialLevel,
                 const std::filesystem::path& optionsFile)
    : engine_(engine), runtime_(runtime), optionsPath_(optionsFile) {
    if (startInMenu) {
        runtime_.unload(engine_);
        screen_ = Screen::mainMenu;
        if (!initialLevel.empty()) {
            requestedLevel_ = initialLevel;
        }
    }
    engine_.attachOverlay(*this);
}

GameHud::~GameHud() {
    clearTowerPreview();
    clearSelectionMarker();
    engine_.detachOverlay();
}

void GameHud::configureUi() {
    presentation::configure();
    initializeMenu();
}

void GameHud::updateLayout() {
    const auto size = ImGui::GetIO().DisplaySize;
    const float automatic = std::clamp(std::min(size.x / 1280, size.y / 720), 0.85F, 1.1F);
    const float scale = std::clamp(automatic * uiScale_, 0.75F, 1.25F);
    layout_.scale = scale;
    layout_.gap = 14 * scale;
    layout_.top = 62 * scale;
    layout_.dockWidth = std::min(240 * scale, size.x - 2 * layout_.gap);
    layout_.dockHeight = (runtime_.placing() ? 178 : 108) * scale;
    layout_.contextWidth = std::min(360 * scale, size.x - 2 * layout_.gap);
    layout_.contextHeight = std::min(330 * scale, size.y - layout_.top - layout_.dockHeight - 4 * layout_.gap);
    presentation::scale(scale);
}

OverlayInput GameHud::frame() {
    ImGui::SetCurrentContext(context_);
    modalFrame_ = false;
    cancelWorldInput_ = false;
    transitions();
    if (restart_) {
        clearFeedback();
        try {
            runtime_.restart(engine_);
        } catch (const std::exception& error) {
            error_ = error.what();
            runtime_.unload(engine_);
            screen_ = Screen::levels;
            refreshLevels();
        }
    }
    ImGui_ImplVulkan_NewFrame();
    const bool mouseRelative = SDL_GetWindowRelativeMouseMode(native_.window);
    if (!mouseRelative) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
    ImGui_ImplSDL3_SetMouseCaptureMode(mouseRelative ? ImGui_ImplSDL3_MouseCaptureMode_Disabled
                                                     : ImGui_ImplSDL3_MouseCaptureMode_Enabled);
    ImGui_ImplSDL3_NewFrame();
    updateLayout();
    ImGui::NewFrame();
    entrance_ = std::min(1.0F, entrance_ + ImGui::GetIO().DeltaTime * 5);
    if (screen_ != Screen::gameplay) {
        modalFrame_ = true;
        menu();
        if (settingsOpen_) {
            settings();
        }
        viewportMouse_ = false;
        commandCursor_.reset();
        commandGesture_ = {};
        return {true, true, false, true};
    }
    const auto* waves = runtime_.level().waves();
    const bool terminal = waves && (waves->phase() == LevelPhase::victory || waves->phase() == LevelPhase::defeat);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !terminal) {
        cancelWorldInput_ = true;
        if (settingsOpen_) {
            settingsOpen_ = false;
        } else if (runtime_.paused()) {
            runtime_.setPaused(false);
        } else if (runtime_.placing()) {
            runtime_.cancelPlacement();
        } else if (runtime_.selectedHero() || runtime_.inspectedTower()) {
            runtime_.clearSelection();
            orderPosition_.reset();
        } else {
            runtime_.setPaused(!runtime_.paused());
        }
    }
    if (runtime_.paused() || terminal) {
        modalFrame_ = true;
        SDL_SetWindowRelativeMouseMode(native_.window, false);
        commandGesture_ = {};
        commandCursor_.reset();
        viewportMouse_ = false;
        if (terminal) {
            result();
        } else {
            pauseMenu();
        }
        return {true, true, false, true};
    }
    const bool relative = SDL_GetWindowRelativeMouseMode(native_.window);
    auto& io = ImGui::GetIO();
    if (relative) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
    ImGui_ImplSDL3_SetMouseCaptureMode(relative ? ImGui_ImplSDL3_MouseCaptureMode_Disabled
                                                : ImGui_ImplSDL3_MouseCaptureMode_Enabled);
    upgradeFlashRemaining_ = std::max(0.0, upgradeFlashRemaining_ - io.DeltaTime);
    orderRemaining_ = std::max(0.0F, orderRemaining_ - io.DeltaTime);
    status();
    if (waves) {
        construction();
        coins();
    }
    towerInfo();
    bossHealth();
    notifications();
    const bool overUi = io.WantCaptureMouse || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    viewportMouse_ = !runtime_.paused() && !relative && !overUi && !cancelWorldInput_;
    const bool commandsAllowed = !cancelWorldInput_ && !runtime_.placing();
    commandCursor_ = commandGesture_.update(engine_.input(), commandsAllowed && (relative || !overUi));
    return {!relative && overUi, io.WantCaptureKeyboard, false, runtime_.placing()};
}

void GameHud::clearFeedback() {
    clearTowerPreview();
    clearSelectionMarker();
    hoveredObject_.reset();
    orderPosition_.reset();
    orderHero_ = previewHero_ = 0;
    hoverDestination_.reset();
    hoverPath_.valid = false;
    hoverRoute_.clear();
    orderedRoutes_.clear();
    orderRemaining_ = 0;
    settingsOpen_ = false;
    entrance_ = 0;
    displayedPhase_.reset();
    notificationRemaining_ = 0;
    resultCatalogReady_ = false;
    restart_ = false;
    upgradedTower_.reset();
    upgradeFlashRemaining_ = 0;
    bossBars_.clear();
    commandGesture_ = {};
    commandCursor_.reset();
    error_.clear();
}

void GameHud::refreshLevels() {
    catalog_ = discoverLevels(engine_.assetRoot());
    if (selectedLevel_ &&
        std::none_of(catalog_.levels.begin(), catalog_.levels.end(), [&](const AvailableLevel& level) {
            return level.path == *selectedLevel_;
        })) {
        selectedLevel_.reset();
    }
    refreshRemaining_ = 2;
    if (!selectedLevel_ && runtime_.progress()) {
        for (const auto& level : catalog_.levels) {
            if (level.id == runtime_.progress()->lastPlayed() && !level.id.empty()) {
                selectedLevel_ = level.path;
                break;
            }
        }
    }
    if (!selectedLevel_ && !catalog_.levels.empty()) {
        selectedLevel_ = catalog_.levels.front().path;
    }
}

void GameHud::progressNotice() {
    if (const auto* progress = runtime_.progress(); progress && !progress->error().empty()) {
        ImGui::TextColored({1, 0.65F, 0.4F, 1}, "Progress saving unavailable (hover for details)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", progress->error().c_str());
        }
    }
}

void GameHud::transitions() {
    if (requestedScreen_) {
        const auto next = *requestedScreen_;
        requestedScreen_.reset();
        runtime_.unload(engine_);
        clearFeedback();
        screen_ = next;
        SDL_SetWindowRelativeMouseMode(native_.window, false);
        if (screen_ == Screen::levels) {
            refreshLevels();
        }
    }
    if (requestedLevel_) {
        const auto path = *requestedLevel_;
        requestedLevel_.reset();
        clearFeedback();
        try {
            runtime_.load(engine_, path);
            screen_ = Screen::gameplay;
        } catch (const std::exception& error) {
            runtime_.unload(engine_);
            screen_ = Screen::levels;
            error_ = error.what();
            refreshLevels();
        }
    }
}

void GameHud::notifications() {
    const auto* waves = runtime_.level().waves();
    if (!waves) {
        return;
    }
    if (displayedPhase_ != waves->phase() || displayedWave_ != waves->wave()) {
        if (waves->phase() == LevelPhase::battle) {
            notification_ = "Wave " + std::to_string(waves->wave() + 1) + " | Defend the objective";
        } else if (displayedPhase_ == LevelPhase::battle && waves->phase() == LevelPhase::preparation) {
            notification_ = "Wave cleared";
            if (const auto reward = runtime_.level().lastWaveReward().value_or(0); reward > 0) {
                notification_ += " | +" + std::to_string(reward) + " gold";
            }
        } else {
            notification_ = "Preparation | Build and position your defenders";
        }
        notificationRemaining_ = 3;
        displayedPhase_ = waves->phase();
        displayedWave_ = waves->wave();
    }
    notificationRemaining_ = std::max(0.0F, notificationRemaining_ - ImGui::GetIO().DeltaTime);
    if (notificationRemaining_ <= 0) {
        return;
    }
    const auto* v = ImGui::GetMainViewport();
    const float width = std::min(410 * layout_.scale, v->Size.x - 2 * layout_.gap);
    const float alpha = std::min(1.0F, notificationRemaining_) * 0.95F;
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::SetNextWindowPos(
        {v->Pos.x + layout_.gap, v->Pos.y + v->Size.y - layout_.gap - layout_.dockHeight - 42 * layout_.scale});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    if (ImGui::Begin("Battle Notice",
                     nullptr,
                     hudFlags | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextWrapped("%s", notification_.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void GameHud::status() {
    const auto* waves = runtime_.level().waves();
    if (!waves) {
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = layout_.scale;
    const auto flags = hudFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const float healthWidth = std::min(390 * scale, viewport->Size.x * 0.30F);
    ImGui::SetNextWindowPos({viewport->Pos.x + layout_.gap, viewport->Pos.y + layout_.gap});
    if (ImGui::Begin("Gameplay Settings", nullptr, flags)) {
        const float gearSize = 44 * scale;
        if (ImGui::Button("##GameplaySettings", {gearSize, gearSize})) {
            runtime_.setPaused(true);
            cancelWorldInput_ = true;
        }
        const auto minimum = ImGui::GetItemRectMin();
        const ImVec2 center{minimum.x + gearSize * 0.5F, minimum.y + gearSize * 0.5F};
        auto* draw = ImGui::GetWindowDrawList();
        presentation::angularPanel(minimum, {minimum.x + gearSize, minimum.y + gearSize}, presentation::ivory, false);
        const auto color = presentation::menuInk(presentation::forest);
        for (int point = 0; point < 32; ++point) {
            const float angle = glm::two_pi<float>() * point / 32;
            const float radius = (point % 4 == 0 || point % 4 == 3 ? 13 : 10) * scale;
            draw->PathLineTo({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
        }
        draw->PathStroke(color, ImDrawFlags_Closed, 1.8F * scale);
        draw->AddCircle(center, 4 * scale, color, 16, 1.8F * scale);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Settings / Main Menu");
        }
    }
    ImGui::End();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F - healthWidth * 0.5F - layout_.gap,
                             viewport->Pos.y + layout_.gap + 9 * scale},
                            ImGuiCond_Always,
                            {1, 0});
    if (ImGui::Begin("Round", nullptr, flags)) {
        ImGui::Text("ROUND %zu / %zu",
                    std::min(waves->wave() + 1, waves->definition().waves.size()),
                    waves->definition().waves.size());
    }
    ImGui::End();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + layout_.gap + 8 * scale},
                            ImGuiCond_Always,
                            {0.5F, 0});
    if (ImGui::Begin("Objective Health", nullptr, flags)) {
        char health[64];
        std::snprintf(health,
                      sizeof(health),
                      "%llu / %llu",
                      static_cast<unsigned long long>(waves->health()),
                      static_cast<unsigned long long>(waves->definition().objectiveHealth));
        presentation::meter(static_cast<float>(waves->health()) / waves->definition().objectiveHealth,
                            {healthWidth, 24 * scale},
                            health);
    }
    ImGui::End();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - layout_.gap, viewport->Pos.y + layout_.gap},
                            ImGuiCond_Always,
                            {1, 0});
    if (ImGui::Begin("Wave Controls", nullptr, flags)) {
        ImGui::BeginDisabled(waves->phase() != LevelPhase::preparation);
        if (presentation::action("Start Wave", {std::min(205 * scale, viewport->Size.x * 0.23F), 48 * scale}, true)) {
            runtime_.startWave();
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void GameHud::bossHealth() {
    const auto* waves = runtime_.level().waves();
    const auto* enemies = runtime_.navigation().enemies();
    if (!waves || waves->phase() != LevelPhase::battle || !enemies) {
        bossBars_.clear();
        return;
    }
    const auto activeBoss = [&](const Enemy* enemy) {
        return enemy && enemy->active && enemy->alive && enemy->health > 0 && enemies->definition(enemy->type).boss;
    };
    std::erase_if(bossBars_, [&](const BossBar& bar) {
        return !activeBoss(enemies->find(bar.enemy));
    });
    const float blend = 1 - std::exp(-8.0F * ImGui::GetIO().DeltaTime);
    for (const auto& enemy : enemies->enemies()) {
        if (!activeBoss(&enemy)) {
            continue;
        }
        const float fraction = std::clamp(enemy.health / enemy.maximumHealth, 0.0F, 1.0F);
        const auto found = std::find_if(bossBars_.begin(), bossBars_.end(), [&](const BossBar& bar) {
            return bar.enemy == enemy.id;
        });
        if (found == bossBars_.end()) {
            bossBars_.push_back({enemy.id, fraction});
        } else {
            found->fraction += (fraction - found->fraction) * blend;
        }
    }
    if (bossBars_.empty()) {
        return;
    }
    auto* viewport = ImGui::GetMainViewport();
    const float width = std::max(100.0F, std::min(560.0F, viewport->Size.x - 36));
    const float left = (viewport->Size.x - width) * 0.5F;
    const float top = layout_.gap + layout_.top + 10 * layout_.scale;
    const bool contextual = runtime_.inspectedTower() != nullptr;
    const float lower = viewport->Size.y - layout_.gap - (contextual ? layout_.contextHeight : layout_.dockHeight) - 12;
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 32 + ImGui::GetStyle().ItemSpacing.y;
    const float height = std::min(24 + rowHeight * static_cast<float>(bossBars_.size()),
                                  std::max(60.0F, std::min(viewport->Size.y * 0.28F, lower - top)));
    ImGui::SetNextWindowPos({viewport->Pos.x + left, viewport->Pos.y + top});
    ImGui::SetNextWindowSize({width, height});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.055F, 0.018F, 0.027F, 0.96F});
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(180, 48, 72, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(35, 20, 28, 255));
    const auto flags = (hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Boss Health", nullptr, flags)) {
        for (const auto& bar : bossBars_) {
            const auto* enemy = enemies->find(bar.enemy);
            const auto& name = enemies->definition(enemy->type).name;
            const float textWidth = ImGui::CalcTextSize(name.c_str()).x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowSize().x - textWidth) * 0.5F));
            ImGui::TextColored({0.95F, 0.78F, 0.48F, 1}, "%s", name.c_str());
            char health[80];
            std::snprintf(health, sizeof(health), "%.0f / %.0f", enemy->health, enemy->maximumHealth);
            ImGui::ProgressBar(bar.fraction, {-1, 32}, health);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
}

void GameHud::coinIcon(ImVec2 center, float size) {
    if (coinTriangles_.empty()) {
        const auto model = engine_.loadModel(engine_.assetRoot() / "environment/dungeon/coin.glb");
        const auto bounds = engine_.modelInfo(model);
        coinTriangles_ = engine_.modelTriangles(model);
        coinCenter_ = (bounds.minimum + bounds.maximum) * 0.5F;
        coinHeight_ = std::max(bounds.maximum.y - bounds.minimum.y, 0.001F);
    }

    struct Face {
        std::array<ImVec2, 3> points;
        float depth;
        ImU32 color;
    };

    std::vector<Face> faces;
    faces.reserve(coinTriangles_.size());
    const auto rotation = glm::angleAxis(static_cast<float>(ImGui::GetTime()) * 0.7F, glm::vec3{0, 1, 0}) *
                          glm::angleAxis(-0.18F, glm::vec3{1, 0, 0});
    // Project the actual coin mesh into the HUD, independent of scene depth and camera movement.
    for (const auto& triangle : coinTriangles_) {
        std::array<glm::vec3, 3> points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            points[i] = rotation * ((triangle[i] - coinCenter_) / coinHeight_);
        }
        const auto normal = glm::cross(points[1] - points[0], points[2] - points[0]);
        if (normal.z <= 0 || glm::dot(normal, normal) < 0.000001F) {
            continue;
        }
        const float light =
            0.38F + 0.62F * std::max(0.0F, glm::dot(glm::normalize(normal), glm::normalize(glm::vec3{-0.3F, 0.6F, 1})));
        Face face{
            {},
            (points[0].z + points[1].z + points[2].z) / 3,
            IM_COL32(static_cast<int>(255 * light), static_cast<int>(199 * light), static_cast<int>(52 * light), 255)};
        for (std::size_t i = 0; i < points.size(); ++i) {
            face.points[i] = {center.x + points[i].x * size, center.y - points[i].y * size};
        }
        faces.push_back(face);
    }
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return a.depth < b.depth;
    });
    auto* draw = ImGui::GetWindowDrawList();
    const auto flags = draw->Flags;
    // Mesh triangles share edges. ImGui's per-shape AA fringe exposes those edges as cracks
    // and overlaps neighboring faces; only independent UI shapes should use that fringe.
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (const auto& face : faces) {
        draw->AddTriangleFilled(face.points[0], face.points[1], face.points[2], face.color);
    }
    draw->Flags = flags;
}

void GameHud::coins() {
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = layout_.scale;
    ImGui::SetNextWindowPos(
        {viewport->Pos.x + viewport->Size.x - layout_.gap, viewport->Pos.y + viewport->Size.y - layout_.gap},
        ImGuiCond_Always,
        {1, 1});
    ImGui::SetNextWindowSize({145 * scale, 58 * scale});
    if (ImGui::Begin("Coins",
                     nullptr,
                     (hudFlags & ~ImGuiWindowFlags_AlwaysAutoResize) | ImGuiWindowFlags_NoBackground)) {
        presentation::windowPanel();
        const auto a = ImGui::GetWindowPos();
        coinIcon({a.x + 34 * scale, a.y + 27 * scale}, 30 * scale);
        ImGui::SetCursorPos({64 * scale, 16 * scale});
        ImGui::PushFont(nullptr, 24 * scale);
        ImGui::Text("%llu", static_cast<unsigned long long>(runtime_.level().economy().gold()));
        ImGui::PopFont();
    }
    ImGui::End();
}

void GameHud::construction() {
    const auto* waves = runtime_.level().waves();
    const bool preparing = waves && waves->phase() == LevelPhase::preparation;
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = layout_.scale;
    ImGui::SetNextWindowPos({viewport->Pos.x + layout_.gap, viewport->Pos.y + viewport->Size.y - layout_.gap},
                            ImGuiCond_Always,
                            {0, 1});
    ImGui::SetNextWindowSize({layout_.dockWidth, layout_.dockHeight});
    const auto flags = hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::Begin("Build Towers", nullptr, flags | ImGuiWindowFlags_NoBackground)) {
        presentation::windowPanel();
        const float cardWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        std::size_t index = 0;
        for (const auto& definition : runtime_.navigation().towers().definitions()) {
            if (index++ % 2) {
                ImGui::SameLine();
            }
            ImGui::PushID(definition.id.c_str());
            const bool affordable = runtime_.level().economy().canAfford(definition.constructionCost);
            ImGui::BeginDisabled(!preparing || !affordable);
            if (presentation::towerCard(definition.name.c_str(),
                                        definition.constructionCost,
                                        runtime_.placing() && runtime_.selectedTower() == definition.id,
                                        {cardWidth, 62 * scale})) {
                runtime_.beginPlacement(definition.id);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                const auto& stats = definition.statistics();
                ImGui::SetTooltip("%s\nDamage %.0f | Range %.1f | %.1f shots/s\n%s",
                                  definition.name.c_str(),
                                  stats.damage,
                                  stats.attackRange,
                                  stats.fireRate,
                                  !affordable  ? "Insufficient gold"
                                  : !preparing ? "Build during Preparation"
                                               : "Select a placement position");
            }
            ImGui::PopID();
        }
        if (runtime_.placing()) {
            if (presentation::action("Cancel Placement", {-1, 28 * scale})) {
                runtime_.cancelPlacement();
            }
            if (!runtime_.placementError().empty()) {
                ImGui::TextWrapped("%s", runtime_.placementError().c_str());
            } else {
                ImGui::TextDisabled("Click the map to build | Esc to cancel");
            }
        }
    }
    ImGui::End();
}

void GameHud::towerInfo() {
    const auto* tower = runtime_.inspectedTower();
    if (!tower) {
        return;
    }
    const auto object = tower->object;
    const auto& towers = runtime_.navigation().towers();
    const auto& def = towers.definition(tower->type);
    const auto& progress = towers.progress(object);
    const auto& current = def.statistics(progress.level);
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x - layout_.gap,
                             v->Pos.y + std::min(v->Size.y * 0.5F + layout_.contextHeight * 0.5F,
                                                 v->Size.y - 2 * layout_.gap - layout_.dockHeight)},
                            ImGuiCond_Always,
                            {1, 1});
    ImGui::SetNextWindowSize({layout_.contextWidth, layout_.contextHeight});
    if (ImGui::Begin("Tower Info",
                     nullptr,
                     (hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) |
                         ImGuiWindowFlags_NoBackground)) {
        presentation::windowPanel();
        std::string towerTitle = def.name;
        std::transform(towerTitle.begin(), towerTitle.end(), towerTitle.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        ImGui::PushFont(nullptr, 25 * s);
        ImGui::Text("%s", towerTitle.c_str());
        ImGui::PopFont();
        ImGui::Text("LV. %zu", progress.level);
        const auto levelPosition = ImGui::GetCursorScreenPos();
        for (std::size_t index = 0; index < def.levels.size(); ++index) {
            const ImVec2 a{levelPosition.x + index * 23 * s, levelPosition.y};
            ImGui::GetWindowDrawList()->AddRectFilled(
                a,
                {a.x + 15 * s, a.y + 12 * s},
                presentation::menuInk(index < progress.level ? presentation::menuGold : presentation::secondary),
                2);
        }
        ImGui::Dummy({0, 20 * s});
        char health[64];
        std::snprintf(health, sizeof(health), "%.0f / %.0f", progress.health, progress.maximumHealth);
        presentation::meter(progress.health / progress.maximumHealth, {-1, 16 * s}, health);
        ImGui::Text("Damage %.0f | Range %.1f", current.damage, current.attackRange);
        ImGui::TextDisabled("%.1f shots / second", current.fireRate);
        if (progress.level < def.levels.size()) {
            const auto& next = def.statistics(progress.level + 1);
            ImGui::Separator();
            ImGui::TextColored(presentation::gold,
                               "NEXT LEVEL | %llu gold",
                               static_cast<unsigned long long>(next.upgradeCost));
            ImGui::Text("Damage %+.0f | Range %+.1f",
                        next.damage - current.damage,
                        next.attackRange - current.attackRange);
            ImGui::TextDisabled("Fire rate %+.1f / second", next.fireRate - current.fireRate);
            const auto error = runtime_.level().upgradeError(runtime_.navigation(), object);
            ImGui::BeginDisabled(!error.empty());
            const auto upgradeLabel = "UPGRADE    " + std::to_string(next.upgradeCost) + "###Upgrade";
            if (presentation::action(upgradeLabel.c_str(), {-1, 52 * s}, true)) {
                try {
                    runtime_.level().upgradeTower(engine_, runtime_.navigation(), object);
                    upgradedTower_ = object;
                    upgradeFlashRemaining_ = 0.8;
                    error_.clear();
                } catch (const std::exception& failure) {
                    error_ = failure.what();
                }
            }
            ImGui::EndDisabled();
            if (!error.empty()) {
                ImGui::TextWrapped("%s", error.c_str());
            }
        } else {
            ImGui::TextColored(presentation::gold, "MAX LEVEL");
        }
        if (upgradedTower_ == object && upgradeFlashRemaining_ > 0) {
            ImGui::TextColored(presentation::green, "Defense upgraded");
        }
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
        }
    }
    ImGui::End();
}

void GameHud::result() {
    presentation::MenuTheme theme;
    ImGui::PushFont(menuFont_, 20);
    const auto* waves = runtime_.level().waves();
    if (!resultCatalogReady_) {
        refreshLevels();
        resultCatalogReady_ = true;
    }
    auto* viewport = ImGui::GetMainViewport();
    presentation::menuShade(0.72F);
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
                            ImGuiCond_Always,
                            {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::min(500 * layout_.scale, viewport->Size.x - 2 * layout_.gap), 0});
    if (ImGui::Begin("Level Result", nullptr, hudFlags)) {
        presentation::menuHeading(waves->phase() == LevelPhase::victory ? "VICTORY" : "DEFEAT", nullptr);
        ImGui::TextUnformatted(waves->phase() == LevelPhase::victory ? "Your objective survived every wave."
                                                                     : "Your defense objective has fallen.");
        ImGui::TextUnformatted(waves->definition().name.c_str());
        if (waves->phase() == LevelPhase::victory) {
            ImGui::Text("Completion time: %.2f s", runtime_.completionSeconds());
            ImGui::Text("Waves completed: %zu / %zu", waves->wave(), waves->definition().waves.size());
            ImGui::TextUnformatted(runtime_.personalRecord() ? "New personal record!" : "No new personal record");
        } else {
            ImGui::Text("Wave reached: %zu / %zu", waves->wave() + 1, waves->definition().waves.size());
        }
        if (presentation::menuAction("RETRY###Retry", {-1, 38 * layout_.scale}, true)) {
            restart_ = true;
        }
        if (waves->phase() == LevelPhase::victory) {
            const auto current =
                std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const AvailableLevel& level) {
                    return !waves->definition().id.empty() ? level.id == waves->definition().id
                                                           : level.path == runtime_.path();
                });
            if (current != catalog_.levels.end() && std::next(current) != catalog_.levels.end()) {
                if (presentation::menuAction("NEXT LEVEL###Next Level", {-1, 38 * layout_.scale})) {
                    requestedLevel_ = std::next(current)->path;
                }
            }
        }
        if (presentation::menuAction("LEVELS###Choose Level", {-1, 38 * layout_.scale})) {
            requestedScreen_ = Screen::levels;
        }
    }
    ImGui::End();
    ImGui::PopFont();
}

void GameHud::finishFrame(const Camera& camera) {
    ImGui::SetCurrentContext(context_);
    if (modalFrame_ || screen_ != Screen::gameplay || runtime_.paused() ||
        (runtime_.level().waves() && (runtime_.level().waves()->phase() == LevelPhase::victory ||
                                      runtime_.level().waves()->phase() == LevelPhase::defeat))) {
        clearSelectionMarker();
        clearTowerPreview();
        ImGui::Render();
        return;
    }
    glm::ivec2 logical{}, drawable{};
    SDL_GetWindowSize(native_.window, &logical.x, &logical.y);
    SDL_GetWindowSizeInPixels(native_.window, &drawable.x, &drawable.y);
    const auto& input = engine_.input();
    const auto busy = [&](MouseButton button) {
        const auto& state = input.button(button);
        return state.held || state.pressed || state.released;
    };
    std::optional<Ray> ray;
    bool confirm = false;
    const bool worldPointer = viewportMouse_ && input.focused && !busy(MouseButton::right) &&
                              !busy(MouseButton::middle) && input.mouseWheel == glm::vec2{0};
    if (worldPointer) {
        ray = mouseRay(camera, input.mousePosition, logical, drawable);
        if (input.button(MouseButton::left).pressed && input.leftPressPosition) {
            ray = mouseRay(camera, *input.leftPressPosition, logical, drawable);
            confirm = true;
        }
    }
    const auto issueOrder = [&](const Ray& commandRay) {
        if (!runtime_.commandHero(commandRay)) {
            return;
        }
        const auto* hero = runtime_.selectedHero();
        const auto* enemy = hero->combat.target ? runtime_.navigation().enemies()->find(*hero->combat.target) : nullptr;
        orderAttack_ = enemy != nullptr;
        orderHero_ = hero->id;
        orderPosition_ = enemy ? std::optional{enemy->position} : hero->destination;
        orderRemaining_ = 0.8F;
        if (!orderAttack_) {
            // Release selection without cancelling the move that was just confirmed.
            runtime_.clearSelection(false);
        }
    };
    if (!runtime_.placing() && confirm) {
        const auto object = ray ? engine_.pickObject(*ray) : std::nullopt;
        const bool selectable = object && ((runtime_.heroes() && runtime_.heroes()->fromObject(*object)) ||
                                           runtime_.navigation().towers().contains(*object));
        if (selectable || !runtime_.selectedHero()) {
            runtime_.selectObject(engine_, ray);
        } else if (ray) {
            issueOrder(*ray);
        }
    }
    hoveredObject_.reset();
    if (ray && !runtime_.placing()) {
        const auto object = confirm ? engine_.selectedObject() : engine_.pickObject(*ray);
        if (object && ((runtime_.heroes() && runtime_.heroes()->fromObject(*object)) ||
                       runtime_.navigation().towers().contains(*object))) {
            hoveredObject_ = object;
        }
    }
    // UI hover and camera gestures retain the last preview instead of recreating it.
    if (worldPointer) {
        runtime_.updatePlacement(engine_, ray, confirm);
    }
    if (commandCursor_ && !runtime_.placing()) {
        if (const auto commandRay = mouseRay(camera, *commandCursor_, logical, drawable)) {
            issueOrder(*commandRay);
        }
    }
    updateSelectionMarker();
    updateTowerPreview();
    updateHeroPathPreview(ray);
    placementOverlay(camera);
    heroMovementOverlay(camera);
    ImGui::Render();
}

void GameHud::updateHeroPathPreview(const std::optional<Ray>& ray) {
    const auto* hero = runtime_.selectedHero();
    const auto invalidate = [&] {
        hoverPath_.valid = false;
        hoverDestination_.reset();
        hoverRoute_.clear();
    };
    if (!hero || hero->health <= 0 || !hero->controllable || !ray || !runtime_.level().waves() || runtime_.placing() ||
        hoveredObject_) {
        invalidate();
        return;
    }
    const auto ground = runtime_.heroes()->ground(*ray);
    const auto& mesh = runtime_.heroes()->navigation(hero->type);
    if (!ground || !mesh.contains(*ground)) {
        invalidate();
        return;
    }
    if (runtime_.level().waves()->phase() == LevelPhase::battle && runtime_.navigation().enemies()) {
        auto visibleRay = *ray;
        visibleRay.maximum = std::min(ray->maximum, glm::length(*ground - ray->origin));
        if (HeroCombatSystem::pick(engine_, *runtime_.navigation().enemies(), visibleRay)) {
            invalidate();
            return;
        }
    }
    const bool changedHero = previewHero_ != hero->id || hoverPath_.revision != mesh.revision();
    const bool changedPosition = !hoverDestination_ || glm::distance(*ground, *hoverDestination_) > 0.02 ||
                                 glm::distance(hero->position, hoverOrigin_) > 0.12;
    const auto now = ImGui::GetTime();
    if (!changedHero && (!changedPosition || now - hoverQueryTime_ < 0.04)) {
        return;
    }
    previewHero_ = hero->id;
    if (changedHero) {
        hoverRoute_.clear();
    }
    hoverOrigin_ = hero->position;
    hoverDestination_ = ground;
    hoverQueryTime_ = now;
    (void)mesh.findPath(hero->position, *ground, hoverPath_);
}

void GameHud::clearTowerPreview() {
    if (towerDefenderPreview_) {
        engine_.removeInstance(*towerDefenderPreview_);
        towerDefenderPreview_.reset();
    }
    if (towerPreview_) {
        engine_.removeInstance(*towerPreview_);
        towerPreview_.reset();
    }
    towerPreviewType_.clear();
}

void GameHud::updateTowerPreview() {
    const auto& tower = runtime_.preview();
    if (!runtime_.placing() || !tower) {
        clearTowerPreview();
        return;
    }
    const auto& definition = runtime_.navigation().towers().definition(tower->type);
    if (!towerPreview_ || towerPreviewType_ != tower->type) {
        clearTowerPreview();
        try {
            towerPreview_ = engine_.createInstance(engine_.loadModel(engine_.assetRoot() / tower->model), true);
            if (!definition.operatorModel.empty()) {
                const auto model = engine_.loadModel(engine_.assetRoot() / definition.operatorModel);
                const auto info = engine_.modelInfo(model);
                if (info.maximum.y <= info.minimum.y) {
                    throw std::runtime_error("Tower preview defender needs a nonzero model height.");
                }
                const auto center = (info.minimum + info.maximum) * 0.5F;
                towerDefenderFoot_ = {center.x, info.minimum.y, center.z};
                towerDefenderScale_ = static_cast<float>(definition.operatorHeight / (info.maximum.y - info.minimum.y));
                towerDefenderPreview_ = engine_.createInstance(model, true);
                const auto clip =
                    std::find(info.animationNames.begin(), info.animationNames.end(), definition.operatorIdle);
                if (clip == info.animationNames.end()) {
                    throw std::runtime_error("Tower preview defender is missing its idle animation.");
                }
                engine_.animation(*towerDefenderPreview_)
                    .play(static_cast<std::size_t>(clip - info.animationNames.begin()));
            }
            towerPreviewType_ = tower->type;
        } catch (...) {
            clearTowerPreview();
            throw;
        }
    }
    const auto transform = glm::translate(glm::mat4{1}, glm::vec3{tower->position}) *
                           glm::mat4_cast(glm::quat{tower->rotation}) *
                           glm::scale(glm::mat4{1}, glm::vec3{tower->visualScale});
    engine_.setTransform(*towerPreview_, transform);
    const bool valid = runtime_.placementError().empty();
    const glm::vec4 tint = valid ? glm::vec4{0.65F, 1, 0.7F, 0.7F} : glm::vec4{1, 0.25F, 0.2F, 0.7F};
    engine_.setTint(*towerPreview_, tint);
    if (towerDefenderPreview_) {
        const auto offset = glm::vec3{definition.operatorOffset} - towerDefenderFoot_ * towerDefenderScale_;
        const auto defender =
            transform * glm::translate(glm::mat4{1}, offset) * glm::scale(glm::mat4{1}, glm::vec3{towerDefenderScale_});
        engine_.setTransform(*towerDefenderPreview_, defender);
        engine_.setTint(*towerDefenderPreview_, tint);
    }
}

void GameHud::clearSelectionMarker() {
    if (selectionMarker_) {
        engine_.removeInstance(*selectionMarker_);
        selectionMarker_.reset();
    }
}

void GameHud::heroMovementOverlay(const Camera& camera) {
    if (!runtime_.heroes()) {
        orderedRoutes_.clear();
        return;
    }
    glm::ivec2 drawable{};
    SDL_GetWindowSizeInPixels(native_.window, &drawable.x, &drawable.y);
    if (drawable.x <= 0 || drawable.y <= 0) {
        return;
    }
    const auto matrix =
        glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()};
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    const auto line = [&](glm::dvec3 from, glm::dvec3 to, double width, ImU32 color) {
        const glm::dvec2 direction{to.x - from.x, to.z - from.z};
        if (glm::length(direction) < 1e-6) {
            return;
        }
        const auto side = glm::normalize(glm::dvec3{-direction.y, 0, direction.x}) * width * 0.5;
        const glm::dvec3 lift{0, 0.065, 0};
        const std::array<glm::dvec3, 4> vertices{from + lift - side,
                                                 to + lift - side,
                                                 to + lift + side,
                                                 from + lift + side};
        std::array<ImVec2, 12> points;
        const auto count = ui::projectPolygon(matrix, vertices, viewport->Pos, viewport->Size, points);
        if (count) {
            draw->AddConvexPolyFilled(points.data(), count, color);
        }
    };
    std::erase_if(orderedRoutes_, [&](const auto& entry) {
        const auto* hero = runtime_.heroes()->find(entry.first);
        return !hero || hero->health <= 0 || !hero->combat.playerMoving || !hero->destination || !hero->path.valid;
    });
    for (const auto& movingHero : runtime_.heroes()->heroes()) {
        const auto* hero = &movingHero;
        if (hero->health <= 0) {
            continue;
        }
        const auto& mesh = runtime_.heroes()->navigation(hero->type);
        const auto arrows = [&](glm::dvec3 center, ImU32 color) {
            const auto surface = [&](glm::dvec3 position) {
                const auto projected = mesh.project(position, 0.15);
                return projected ? glm::dvec3{position.x, projected->position.y, position.z} : position;
            };
            for (int i = 0; i < 4; ++i) {
                const double angle = glm::half_pi<double>() * i;
                const glm::dvec3 direction{std::cos(angle), 0, std::sin(angle)};
                const glm::dvec3 side{-direction.z, 0, direction.x};
                const auto tip = surface(center + direction * 0.16);
                line(surface(center + direction * 0.4 + side * 0.15), tip, 0.075, color);
                line(tip, surface(center + direction * 0.4 - side * 0.15), 0.075, color);
            }
        };
        const bool hovering = runtime_.selectedHero() && runtime_.selectedHero()->id == hero->id && hoverPath_.valid &&
                              previewHero_ == hero->id;
        const bool moving = hero->combat.playerMoving && hero->destination && hero->path.valid;
        const auto* path = hovering ? &hoverPath_ : moving ? &hero->path : nullptr;
        if (path) {
            auto& visual = hovering ? hoverRoute_ : orderedRoutes_[hero->id];
            visual.update(mesh,
                          hero->position,
                          *path,
                          hero->avoidanceEnemy || hero->avoidanceHero ? std::optional{hero->avoidancePoint}
                                                                      : std::nullopt);
            for (const auto& dash : visual.dashes()) {
                line(dash.from, dash.to, 0.075, IM_COL32(255, 215, 40, 230));
            }
            if (hovering && (!moving || glm::distance(path->destination, *hero->destination) > 0.1)) {
                arrows(path->destination, IM_COL32(255, 215, 40, 150));
            }
        }
        if (moving) {
            arrows(*hero->destination, IM_COL32(255, 215, 40, 255));
        } else if (orderHero_ == hero->id && !orderAttack_ && orderPosition_ && orderRemaining_ > 0) {
            arrows(*orderPosition_, IM_COL32(255, 215, 40, 230));
        }
    }
}

void GameHud::updateSelectionMarker() {
    const auto* hero = runtime_.selectedHero();
    const auto* tower = runtime_.inspectedTower();
    if ((!hero || hero->health <= 0) && !tower) {
        clearSelectionMarker();
        return;
    }
    glm::dvec3 position;
    double radius = 0;
    const NavMesh* surface = nullptr;
    if (hero && hero->health > 0) {
        position = hero->position;
        radius = runtime_.heroes()->definition(*hero).radius + 0.16;
        surface = &runtime_.heroes()->navigation(hero->type);
    } else {
        position = tower->position - glm::dvec3{0, tower->halfExtent.y, 0};
        radius = std::max(tower->halfExtent.x, tower->halfExtent.z) + 0.16;
    }
    glm::vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    if (surface) {
        const auto x = surface->project(position + glm::dvec3{radius, 0, 0}, 0.25);
        const auto z = surface->project(position + glm::dvec3{0, 0, radius}, 0.25);
        if (x && z) {
            const auto normal = glm::cross(glm::vec3{z->position - position}, glm::vec3{x->position - position});
            if (glm::length(normal) > 1e-4F && glm::normalize(normal).y > 0.5F) {
                up = glm::normalize(normal);
                right = glm::normalize(glm::cross(up, glm::vec3{0, 0, 1}));
                forward = glm::normalize(glm::cross(right, up));
            }
        }
    }
    if (!selectionMarker_) {
        const auto model = engine_.loadModel(engine_.assetRoot() / "towers/structures/tower_defense/selection-a.glb");
        selectionMarker_ = engine_.createInstance(model, true);
    }
    glm::mat4 transform{1};
    transform[0] = glm::vec4{right * static_cast<float>(2 * radius), 0};
    transform[1] = glm::vec4{up, 0};
    transform[2] = glm::vec4{forward * static_cast<float>(2 * radius), 0};
    transform[3] = glm::vec4{glm::vec3{position} + up * 0.015F, 1};
    engine_.setTransform(*selectionMarker_, transform);
}

void GameHud::placementOverlay(const Camera& camera) {
    glm::ivec2 drawable{};
    SDL_GetWindowSizeInPixels(native_.window, &drawable.x, &drawable.y);
    if (drawable.x <= 0 || drawable.y <= 0) {
        return;
    }
    const auto matrix =
        glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()};
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    const auto healthBar =
        [&](glm::dvec3 position, float health, float maximum, double flash, bool alwaysVisible = false) {
            if (!worldHealthBars_ || (!alwaysVisible && health >= maximum)) {
                return;
            }
            const auto clip = matrix * glm::dvec4{position, 1};
            if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w) {
                return;
            }
            const ImVec2 point{viewport->Pos.x + static_cast<float>((clip.x / clip.w + 1) * viewport->Size.x * 0.5),
                               viewport->Pos.y + static_cast<float>((clip.y / clip.w + 1) * viewport->Size.y * 0.5)};
            const float s = layout_.scale;
            draw->AddRectFilled({point.x - 30 * s, point.y - 5 * s},
                                {point.x + 30 * s, point.y + 5 * s},
                                IM_COL32(8, 14, 18, 235),
                                3 * s);
            draw->AddRectFilled({point.x - 28 * s, point.y - 3 * s},
                                {point.x - 28 * s + 56 * s * std::clamp(health / maximum, 0.0F, 1.0F), point.y + 3 * s},
                                health / maximum < 0.3F ? IM_COL32(218, 100, 91, 255) : IM_COL32(102, 197, 164, 255),
                                2 * s);
            draw->AddRect({point.x - 30 * s, point.y - 5 * s},
                          {point.x + 30 * s, point.y + 5 * s},
                          IM_COL32(205, 185, 133, 130),
                          3 * s);
            if (flash > 0) {
                draw->AddRect({point.x - 31 * s, point.y - 6 * s},
                              {point.x + 31 * s, point.y + 6 * s},
                              IM_COL32(255, 240, 210, static_cast<int>(std::clamp(flash / 0.12, 0.0, 1.0) * 190)),
                              3 * s);
            }
        };
    for (const auto& tower : runtime_.navigation().towers().towers()) {
        const auto& progress = runtime_.navigation().towers().progress(tower.object);
        healthBar(tower.position + glm::dvec3{0, tower.halfExtent.y + 0.12, 0},
                  progress.health,
                  progress.maximumHealth,
                  progress.hitFlashRemaining);
    }
    const auto* selected = runtime_.selectedHero();
    if (const auto* enemies = runtime_.navigation().enemies()) {
        for (const auto& enemy : enemies->enemies()) {
            const auto& definition = enemies->definition(enemy.type);
            if (enemy.alive && (enemy.life == Enemy::Life::bones || definition.showHealthBar ||
                                (selected && selected->combat.target == enemy.id))) {
                const double barHeight =
                    enemy.life == Enemy::Life::bones ? 0.3 : definition.visualHeight + definition.hoverHeight + 0.12;
                healthBar(enemy.position + glm::dvec3{0, barHeight, 0},
                          enemy.health,
                          enemy.maximumHealth,
                          enemy.hitFlashRemaining,
                          enemy.life == Enemy::Life::bones);
            }
        }
    }
    const auto polygon = [&](std::span<const glm::dvec3> vertices, ImU32 color, bool outline = true) {
        std::array<ImVec2, 12> points;
        const auto count = ui::projectPolygon(matrix, vertices, viewport->Pos, viewport->Size, points);
        if (count) {
            draw->AddConvexPolyFilled(points.data(), count, color);
            if (outline) {
                draw->AddPolyline(points.data(), count, color | IM_COL32(0, 0, 0, 150), ImDrawFlags_Closed, 1.0F);
            }
        }
    };
    const auto ring = [&](glm::dvec3 position, double radius, ImU32 color) {
        const auto center = position + glm::dvec3{0, 0.04, 0};
        for (int i = 0; i < 32; ++i) {
            const double first = glm::two_pi<double>() * i / 32;
            const double second = glm::two_pi<double>() * (i + 1) / 32;
            const glm::dvec3 a{std::cos(first), 0, std::sin(first)};
            const glm::dvec3 b{std::cos(second), 0, std::sin(second)};
            const std::array<glm::dvec3, 4> vertices{center + a * radius,
                                                     center + b * radius,
                                                     center + b * (radius - 0.045),
                                                     center + a * (radius - 0.045)};
            polygon(vertices, color, false);
        }
    };
    if (runtime_.heroes()) {
        for (const auto& hero : runtime_.heroes()->heroes()) {
            const bool selectedHero = runtime_.selectedHero() && runtime_.selectedHero()->id == hero.id;
            if (!selectedHero && hoveredObject_ == hero.object) {
                ring(hero.position,
                     runtime_.heroes()->definition(hero).radius + 0.12,
                     selectedHero ? IM_COL32(90, 220, 180, 190) : IM_COL32(225, 210, 160, 130));
            }
        }
    }
    for (const auto& tower : runtime_.navigation().towers().towers()) {
        const bool selectedTower = runtime_.inspectedTower() && runtime_.inspectedTower()->object == tower.object;
        if (!selectedTower && hoveredObject_ == tower.object) {
            const auto center = tower.position - glm::dvec3{0, tower.halfExtent.y, 0};
            ring(center,
                 std::max(tower.halfExtent.x, tower.halfExtent.z) + 0.12,
                 selectedTower ? IM_COL32(90, 220, 180, 190) : IM_COL32(225, 210, 160, 130));
        }
    }
    if (orderAttack_ && orderPosition_ && orderRemaining_ > 0) {
        const auto center = *orderPosition_ + glm::dvec3{0, 0.03, 0};
        const std::array<glm::dvec3, 4> marker{center + glm::dvec3{-0.12, 0, 0},
                                               center + glm::dvec3{0, 0, 0.12},
                                               center + glm::dvec3{0.12, 0, 0},
                                               center + glm::dvec3{0, 0, -0.12}};
        polygon(marker, IM_COL32(240, 90, 65, 180));
    }
    if (const auto* hero = runtime_.selectedHero()) {
        const auto* enemy = hero->combat.target ? runtime_.navigation().enemies()->find(*hero->combat.target) : nullptr;
        if (enemy) {
            const auto center = enemy->position + glm::dvec3{0, 0.04, 0};
            const double radius = enemy->radius + 0.12;
            for (int i = 0; i < 32; ++i) {
                const double a = glm::two_pi<double>() * i / 32;
                const double b = glm::two_pi<double>() * (i + 1) / 32;
                const glm::dvec3 first{std::cos(a), 0, std::sin(a)}, second{std::cos(b), 0, std::sin(b)};
                const std::array<glm::dvec3, 4> vertices{center + first * radius,
                                                         center + second * radius,
                                                         center + second * (radius - 0.06),
                                                         center + first * (radius - 0.06)};
                polygon(vertices, IM_COL32(240, 90, 65, 170), false);
            }
        }
        if (hero->combat.feedbackRemaining > 0) {
            const auto& cleave = runtime_.heroes()->definition(*hero).combat.cleave;
            const auto center = hero->position + glm::dvec3{0, 0.05, 0};
            for (int i = 0; i < 20; ++i) {
                const double a = hero->yaw + glm::radians(cleave.angle) * (static_cast<double>(i) / 20 - 0.5);
                const double b = hero->yaw + glm::radians(cleave.angle) * (static_cast<double>(i + 1) / 20 - 0.5);
                const std::array<glm::dvec3, 3> vertices{
                    center,
                    center + glm::dvec3{std::sin(a), 0, std::cos(a)} * cleave.radius,
                    center + glm::dvec3{std::sin(b), 0, std::cos(b)} * cleave.radius};
                polygon(vertices, IM_COL32(240, 200, 100, 65), false);
            }
        }
    }
    const auto groundCircle = [&](glm::dvec3 center, double range, ImU32 color, ImU32 fill) {
        // Adjacent filled sectors share edges. Antialiasing each sector separately
        // adds translucent fringes along those internal edges and exposes the tessellation.
        const auto drawFlags = draw->Flags;
        draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        constexpr int segments = 160;
        for (int index = 0; index < segments; ++index) {
            const double a = glm::two_pi<double>() * index / segments;
            const double b = glm::two_pi<double>() * (index + 1) / segments;
            const glm::dvec3 first{std::cos(a), 0, std::sin(a)}, second{std::cos(b), 0, std::sin(b)};
            const std::array<glm::dvec3, 3> area{center, center + first * range, center + second * range};
            polygon(area, fill, false);
            const auto band = [&](double outer, double inner, ImU32 tint) {
                const std::array<glm::dvec3, 4> vertices{center + first * outer,
                                                         center + second * outer,
                                                         center + second * inner,
                                                         center + first * inner};
                polygon(vertices, tint, false);
            };
            band(range + 0.045, range - 0.16, IM_COL32(24, 22, 14, 220));
            band(range, range - 0.11, color);
        }
        draw->Flags = drawFlags;
    };
    for (const auto& zone : runtime_.navigation().placement().zones()) {
        if (!zone.occupants.empty()) {
            continue;
        }
        const bool placing = runtime_.placing();
        groundCircle(zone.constructionPosition() + glm::dvec3{0, 0.035, 0},
                     1,
                     IM_COL32(255, 218, 70, placing ? 245 : 205),
                     IM_COL32(255, 218, 70, placing ? 24 : 12));
    }
    const auto* rangeTower = runtime_.preview() ? &*runtime_.preview() : runtime_.inspectedTower();
    if (rangeTower) {
        const auto& towers = runtime_.navigation().towers();
        const auto level = runtime_.preview() ? 1 : towers.progress(rangeTower->object).level;
        const auto range = towers.definition(rangeTower->type).statistics(level).attackRange;
        const bool flashing = upgradedTower_ == rangeTower->object && upgradeFlashRemaining_ > 0;
        const bool valid = !runtime_.preview() || runtime_.placementError().empty();
        const auto color = valid ? IM_COL32(255, 218, 70, 245) : IM_COL32(255, 85, 65, 245);
        const auto fill = valid ? IM_COL32(255, 218, 70, flashing ? 36 : 18) : IM_COL32(255, 85, 65, 18);
        const auto center = rangeTower->position - glm::dvec3{0, rangeTower->halfExtent.y - 0.025, 0};
        groundCircle(center, range, color, fill);
    }
}

} // namespace grimlock::game
