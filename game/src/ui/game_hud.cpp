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
    status();
    const auto* waves = runtime_.level().waves();
    if (waves && (waves->phase() == LevelPhase::victory || waves->phase() == LevelPhase::defeat)) {
        result();
    } else if (waves) {
        construction();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        runtime_.cancelPlacement();
    }
    viewportMouse_ = !relative && !io.WantCaptureMouse;
    return {!relative && io.WantCaptureMouse, io.WantCaptureKeyboard, false, runtime_.placing()};
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
        for (const auto& definition : runtime_.navigation().towers().definitions()) {
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
                        definition.damage,
                        definition.attackRange,
                        definition.fireRate);
            ImGui::PopID();
        }
        if (const auto* tower = runtime_.inspectedTower()) {
            const auto& definition = runtime_.navigation().towers().definition(tower->type);
            ImGui::Separator();
            ImGui::Text("Selected: %s", definition.name.c_str());
            ImGui::Text("Damage %.0f  |  Range %.1f  |  %.1f shots/s  |  %llu gold",
                        definition.damage,
                        definition.attackRange,
                        definition.fireRate,
                        static_cast<unsigned long long>(definition.constructionCost));
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
    if (!runtime_.placing() && confirm) {
        runtime_.selectTower(ray);
    }
    runtime_.updatePlacement(engine_, ray, confirm);
    placementOverlay(camera);
    ImGui::Render();
}

void GameHud::placementOverlay(const Camera& camera) {
    if (!runtime_.placing() && !runtime_.inspectedTower()) {
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
    if (runtime_.placing()) {
        for (const auto& zone : runtime_.navigation().placement().zones()) {
            const auto corners = zone.corners();
            const std::array<glm::dvec3, 4> vertices{corners[0], corners[1], corners[2], corners[3]};
            polygon(vertices, IM_COL32(35, 130, 90, 35));
        }
    }
    const auto* rangeTower = runtime_.preview() ? &*runtime_.preview() : runtime_.inspectedTower();
    if (rangeTower) {
        const auto range = runtime_.navigation().towers().definition(rangeTower->type).attackRange;
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
            polygon(vertices, IM_COL32(115, 205, 200, 75), false);
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
