#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <grimlock/input.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace grimlock::game {

namespace {
constexpr ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
}

GameHud::GameHud(Engine& engine, LevelRuntime& runtime) : engine_(engine), runtime_(runtime) {
    engine_.attachOverlay(*this);
}

GameHud::~GameHud() {
    engine_.detachOverlay();
}

void GameHud::configureUi() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.FontSizeBase = 16;
    ImFontConfig font;
    font.SizePixels = 16;
    ImGui::GetIO().Fonts->AddFontDefaultVector(&font);
    style.WindowRounding = 9;
    style.FrameRounding = 5;
    style.WindowPadding = {16, 12};
    style.FramePadding = {12, 8};
    style.ItemSpacing = {10, 8};
    style.WindowBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = {0.018F, 0.025F, 0.033F, 0.95F};
    style.Colors[ImGuiCol_Button] = {0.035F, 0.12F, 0.14F, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.06F, 0.20F, 0.23F, 1};
    style.Colors[ImGuiCol_ButtonActive] = {0.08F, 0.25F, 0.28F, 1};
}

OverlayInput GameHud::frame() {
    ImGui::SetCurrentContext(context_);
    if (restart_) {
        restart_ = false;
        upgradedTower_.reset();
        upgradeFlashRemaining_ = 0;
        try {
            runtime_.restart(engine_);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
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
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    upgradeFlashRemaining_ = std::max(0.0, upgradeFlashRemaining_ - io.DeltaTime);
    status();
    const auto* waves = runtime_.level().waves();
    if (waves && (waves->phase() == LevelPhase::victory || waves->phase() == LevelPhase::defeat)) {
        result();
    } else if (waves) {
        construction();
    }
    towerInfo();
    heroControls();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        runtime_.cancelPlacement();
    }
    viewportMouse_ = !relative && !io.WantCaptureMouse;
    commandCursor_ = commandGesture_.update(engine_.input(), relative || !io.WantCaptureMouse);
    return {!relative && io.WantCaptureMouse,
            io.WantCaptureKeyboard,
            false,
            runtime_.placing() || runtime_.deployingHero()};
}

void GameHud::status() {
    const auto* waves = runtime_.level().waves();
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + 18, viewport->Pos.y + 18});
    if (ImGui::Begin("Game Status", nullptr, hudFlags)) {
        if (waves) {
            const auto& definition = waves->definition();
            ImGui::TextUnformatted(definition.name.c_str());
            ImGui::Text("%s  |  Wave %zu / %zu",
                        levelPhaseName(waves->phase()),
                        std::min(waves->wave() + 1, definition.waves.size()),
                        definition.waves.size());
            ImGui::Text("Gold  %llu     Objective  %llu / %llu",
                        static_cast<unsigned long long>(runtime_.level().economy().gold()),
                        static_cast<unsigned long long>(waves->health()),
                        static_cast<unsigned long long>(definition.objectiveHealth));
            if (const auto reward = runtime_.level().lastWaveReward()) {
                ImGui::Text("Wave completed: +%llu gold", static_cast<unsigned long long>(*reward));
            }
            if (waves->phase() == LevelPhase::preparation && ImGui::Button("Start Wave")) {
                runtime_.startWave();
            }
        }
        if (ImGui::Button("Restart Level")) {
            restart_ = true;
        }
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
        }
    }
    ImGui::End();
}

