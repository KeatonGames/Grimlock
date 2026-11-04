#include "ui/game_hud.hpp"

#include "ui/game_style.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/constants.hpp>
#include <grimlock/input.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace grimlock::game {

namespace {
constexpr ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
}

GameHud::GameHud(Engine& engine, LevelRuntime& runtime, bool startInMenu, const std::filesystem::path& initialLevel)
    : engine_(engine), runtime_(runtime) {
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
    engine_.detachOverlay();
}

void GameHud::configureUi() {
    presentation::configure();
}

void GameHud::updateLayout() {
    const auto size = ImGui::GetIO().DisplaySize;
    const float automatic = std::clamp(std::min(size.x / 1280, size.y / 720), 0.85F, 1.1F);
    const float scale = std::clamp(automatic * uiScale_, 0.75F, 1.25F);
    layout_.scale = scale;
    layout_.gap = 14 * scale;
    const bool saveWarning = runtime_.progress() && !runtime_.progress()->error().empty();
    layout_.top = (saveWarning ? 112 : 92) * scale;
    layout_.contextWidth = std::min(320 * scale, size.x * 0.42F);
    layout_.contextHeight = std::min(310 * scale, size.y - layout_.top - 3 * layout_.gap - 100 * scale);
    layout_.dockWidth = std::min(760 * scale, size.x - layout_.contextWidth - 3 * layout_.gap);
    const bool feedback = runtime_.placing() || runtime_.deployingHero() || !runtime_.heroError().empty();
    layout_.dockHeight = std::min((feedback ? 252 : 212) * scale, size.y * 0.48F);
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
        } else if (runtime_.placing() || runtime_.deployingHero()) {
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
    }
    towerInfo();
    heroControls();
    bossHealth();
    notifications();
    const bool overUi = io.WantCaptureMouse || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    viewportMouse_ = !runtime_.paused() && !relative && !overUi && !cancelWorldInput_;
    const bool commandsAllowed = !cancelWorldInput_ && !runtime_.placing() && !runtime_.deployingHero();
    commandCursor_ = commandGesture_.update(engine_.input(), commandsAllowed && (relative || !overUi));
    return {!relative && overUi, io.WantCaptureKeyboard, false, runtime_.placing() || runtime_.deployingHero()};
}

