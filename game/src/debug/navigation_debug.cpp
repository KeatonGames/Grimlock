#include "debug/navigation_debug.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <utility>

namespace grimlock::game {

void NavigationDebug::menu(Engine& engine) {
    session_.synchronize(engine);
    levelMenu(engine);
    buildZoneMenu(engine);
    ImGui::BeginDisabled(level_.waves() && level_.waves()->phase() != LevelPhase::preparation);
    towerMenu(engine);
    ImGui::EndDisabled();
    auto& enemies = *session_.enemies();
    ImGui::Separator();
    ImGui::TextUnformatted("Animals");
    ImGui::BeginDisabled(level_.waves() != nullptr);
    const auto selected = session_.selectedAnimal();
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Animal Model", enemies.definition(selected).name.c_str())) {
        for (EnemyTypeId type = 0; type < enemies.typeCount(); ++type) {
            if (ImGui::Selectable(enemies.definition(type).name.c_str(), selected == type)) {
                session_.selectAnimal(engine, type);
            }
            if (selected == type) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    auto destination = session_.destination().value_or(glm::dvec2{0});
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputScalarN("Destination X/Z", ImGuiDataType_Double, &destination.x, 2, nullptr, nullptr, "%.3f")) {
        try {
            session_.setDestination(destination);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }

    ImGui::SetNextItemWidth(150);
    if (ImGui::InputInt("Enemy Count", &count_)) {
        count_ = std::clamp(count_, 1, 1000);
    }
    float speed = static_cast<float>(enemies.definition(session_.selectedAnimal()).speed);
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("Movement Speed", &speed, 0, 5, "%.2f")) {
        enemies.setSpawnSpeed(speed, session_.selectedAnimal());
    }
    if (ImGui::BeginMenu("Spawn Area (world X/Z)")) {
        auto& area = session_.spawnArea();
        ImGui::InputDouble("Minimum X", &area.minimum.x);
        ImGui::InputDouble("Minimum Z", &area.minimum.y);
        ImGui::InputDouble("Maximum X", &area.maximum.x);
        ImGui::InputDouble("Maximum Z", &area.maximum.y);
        ImGui::EndMenu();
    }
    if (ImGui::Button("Spawn Animals")) {
        try {
            lastSpawned_ = session_.spawn(engine, static_cast<std::size_t>(count_));
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Animals")) {
        session_.reset(engine);
        error_.clear();
    }
    ImGui::Text("Active: %zu | Despawned on arrival: %llu | Last spawn: %zu",
                enemies.enemies().size(),
                static_cast<unsigned long long>(enemies.metrics().arrived),
                lastSpawned_);
    std::array<std::size_t, 4> states{};
    for (const auto& enemy : enemies.enemies()) {
        ++states[static_cast<std::size_t>(enemy.state)];
    }
    ImGui::Text("Idle: %zu | Moving: %zu | Reached: %zu | Blocked: %zu", states[0], states[1], states[2], states[3]);
    enemyInspector(enemies);
    ImGui::EndDisabled();
    ImGui::Text("CPU update: %.3f ms | Neighbor checks: %llu",
                enemies.metrics().updateMilliseconds,
                static_cast<unsigned long long>(enemies.metrics().crowd.neighborChecks));
    ImGui::Text("Frame: %.2f ms (%.0f FPS)",
                1000.0F / std::max(1.0F, ImGui::GetIO().Framerate),
                ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::TextUnformatted("NavMesh");
    ImGui::Checkbox("Show NavMesh", &showNavMesh_);
    if (ImGui::Button("Rebuild NavMesh")) {
        session_.rebuild(engine);
    }
    const auto& navigation = session_.navigation();
    const auto& stats = navigation.statistics();
    ImGui::TextWrapped("Status: %s", navigation.status().c_str());
    ImGui::Text("Polygons: %zu | Build: %.2f ms | Paths: %llu",
                stats.polygons,
                stats.buildMilliseconds,
                static_cast<unsigned long long>(stats.pathQueries));
    ImGui::TextDisabled("Model and speed selection apply to future spawns.");
    if (!error_.empty()) {
        ImGui::TextWrapped("%s", error_.c_str());
    }
}

void NavigationDebug::towerMenu(Engine& engine) {
    if (ImGui::BeginMenu("Towers")) {
        const auto& towers = session_.towers();
        if (ImGui::BeginCombo("Tower Type", towers.definition(towerType_).name.c_str())) {
            for (const auto& definition : towers.definitions()) {
                if (ImGui::Selectable(definition.name.c_str(), definition.id == towerType_)) {
                    towerType_ = definition.id;
                    previewKey_.reset();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputFloat("Tower Rotation (degrees)", &towerYaw_, 5, 15, "%.1f");
        if (ImGui::Button("Place Tower")) {
            placingTower_ = true;
            placementScene_ = engine.scene();
            previewKey_.reset();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Cancel Placement")) {
            placingTower_ = showPreview_ = false;
        }
        const auto selected = engine.selectedObject();
        ImGui::BeginDisabled(!selected || !session_.towers().contains(*selected));
        if (ImGui::Button("Remove Selected Tower")) {
            sceneChanged_ = session_.removeTower(engine, *selected) || sceneChanged_;
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted("LMB: place | Escape: cancel | Mouse camera controls remain active.");
        ImGui::TextWrapped("%s", previewError_.c_str());
        ImGui::EndMenu();
    }
}

bool NavigationDebug::viewport(Engine& engine, const editor::ViewportInput& input) {
    const bool changed = std::exchange(sceneChanged_, false);
    session_.synchronize(engine);
    zoneQuads_.clear();
    if (showZones_ || placingTower_) {
        for (const auto& zone : session_.placement().zones()) {
            zoneQuads_.push_back({zone.corners()});
        }
    }
    showPreview_ = false;
    if (input.cancel || placementScene_ != engine.scene() ||
        (level_.waves() && level_.waves()->phase() != LevelPhase::preparation)) {
        placingTower_ = false;
    }
    if (!placingTower_) {
        session_.cancelPlacement();
        previewKey_.reset();
        return changed;
    }
    session_.synchronize(engine);
    const auto ray = input.pressRay ? input.pressRay : input.ray;
    const auto tower =
        ray ? session_.towerAt(*ray, towerType_, glm::radians(static_cast<double>(towerYaw_))) : std::nullopt;
    if (!tower) {
        previewKey_.reset();
        return changed;
    }
    const PreviewKey key{*tower,
                         session_.spawnArea(),
                         session_.destination(),
                         session_.revision(),
                         session_.selectedAnimal()};
    if (!previewKey_ || *previewKey_ != key) {
        previewKey_ = key;
        rejectedPlacement_.clear();
    }
    const auto footprintError = session_.checkTower(engine, *tower, false);
    previewError_ = footprintError.empty() ? rejectedPlacement_ : footprintError;
    if (input.pressRay && footprintError.empty()) {
        try {
            // Route validation belongs to the commit, not the cursor-following preview.
            (void)session_.placeTower(engine, *tower);
            previewKey_.reset();
            return true;
        } catch (const std::exception& error) {
            rejectedPlacement_ = previewError_ = error.what();
        }
    }
    preview_[0] = {glm::vec3{tower->position - tower->halfExtent},
                   glm::vec3{tower->position + tower->halfExtent},
                   previewError_.empty() ? glm::vec4{0.1F, 0.8F, 0.2F, 0.16F} : glm::vec4{0.9F, 0.12F, 0.08F, 0.16F},
                   glm::quat{tower->rotation}};
    showPreview_ = true;
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2{ImGui::GetIO().MousePos.x + 16, ImGui::GetIO().MousePos.y + 16},
        IM_COL32(235, 235, 235, 255),
        previewError_.empty() ? "Place tower (routes checked on click; Escape cancels)" : previewError_.c_str());
    return changed;
}

void NavigationDebug::enemyInspector(EnemySystem& enemies) {
    if (!ImGui::BeginMenu("Inspect Enemy", !enemies.enemies().empty())) {
        return;
    }
    const Enemy* selected = inspectedEnemy_ ? enemies.find(*inspectedEnemy_) : nullptr;
    if (!selected) {
        selected = &enemies.enemies().front();
        inspectedEnemy_ = selected->id;
    }
    char label[128];
    std::snprintf(label,
                  sizeof(label),
                  "%llu (%s)",
                  static_cast<unsigned long long>(selected->id),
                  enemies.definition(selected->type).name.c_str());
    if (ImGui::BeginCombo("Enemy", label)) {
        for (const auto& enemy : enemies.enemies()) {
            std::snprintf(label,
                          sizeof(label),
                          "%llu (%s)",
                          static_cast<unsigned long long>(enemy.id),
                          enemies.definition(enemy.type).name.c_str());
            if (ImGui::Selectable(label, enemy.id == *inspectedEnemy_)) {
                inspectedEnemy_ = enemy.id;
                selected = &enemy;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text("State: %s", enemyStateName(selected->state));
    ImGui::Text("Position: %.2f, %.2f, %.2f", selected->position.x, selected->position.y, selected->position.z);
    auto destination = selected->destination.value_or(glm::dvec2{selected->position.x, selected->position.z});
    if (ImGui::InputScalarN("Enemy Destination X/Z",
                            ImGuiDataType_Double,
                            &destination.x,
                            2,
                            nullptr,
                            nullptr,
                            "%.3f")) {
        try {
            (void)enemies.setDestination(selected->id, destination);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    if (ImGui::Button("Clear Enemy Destination")) {
        (void)enemies.setDestination(selected->id, std::nullopt);
    }
    ImGui::TextDisabled(selected->destination ? "Individual destination assigned." : "No destination (Idle).");
    ImGui::EndMenu();
}

void NavigationDebug::update(Engine& engine, double delta) {
    level_.update(engine, session_, delta);
}

void NavigationDebug::requestLevel(Engine& engine, LevelDefinition definition) {
    requestedScene_ = engine.assetRoot() / definition.scene;
    pendingLevel_ = std::move(definition);
}

std::optional<std::filesystem::path> NavigationDebug::takeSceneRequest() {
    return std::exchange(requestedScene_, std::nullopt);
}

void NavigationDebug::sceneOpened(Engine& engine, const std::filesystem::path& path) {
    placingTower_ = showPreview_ = false;
    level_.stop(session_);
    if (pendingLevel_ && !path.empty() &&
        std::filesystem::weakly_canonical(path) ==
            std::filesystem::weakly_canonical(engine.assetRoot() / pendingLevel_->scene)) {
        try {
            level_.begin(engine, session_, std::move(*pendingLevel_));
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    pendingLevel_.reset();
}

void NavigationDebug::levelMenu(Engine& engine) {
    if (ImGui::BeginMenu("Level")) {
        if (ImGui::Button("Load First Watch")) {
            try {
                requestLevel(engine, loadLevelDefinition(engine.assetRoot() / "levels/first_watch.json"));
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        ImGui::TextUnformatted("Handcrafted waves. Level status appears in its own panel.");
        ImGui::EndMenu();
    }
}

void NavigationDebug::panel(Engine& engine) {
    const auto* waves = level_.waves();
    if (!waves) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2{350, 45}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{360, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& definition = waves->definition();
        ImGui::TextUnformatted(definition.name.c_str());
        ImGui::TextUnformatted(levelPhaseName(waves->phase()));
        ImGui::Text("Objective: %llu / %llu",
                    static_cast<unsigned long long>(waves->health()),
                    static_cast<unsigned long long>(definition.objectiveHealth));
        const auto index = std::min(waves->wave(), definition.waves.size() - 1);
        ImGui::Text("Wave %zu / %zu: %s", index + 1, definition.waves.size(), definition.waves[index].name.c_str());
        ImGui::Text("Pending: %zu | Active: %zu", waves->pending(), session_.enemies()->enemies().size());
        ImGui::TextDisabled("Gold marker: objective | Teal markers: spawn areas");
        if (waves->phase() == LevelPhase::preparation) {
            ImGui::TextUnformatted("Prepare towers using Navigation > Towers.");
            if (ImGui::Button("Start Wave")) {
                placingTower_ = showPreview_ = false;
                level_.startWave(session_);
            }
        }
        if (ImGui::Button("Restart Level")) {
            requestLevel(engine, definition);
        }
    }
    ImGui::End();
}

void NavigationDebug::buildZoneMenu(Engine& engine) {
    if (!ImGui::BeginMenu("Build Zones")) {
        return;
    }
    ImGui::Checkbox("Show Build Zones", &showZones_);
    auto& scene = *engine.scene();
    if (ImGui::Button("Create Build Zone")) {
        BuildZone zone;
        zone.name = "Build zone";
        zone.position = {engine.camera().target.x, 0, engine.camera().target.z};
        try {
            (void)PlacementSystem::createZone(scene, zone);
            sceneChanged_ = true;
            session_.synchronize(engine);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    const auto selected = engine.selectedObject();
    std::optional<BuildZone> zone;
    for (const auto& item : session_.placement().zones()) {
        char label[256];
        std::snprintf(label,
                      sizeof(label),
                      "%s##zone%llu",
                      item.name.c_str(),
                      static_cast<unsigned long long>(item.object));
        if (ImGui::Selectable(label, selected == item.object)) {
            scene.selectObject(item.object, true);
        }
        if (scene.selectedObject() == item.object) {
            zone = item;
        }
    }
    if (zone) {
        char name[256];
        std::snprintf(name, sizeof(name), "%s", zone->name.c_str());
        bool edited = ImGui::InputText("Zone Name", name, sizeof(name));
        zone->name = name;
        edited = ImGui::InputScalarN("Zone Position",
                                     ImGuiDataType_Double,
                                     &zone->position.x,
                                     3,
                                     nullptr,
                                     nullptr,
                                     "%.3f") ||
                 edited;
        double degrees = glm::degrees(zone->yaw);
        edited = ImGui::InputDouble("Zone Rotation (degrees)", &degrees, 5, 15, "%.1f") || edited;
        zone->yaw = glm::radians(degrees);
        edited = ImGui::InputScalarN("Zone Width / Depth",
                                     ImGuiDataType_Double,
                                     &zone->dimensions.x,
                                     2,
                                     nullptr,
                                     nullptr,
                                     "%.3f") ||
                 edited;
        if (edited) {
            try {
                PlacementSystem::editZone(scene, *zone);
                sceneChanged_ = true;
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        if (ImGui::Button("Delete Build Zone")) {
            scene.removeObject(zone->object);
            sceneChanged_ = true;
        }
    }
    ImGui::TextUnformatted("Zones also appear in the scene hierarchy and save with the scene.");
    ImGui::TextWrapped("%s", session_.placement().error().c_str());
    ImGui::EndMenu();
}

} // namespace grimlock::game