void GameHud::construction() {
    const auto* waves = runtime_.level().waves();
    const bool preparing = waves && waves->phase() == LevelPhase::preparation;
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + 18, viewport->Pos.y + viewport->Size.y - 18}, ImGuiCond_Always, {0, 1});
    if (ImGui::Begin("Build Towers", nullptr, hudFlags)) {
        ImGui::TextUnformatted(preparing ? "Prepare your defenses" : "Battle in progress");
        ImGui::BeginDisabled(!preparing);
        if (ImGui::Button("Deploy Hero")) {
            runtime_.beginHeroPlacement();
        }
        ImGui::EndDisabled();
        if (runtime_.deployingHero()) {
            ImGui::SameLine();
            if (ImGui::Button("Cancel Deployment")) {
                runtime_.cancelPlacement();
            }
            ImGui::TextUnformatted("Left-click clear ground to deploy or reposition | Escape cancels");
        }
        if (!runtime_.heroError().empty()) {
            ImGui::TextUnformatted(runtime_.heroError().c_str());
        }
        for (const auto& definition : runtime_.navigation().towers().definitions()) {
            const auto& statistics = definition.statistics();
            ImGui::PushID(definition.id.c_str());
            const bool affordable = runtime_.level().economy().canAfford(definition.constructionCost);
            ImGui::BeginDisabled(!preparing || !affordable);
            if (ImGui::Button(definition.name.c_str())) {
                runtime_.beginPlacement(definition.id);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("%llu gold%s",
                        static_cast<unsigned long long>(definition.constructionCost),
                        affordable ? "" : " - insufficient gold");
            ImGui::Text("Damage %.0f  |  Range %.1f  |  %.1f shots/s",
                        statistics.damage,
                        statistics.attackRange,
                        statistics.fireRate);
            ImGui::PopID();
        }
        if (runtime_.placing()) {
            if (ImGui::Button("Cancel Placement")) {
                runtime_.cancelPlacement();
            }
            ImGui::TextUnformatted("Left-click to build | Escape to cancel");
            if (!runtime_.placementError().empty()) {
                ImGui::TextWrapped("%s", runtime_.placementError().c_str());
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
    const auto& definition = towers.definition(tower->type);
    const auto level = towers.progress(object).level;
    const auto& current = definition.statistics(level);
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - 18, viewport->Pos.y + 18}, ImGuiCond_Always, {1, 0});
    if (ImGui::Begin("Tower Info", nullptr, hudFlags)) {
        ImGui::Text("%s  |  Level %zu / %zu", definition.name.c_str(), level, definition.levels.size());
        ImGui::Text("Health %.0f / %.0f", towers.progress(object).health, towers.progress(object).maximumHealth);
        ImGui::Text("Damage %.0f  |  Range %.1f  |  %.1f shots/s",
                    current.damage,
                    current.attackRange,
                    current.fireRate);
        if (level < definition.levels.size()) {
            const auto& next = definition.statistics(level + 1);
            ImGui::Separator();
            ImGui::Text("Next: Level %zu  |  %llu gold", level + 1, static_cast<unsigned long long>(next.upgradeCost));
            ImGui::Text("Damage %.0f (%+.0f)", next.damage, next.damage - current.damage);
            ImGui::Text("Range %.1f (%+.1f)  |  %.1f shots/s (%+.1f)",
                        next.attackRange,
                        next.attackRange - current.attackRange,
                        next.fireRate,
                        next.fireRate - current.fireRate);
        } else {
            ImGui::TextColored({0.85F, 0.72F, 0.35F, 1}, "MAX LEVEL");
        }
        const auto error = runtime_.level().upgradeError(runtime_.navigation(), object);
        ImGui::BeginDisabled(!error.empty());
        if (ImGui::Button("Upgrade")) {
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
        if (!error.empty() && error != "MAX LEVEL") {
            ImGui::TextUnformatted(error.c_str());
        }
        if (upgradedTower_ == object && upgradeFlashRemaining_ > 0) {
            ImGui::TextColored({0.9F, 0.8F, 0.4F, 1}, "Upgraded!");
        }
    }
    ImGui::End();
}

void GameHud::result() {
    const auto* waves = runtime_.level().waves();
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + viewport->Size.y * 0.5F},
                            ImGuiCond_Always,
                            {0.5F, 0.5F});
    if (ImGui::Begin("Level Result", nullptr, hudFlags)) {
        ImGui::TextUnformatted(waves->phase() == LevelPhase::victory ? "VICTORY" : "DEFEAT");
        ImGui::TextUnformatted(waves->phase() == LevelPhase::victory ? "Your objective survived every wave."
                                                                     : "Your defense objective has fallen.");
        if (ImGui::Button("Play Again")) {
            restart_ = true;
        }
    }
    ImGui::End();
}

void GameHud::heroControls() {
    const auto* hero = runtime_.selectedHero();
    if (!hero) {
        return;
    }
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - 18, viewport->Pos.y + 18}, ImGuiCond_Always, {1, 0});
    if (ImGui::Begin("Hero Info", nullptr, hudFlags)) {
        ImGui::TextUnformatted(runtime_.heroes()->definition().name.c_str());
        ImGui::Text("Health %.0f / %.0f", hero->health, hero->maximumHealth);
        ImGui::TextUnformatted(hero->health <= 0             ? "Dead"
                               : hero->combat.playerMoving   ? "Manual move"
                               : hero->combat.explicitTarget ? "Manual attack"
                               : hero->combat.returning      ? "Returning to defense"
                                                             : "Automatic defense");
        ImGui::BeginDisabled(hero->health <= 0);
        if (ImGui::Button("Cancel Order")) {
            runtime_.cancelHeroOrder();
        }
        ImGui::EndDisabled();
        const auto& combat = runtime_.heroes()->definition().combat;
        ImGui::Text("Damage %.0f | Range %.1f | %.2f attacks/s", combat.damage, combat.range, 1 / combat.interval);
        if (const auto* enemy =
                hero->combat.target ? runtime_.navigation().enemies()->find(*hero->combat.target) : nullptr) {
            ImGui::Text("Target: %s #%llu (%.0f health)",
                        runtime_.navigation().enemies()->definition(enemy->type).name.c_str(),
                        static_cast<unsigned long long>(enemy->id),
                        enemy->health);
        } else {
            ImGui::TextUnformatted("Target: none");
        }
        const bool battle = runtime_.level().waves() && runtime_.level().waves()->phase() == LevelPhase::battle;
        ImGui::BeginDisabled(!battle || hero->health <= 0 || hero->combat.cleaveCooldown > 1e-10 ||
                             hero->combat.swinging);
        if (ImGui::Button("Cleave")) {
            (void)runtime_.activateHeroAbility(HeroAbility::cleave);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%.1fs | %.0f damage", hero->combat.cleaveCooldown, combat.cleave.damage);
        ImGui::TextUnformatted(hero->state == HeroState::moving    ? "Moving"
                               : hero->state == HeroState::blocked ? "Unreachable"
                                                                   : "Stationary");
        ImGui::TextUnformatted("Battle: right-click enemy to attack, ground to move");
        ImGui::TextUnformatted("Right drag: pan | Middle drag: orbit | Wheel: zoom");
        if (!runtime_.heroError().empty()) {
            ImGui::TextUnformatted(runtime_.heroError().c_str());
        }
    }
    ImGui::End();
}