void GameHud::clearFeedback() {
    hoveredObject_.reset();
    orderPosition_.reset();
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

void GameHud::menu() {
    auto* viewport = ImGui::GetMainViewport();
    const float s = layout_.scale;
    presentation::backdrop();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F,
                             viewport->Pos.y + viewport->Size.y * 0.5F + (1 - entrance_) * 12 * s},
                            ImGuiCond_Always,
                            {0.5F, 0.5F});
    const bool selecting = screen_ == Screen::levels;
    ImGui::SetNextWindowSize({std::max(200.0F, std::min((selecting ? 720.0F : 480.0F) * s, viewport->Size.x - 32 * s)),
                              selecting ? std::max(220.0F, std::min(650 * s, viewport->Size.y - 32 * s)) : 340 * s});
    if (ImGui::Begin(selecting ? "Level Selection" : "Main Menu",
                     nullptr,
                     hudFlags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(nullptr, 32);
        ImGui::TextColored(presentation::gold, "GRIMLOCK");
        ImGui::PopFont();
        ImGui::TextDisabled(selecting ? "CHOOSE YOUR BATTLEFIELD" : "PREPARE. DEFEND. SURVIVE.");
        ImGui::Separator();
        ImGui::BeginDisabled(settingsOpen_);
        if (!selecting) {
            if (presentation::action("Choose Level", {-1, 48 * s}, true)) {
                requestedScreen_ = Screen::levels;
            }
            if (presentation::action("Settings", {-1, 42 * s})) {
                settingsOpen_ = true;
            }
            if (presentation::action("Quit", {-1, 42 * s})) {
                quit_ = true;
            }
        } else {
            refreshRemaining_ -= ImGui::GetIO().DeltaTime;
            if (refreshRemaining_ <= 0) {
                refreshLevels();
            }
            const bool saveWarning = runtime_.progress() && !runtime_.progress()->error().empty();
            const float footer = (110 + (!error_.empty() ? 28 : 0) + (saveWarning ? 28 : 0)) * s;
            if (ImGui::BeginChild("Available Levels", {0, -footer})) {
                if (catalog_.levels.empty()) {
                    ImGui::TextWrapped(
                        "No playable levels found. Save a level in the game's levels folder, then refresh.");
                }
                for (const auto& level : catalog_.levels) {
                    ImGui::PushID(level.path.generic_string().c_str());
                    const auto* progress = runtime_.progress();
                    const bool last = progress && !level.id.empty() && progress->lastPlayed() == level.id;
                    if (last) {
                        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.78F, 0.48F, 1});
                    }
                    if (ImGui::Selectable(level.name.c_str(), selectedLevel_ == level.path)) {
                        selectedLevel_ = level.path;
                        error_.clear();
                    }
                    if (last) {
                        ImGui::PopStyleColor();
                        ImGui::TextDisabled("Last played");
                    }
                    if (const auto* record = progress ? progress->find(level.id) : nullptr) {
                        if (record->completed) {
                            ImGui::TextDisabled("Completed | Best %.2f s", *record->bestSeconds);
                        }
                        ImGui::TextDisabled("Attempts %llu | Victories %llu",
                                            static_cast<unsigned long long>(record->attempts),
                                            static_cast<unsigned long long>(record->victories));
                    }
                    if (progress && level.id.empty()) {
                        ImGui::TextDisabled("Save in Editor to enable progress tracking");
                    }
                    if (!level.description.empty()) {
                        ImGui::TextWrapped("%s", level.description.c_str());
                    }
                    ImGui::Spacing();
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            if (!error_.empty()) {
                ImGui::TextColored({1, 0.55F, 0.45F, 1}, "Unable to load this level. Hover for details.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", error_.c_str());
                }
            }
            ImGui::BeginDisabled(!selectedLevel_);
            if (presentation::action("Play", {160, 36}, true)) {
                requestedLevel_ = selectedLevel_;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (presentation::action("Refresh", {100, 36})) {
                refreshLevels();
            }
            if (presentation::action("Main Menu")) {
                requestedScreen_ = Screen::mainMenu;
            }
            if (!catalog_.errors.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%zu unavailable", catalog_.errors.size());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(460);
                    for (const auto& error : catalog_.errors) {
                        ImGui::TextWrapped("%s", error.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndDisabled();
        progressNotice();
    }
    ImGui::End();
}

void GameHud::pauseMenu() {
    presentation::backdrop(0.65F);
    if (settingsOpen_) {
        settings();
        return;
    }
    auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * .5F, v->Pos.y + v->Size.y * .5F}, ImGuiCond_Always, {.5F, .5F});
    ImGui::SetNextWindowSize({std::min(420 * s, v->Size.x - 2 * layout_.gap), 0});
    if (ImGui::Begin("Pause Menu", nullptr, hudFlags)) {
        presentation::heading("PAUSED", "Your defense will wait.");
        ImGui::Separator();
        if (presentation::action("Resume", {-1, 38 * s}, true)) {
            runtime_.setPaused(false);
        }
        if (presentation::action("Settings", {-1, 34 * s})) {
            settingsOpen_ = true;
        }
        if (presentation::action("Restart Level", {-1, 34 * s})) {
            restart_ = true;
        }
        if (presentation::action("Choose Level", {-1, 34 * s})) {
            requestedScreen_ = Screen::levels;
        }
        if (presentation::action("Main Menu", {-1, 34 * s})) {
            requestedScreen_ = Screen::mainMenu;
        }
        if (presentation::action("Quit", {-1, 34 * s})) {
            quit_ = true;
        }
    }
    ImGui::End();
}

void GameHud::settings() {
    auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x * .5F, v->Pos.y + v->Size.y * .5F}, ImGuiCond_Always, {.5F, .5F});
    ImGui::SetNextWindowSize({std::min(450 * s, v->Size.x - 2 * layout_.gap), 0});
    if (ImGui::Begin("Settings", nullptr, hudFlags)) {
        presentation::heading("SETTINGS", "Interface preferences for this session");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-100 * s);
        ImGui::SliderFloat("UI Scale", &uiScale_, 0.85F, 1.25F, "%.2fx");
        ImGui::Checkbox("World health bars", &worldHealthBars_);
        ImGui::TextDisabled("Boss health is always visible during an encounter.");
        ImGui::Separator();
        ImGui::TextWrapped("Right-click: hero command\nRight drag: pan\nMiddle drag: orbit\nWheel: zoom\nEscape: "
                           "cancel placement/selection, then pause");
        if (presentation::action("Back", {-1, 36 * s}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            settingsOpen_ = false;
        }
    }
    ImGui::End();
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
            notification_ =
                "Wave cleared | +" + std::to_string(runtime_.level().lastWaveReward().value_or(0)) + " gold";
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
    const float width = std::min(layout_.dockWidth, 410 * layout_.scale);
    const float alpha = std::min(1.0F, notificationRemaining_) * 0.95F;
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::SetNextWindowPos(
        {v->Pos.x + layout_.gap, v->Pos.y + v->Size.y - layout_.gap - layout_.dockHeight - 42 * layout_.scale});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    if (ImGui::Begin("Battle Notice",
                     nullptr,
                     hudFlags | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextColored(presentation::gold, "%s", notification_.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void GameHud::status() {
    const auto* waves = runtime_.level().waves();
    if (!waves) {
        return;
    }
    const auto* v = ImGui::GetMainViewport();
    const auto& def = waves->definition();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + layout_.gap, v->Pos.y + layout_.gap});
    ImGui::SetNextWindowSize({v->Size.x - 2 * layout_.gap, layout_.top});
    if (ImGui::Begin("Game Status", nullptr, hudFlags & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("Status metrics", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Wave", ImGuiTableColumnFlags_WidthStretch, 0.34F);
            ImGui::TableSetupColumn("Objective", ImGuiTableColumnFlags_WidthStretch, 0.34F);
            ImGui::TableSetupColumn("Gold", ImGuiTableColumnFlags_WidthStretch, 0.20F);
            ImGui::TableSetupColumn("Menu", ImGuiTableColumnFlags_WidthStretch, 0.12F);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", waves->phase() == LevelPhase::preparation ? "PREPARATION" : "BATTLE");
            ImGui::Text("Wave %zu / %zu", std::min(waves->wave() + 1, def.waves.size()), def.waves.size());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(def.name.c_str());
                const auto& wave = def.waves[std::min(waves->wave(), def.waves.size() - 1)];
                ImGui::TextUnformatted(wave.name.c_str());
                for (const auto& group : wave.groups) {
                    ImGui::Text("%zu %s", group.count, group.enemy.c_str());
                }
                ImGui::Text("Reward: %llu gold", static_cast<unsigned long long>(wave.goldReward));
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("DEFENSE OBJECTIVE");
            char health[64];
            std::snprintf(health,
                          sizeof(health),
                          "%llu / %llu",
                          static_cast<unsigned long long>(waves->health()),
                          static_cast<unsigned long long>(def.objectiveHealth));
            presentation::meter(static_cast<float>(waves->health()) / def.objectiveHealth, {-1, 22 * s}, health);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("GOLD");
            ImGui::TextColored(presentation::gold,
                               "%llu",
                               static_cast<unsigned long long>(runtime_.level().economy().gold()));
            if (runtime_.progress() && !runtime_.progress()->error().empty()) {
                ImGui::TextColored(presentation::red, "Save unavailable");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", runtime_.progress()->error().c_str());
                }
            }
            ImGui::TableNextColumn();
            if (presentation::action("Pause", {-1, 40 * s})) {
                runtime_.setPaused(true);
                SDL_SetWindowRelativeMouseMode(native_.window, false);
            }
            ImGui::EndTable();
        }
    }
    statusSize_ = {ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
    ImGui::End();
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
    const bool contextual = runtime_.selectedHero() || runtime_.inspectedTower();
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

void GameHud::construction() {
    const auto* waves = runtime_.level().waves();
    const bool preparing = waves && waves->phase() == LevelPhase::preparation;
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + layout_.gap, v->Pos.y + v->Size.y - layout_.gap}, ImGuiCond_Always, {0, 1});
    ImGui::SetNextWindowSize({layout_.dockWidth, layout_.dockHeight});
    const auto flags = (hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar));
    if (ImGui::Begin("Build Towers", nullptr, flags)) {
        ImGui::TextDisabled(preparing ? "BUILD YOUR DEFENSE" : "DEFENSE COMMAND");
        const float cardWidth =
            std::min(168 * s, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F);
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
                                        {cardWidth, 62 * s})) {
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
        if (runtime_.heroes()) {
            for (const auto& hero : runtime_.heroes()->heroes()) {
                if (ImGui::GetContentRegionAvail().x < 130 * s) {
                    ImGui::NewLine();
                }
                ImGui::PushID(static_cast<int>(hero.id));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.85F, 0.5F});
                const bool selected = runtime_.selectedHero() && runtime_.selectedHero()->id == hero.id;
                if (presentation::action(runtime_.heroes()->definition(hero).name.c_str(),
                                         {130 * s, 30 * s},
                                         selected)) {
                    (void)runtime_.selectHero(hero.id);
                }
                ImGui::PopStyleVar();
                const auto p = ImGui::GetItemRectMin();
                presentation::icon(presentation::Icon::hero,
                                   {p.x + 20 * s, p.y + 15 * s},
                                   22 * s,
                                   ImGui::ColorConvertFloat4ToU32(presentation::gold));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Select hero | Health %.0f / %.0f", hero.health, hero.maximumHealth);
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
        }
        ImGui::BeginDisabled(!preparing);
        if (ImGui::GetContentRegionAvail().x < 248 * s) {
            ImGui::NewLine();
        }
        if (presentation::action("Deploy Hero", {110 * s, 30 * s})) {
            runtime_.beginHeroPlacement(heroType_);
        }
        if (runtime_.heroes()) {
            ImGui::SameLine();
            const auto& heroes = *runtime_.heroes();
            ImGui::SetNextItemWidth(130 * s);
            if (ImGui::BeginCombo("##heroType", heroes.definition(heroType_).name.c_str())) {
                for (std::size_t type = 0; type < heroes.typeCount(); ++type) {
                    if (ImGui::Selectable(heroes.definition(type).name.c_str(), heroType_ == type)) {
                        heroType_ = type;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::EndDisabled();
        if (runtime_.placing() || runtime_.deployingHero()) {
            if (presentation::action(runtime_.placing() ? "Cancel Placement" : "Cancel Deployment", {-1, 28 * s})) {
                runtime_.cancelPlacement();
            }
            const auto& reason = runtime_.placing() ? runtime_.placementError() : runtime_.heroError();
            if (!reason.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, presentation::red);
                ImGui::TextWrapped("%s", reason.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("Left-click to confirm | Esc to cancel");
            }
        } else if (preparing) {
            if (presentation::action("Start Wave", {-1, 34 * s}, true)) {
                runtime_.startWave();
            }
        } else {
            const auto active = runtime_.navigation().enemies()->enemies().size();
            ImGui::TextDisabled("%zu active | %zu arriving", active, waves->pending());
        }
        if (!runtime_.placing() && !runtime_.deployingHero() && !runtime_.heroError().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, presentation::red);
            ImGui::TextWrapped("%s", runtime_.heroError().c_str());
            ImGui::PopStyleColor();
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
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x - layout_.gap, v->Pos.y + v->Size.y - layout_.gap},
                            ImGuiCond_Always,
                            {1, 1});
    ImGui::SetNextWindowSize({layout_.contextWidth, layout_.contextHeight});
    if (ImGui::Begin("Tower Info",
                     nullptr,
                     (hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)))) {
        presentation::heading(def.name.c_str());
        ImGui::TextDisabled("LEVEL %zu / %zu", progress.level, def.levels.size());
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
            if (presentation::action("Upgrade", {-1, 34 * s}, true)) {
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
    const auto* waves = runtime_.level().waves();
    if (!resultCatalogReady_) {
        refreshLevels();
        resultCatalogReady_ = true;
    }
    auto* viewport = ImGui::GetMainViewport();
    presentation::backdrop(0.72F);
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
                            ImGuiCond_Always,
                            {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::min(500 * layout_.scale, viewport->Size.x - 2 * layout_.gap), 0});
    if (ImGui::Begin("Level Result", nullptr, hudFlags)) {
        presentation::heading(waves->phase() == LevelPhase::victory ? "VICTORY" : "DEFEAT");
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
        if (presentation::action("Retry", {-1, 38 * layout_.scale}, true)) {
            restart_ = true;
        }
        if (waves->phase() == LevelPhase::victory) {
            const auto current =
                std::find_if(catalog_.levels.begin(), catalog_.levels.end(), [&](const AvailableLevel& level) {
                    return !waves->definition().id.empty() ? level.id == waves->definition().id
                                                           : level.path == runtime_.path();
                });
            if (current != catalog_.levels.end() && std::next(current) != catalog_.levels.end()) {
                if (presentation::action("Next Level", {-1, 38 * layout_.scale})) {
                    requestedLevel_ = std::next(current)->path;
                }
            }
        }
        if (presentation::action("Choose Level", {-1, 38 * layout_.scale})) {
            requestedScreen_ = Screen::levels;
        }
    }
    ImGui::End();
}

void GameHud::heroControls() {
    const auto* hero = runtime_.selectedHero();
    if (!hero) {
        return;
    }
    const auto* v = ImGui::GetMainViewport();
    const float s = layout_.scale;
    ImGui::SetNextWindowPos({v->Pos.x + v->Size.x - layout_.gap, v->Pos.y + v->Size.y - layout_.gap},
                            ImGuiCond_Always,
                            {1, 1});
    ImGui::SetNextWindowSize({layout_.contextWidth, layout_.contextHeight});
    if (ImGui::Begin("Hero Info",
                     nullptr,
                     (hudFlags & ~(ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)))) {
        presentation::heading(runtime_.heroes()->definition(*hero).name.c_str());
        char health[64];
        std::snprintf(health, sizeof(health), "%.0f / %.0f", hero->health, hero->maximumHealth);
        presentation::meter(hero->health / hero->maximumHealth, {-1, 18 * s}, health);
        const char* state = hero->health <= 0                   ? "Fallen"
                            : hero->state == HeroState::blocked ? "Destination unreachable"
                            : hero->combat.playerMoving         ? "Following movement order"
                            : hero->combat.explicitTarget       ? "Attacking ordered target"
                            : hero->combat.returning            ? "Returning to position"
                                                                : "Defending this position";
        ImGui::TextColored(presentation::gold, "%s", state);
        const auto& stats = runtime_.heroes()->definition(*hero).combat;
        ImGui::TextDisabled("DMG %.0f | RNG %.1f | %.1f/s", stats.damage, stats.range, 1 / stats.interval);
        if (const auto* target =
                hero->combat.target ? runtime_.navigation().enemies()->find(*hero->combat.target) : nullptr) {
            ImGui::Text("Target: %s", runtime_.navigation().enemies()->definition(target->type).name.c_str());
        }
        ImGui::BeginDisabled(hero->health <= 0);
        if (presentation::action("Cancel Order", {-1, 28 * s})) {
            runtime_.cancelHeroOrder();
        }
        ImGui::EndDisabled();
        if (stats.attack == HeroAttack::melee) {
            const bool battle = runtime_.level().waves()->phase() == LevelPhase::battle;
            ImGui::BeginDisabled(!battle || hero->health <= 0 || hero->combat.cleaveCooldown > 1e-10 ||
                                 (hero->combat.swinging && hero->combat.abilitySwing));
            if (presentation::action("Cleave", {-1, 34 * s}, true)) {
                (void)runtime_.activateHeroAbility(HeroAbility::cleave);
            }
            const auto abilityPosition = ImGui::GetItemRectMin();
            auto abilityColor = presentation::gold;
            abilityColor.w = ImGui::GetStyle().Alpha;
            presentation::icon(presentation::Icon::sword,
                               {abilityPosition.x + 18 * s, abilityPosition.y + 17 * s},
                               20 * s,
                               ImGui::ColorConvertFloat4ToU32(abilityColor));
            ImGui::EndDisabled();
            char cooldown[64];
            std::snprintf(cooldown,
                          sizeof(cooldown),
                          hero->combat.cleaveCooldown > 0 ? "%.1f s remaining" : "Ready | %.0f damage",
                          hero->combat.cleaveCooldown > 0 ? hero->combat.cleaveCooldown : stats.cleave.damage);
            presentation::meter(1 - static_cast<float>(hero->combat.cleaveCooldown / stats.cleave.cooldown),
                                {-1, 16 * s},
                                cooldown,
                                presentation::gold);
        }
        if (!runtime_.heroError().empty()) {
            ImGui::TextWrapped("%s", runtime_.heroError().c_str());
        }
    }
    ImGui::End();
}

void GameHud::finishFrame(const Camera& camera) {
    ImGui::SetCurrentContext(context_);
    if (modalFrame_ || screen_ != Screen::gameplay || runtime_.paused() ||
        (runtime_.level().waves() && (runtime_.level().waves()->phase() == LevelPhase::victory ||
                                      runtime_.level().waves()->phase() == LevelPhase::defeat))) {
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
    if (!runtime_.placing() && !runtime_.deployingHero() && confirm) {
        runtime_.selectObject(engine_, ray);
    }
    hoveredObject_.reset();
    if (ray && !runtime_.placing() && !runtime_.deployingHero()) {
        const auto object = engine_.pickObject(*ray);
        if (object && ((runtime_.heroes() && runtime_.heroes()->fromObject(*object)) ||
                       runtime_.navigation().towers().contains(*object))) {
            hoveredObject_ = object;
        }
    }
    // UI hover and camera gestures retain the last preview instead of recreating it.
    if (worldPointer) {
        runtime_.updatePlacement(engine_, ray, confirm);
        runtime_.updateHeroPlacement(engine_, ray, confirm);
    }
    if (commandCursor_ && !runtime_.placing() && !runtime_.deployingHero()) {
        if (const auto commandRay = mouseRay(camera, *commandCursor_, logical, drawable)) {
            if (runtime_.commandHero(*commandRay)) {
                const auto* hero = runtime_.selectedHero();
                const auto* enemy =
                    hero->combat.target ? runtime_.navigation().enemies()->find(*hero->combat.target) : nullptr;
                orderAttack_ = enemy != nullptr;
                orderPosition_ = enemy ? std::optional{enemy->position} : hero->destination;
                orderRemaining_ = 0.8F;
            }
        }
    }
    placementOverlay(camera);
    ImGui::Render();
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
    const auto healthBar = [&](glm::dvec3 position, float health, float maximum, double flash) {
        if (!worldHealthBars_ || health >= maximum) {
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
    if (runtime_.heroes()) {
        for (const auto& hero : runtime_.heroes()->heroes()) {
            healthBar(hero.position + glm::dvec3{0, runtime_.heroes()->definition(hero).visualHeight + 0.12, 0},
                      hero.health,
                      hero.maximumHealth,
                      hero.hitFlashRemaining);
        }
    }
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
            if (definition.showHealthBar || (selected && selected->combat.target == enemy.id)) {
                healthBar(enemy.position + glm::dvec3{0, definition.visualHeight + 0.12, 0},
                          enemy.health,
                          enemy.maximumHealth,
                          enemy.hitFlashRemaining);
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
            if (selectedHero || hoveredObject_ == hero.object) {
                ring(hero.position,
                     runtime_.heroes()->definition(hero).radius + 0.12,
                     selectedHero ? IM_COL32(90, 220, 180, 190) : IM_COL32(225, 210, 160, 130));
            }
        }
    }
    for (const auto& tower : runtime_.navigation().towers().towers()) {
        const bool selectedTower = runtime_.inspectedTower() && runtime_.inspectedTower()->object == tower.object;
        if (selectedTower || hoveredObject_ == tower.object) {
            const auto center = tower.position - glm::dvec3{0, tower.halfExtent.y, 0};
            ring(center,
                 std::max(tower.halfExtent.x, tower.halfExtent.z) + 0.12,
                 selectedTower ? IM_COL32(90, 220, 180, 190) : IM_COL32(225, 210, 160, 130));
        }
    }
    if (orderPosition_ && orderRemaining_ > 0) {
        const auto center = *orderPosition_ + glm::dvec3{0, 0.03, 0};
        const std::array<glm::dvec3, 4> marker{center + glm::dvec3{-0.12, 0, 0},
                                               center + glm::dvec3{0, 0, 0.12},
                                               center + glm::dvec3{0.12, 0, 0},
                                               center + glm::dvec3{0, 0, -0.12}};
        polygon(marker, orderAttack_ ? IM_COL32(240, 90, 65, 180) : IM_COL32(70, 200, 185, 180));
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
    if (runtime_.placing()) {
        for (const auto& zone : runtime_.navigation().placement().zones()) {
            const auto corners = zone.corners();
            const std::array<glm::dvec3, 4> vertices{corners[0], corners[1], corners[2], corners[3]};
            polygon(vertices, IM_COL32(35, 130, 90, 35));
        }
    }
    const auto* rangeTower = runtime_.preview() ? &*runtime_.preview() : runtime_.inspectedTower();
    if (rangeTower) {
        const auto& towers = runtime_.navigation().towers();
        const auto level = runtime_.preview() ? 1 : towers.progress(rangeTower->object).level;
        const auto range = towers.definition(rangeTower->type).statistics(level).attackRange;
        const bool flashing = upgradedTower_ == rangeTower->object && upgradeFlashRemaining_ > 0;
        const auto color = flashing ? IM_COL32(235, 195, 85, 130) : IM_COL32(115, 205, 200, 75);
        const auto center = rangeTower->position - glm::dvec3{0, rangeTower->halfExtent.y - 0.025, 0};
        constexpr int segments = 80;
        for (int index = 0; index < segments; ++index) {
            const double a = glm::two_pi<double>() * index / segments;
            const double b = glm::two_pi<double>() * (index + 1) / segments;
            const glm::dvec3 first{std::cos(a), 0, std::sin(a)}, second{std::cos(b), 0, std::sin(b)};
            const std::array<glm::dvec3, 4> vertices{center + first * range,
                                                     center + second * range,
                                                     center + second * (range - 0.035),
                                                     center + first * (range - 0.035)};
            polygon(vertices, color, false);
        }
    }
    if (const auto& tower = runtime_.preview()) {
        std::array<glm::dvec3, 8> corners;
        for (int i = 0; i < 8; ++i) {
            const glm::dvec3 local{(i & 1 ? 1 : -1) * tower->halfExtent.x,
                                   (i & 2 ? 1 : -1) * tower->halfExtent.y,
                                   (i & 4 ? 1 : -1) * tower->halfExtent.z};
            corners[i] = tower->position + tower->rotation * local;
        }
        constexpr std::array<std::array<int, 4>, 6> faces{
            {{0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}, {0, 4, 6, 2}, {1, 3, 7, 5}}};
        const bool valid = runtime_.placementError().empty();
        const int alpha = 45 + static_cast<int>(10 * (0.5 + 0.5 * std::sin(ImGui::GetTime() * 4)));
        const auto color = valid ? IM_COL32(92, 216, 171, alpha) : IM_COL32(235, 100, 91, alpha);
        for (const auto& face : faces) {
            const std::array<glm::dvec3, 4> vertices{corners[face[0]],
                                                     corners[face[1]],
                                                     corners[face[2]],
                                                     corners[face[3]]};
            polygon(vertices, color);
        }
    }
}

} // namespace grimlock::game