void GameHud::finishFrame(const Camera& camera) {
    ImGui::SetCurrentContext(context_);
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
    if (viewportMouse_ && input.focused && !busy(MouseButton::right) && !busy(MouseButton::middle) &&
        input.mouseWheel == glm::vec2{0}) {
        ray = mouseRay(camera, input.mousePosition, logical, drawable);
        if (input.button(MouseButton::left).pressed && input.leftPressPosition) {
            ray = mouseRay(camera, *input.leftPressPosition, logical, drawable);
            confirm = true;
        }
    }
    if (!runtime_.placing() && !runtime_.deployingHero() && confirm) {
        runtime_.selectObject(engine_, ray);
    }
    runtime_.updatePlacement(engine_, ray, confirm);
    runtime_.updateHeroPlacement(engine_, ray, confirm);
    if (commandCursor_ && !runtime_.placing() && !runtime_.deployingHero()) {
        if (const auto commandRay = mouseRay(camera, *commandCursor_, logical, drawable)) {
            (void)runtime_.commandHero(*commandRay);
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
    const auto healthBar = [&](glm::dvec3 position, float health, float maximum) {
        if (health >= maximum) {
            return;
        }
        const auto clip = matrix * glm::dvec4{position, 1};
        if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w) {
            return;
        }
        const ImVec2 point{viewport->Pos.x + static_cast<float>((clip.x / clip.w + 1) * viewport->Size.x * 0.5),
                           viewport->Pos.y + static_cast<float>((clip.y / clip.w + 1) * viewport->Size.y * 0.5)};
        draw->AddRectFilled({point.x - 23, point.y - 3}, {point.x + 23, point.y + 3}, IM_COL32(25, 25, 25, 210), 2);
        draw->AddRectFilled({point.x - 22, point.y - 2},
                            {point.x - 22 + 44 * std::clamp(health / maximum, 0.0F, 1.0F), point.y + 2},
                            IM_COL32(105, 200, 130, 230),
                            1);
    };
    if (runtime_.heroes()) {
        for (const auto& hero : runtime_.heroes()->heroes()) {
            healthBar(hero.position + glm::dvec3{0, runtime_.heroes()->definition().visualHeight + 0.12, 0},
                      hero.health,
                      hero.maximumHealth);
        }
    }
    for (const auto& tower : runtime_.navigation().towers().towers()) {
        const auto& progress = runtime_.navigation().towers().progress(tower.object);
        healthBar(tower.position + glm::dvec3{0, tower.halfExtent.y + 0.12, 0},
                  progress.health,
                  progress.maximumHealth);
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
    if (const auto* hero = runtime_.selectedHero(); hero && hero->destination) {
        const auto center = *hero->destination + glm::dvec3{0, 0.03, 0};
        const std::array<glm::dvec3, 4> marker{center + glm::dvec3{-0.12, 0, 0},
                                               center + glm::dvec3{0, 0, 0.12},
                                               center + glm::dvec3{0.12, 0, 0},
                                               center + glm::dvec3{0, 0, -0.12}};
        polygon(marker, IM_COL32(70, 200, 185, 120));
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
            const auto& cleave = runtime_.heroes()->definition().combat.cleave;
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
        for (const auto& face : faces) {
            const std::array<glm::dvec3, 4> vertices{corners[face[0]],
                                                     corners[face[1]],
                                                     corners[face[2]],
                                                     corners[face[3]]};
            polygon(vertices,
                    runtime_.placementError().empty() ? IM_COL32(20, 180, 45, 40) : IM_COL32(190, 20, 12, 40));
        }
    }
}

} // namespace grimlock::game
