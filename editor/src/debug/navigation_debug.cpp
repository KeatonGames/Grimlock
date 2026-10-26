#include "debug/navigation_debug.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <utility>

namespace grimlock::game {

void NavigationDebug::menu(Engine& engine) {
    session_.synchronize(engine);
    levelMenu(engine);
    buildZoneMenu(engine);
    towerMenu(engine);
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
    double destinationHeight = session_.destinationHeight();
    if (ImGui::InputDouble("Destination Height", &destinationHeight, 0.1, 1, "%.3f")) {
        try {
            session_.setDestinationHeight(destinationHeight);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
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
        ImGui::InputDouble("Spawn Height", &area.height);
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
    ImGui::Checkbox("Show Combat Visibility", &showCombat_);
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
        ImGui::TextDisabled("Editor authoring: free. Use the Level panel for paid construction.");
        const auto& towers = session_.towers();
        if (ImGui::BeginCombo("Tower Type", towers.definition(towerType_).name.c_str())) {
            for (const auto& definition : towers.definitions()) {
                if (ImGui::Selectable(definition.name.c_str(), definition.id == towerType_)) {
                    towerType_ = definition.id;
                    previewKey_.reset();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%llu gold", static_cast<unsigned long long>(definition.constructionCost));
            }
            ImGui::EndCombo();
        }
        ImGui::InputFloat("Tower Rotation (degrees)", &towerYaw_, 5, 15, "%.1f");
        if (ImGui::Button("Place Tower")) {
            gameplayPlacement_ = false;
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
    combatDebug(engine);
    if (showZones_ || placingTower_) {
        for (const auto& zone : session_.placement().zones()) {
            zoneQuads_.push_back({zone.corners()});
        }
    }
    showPreview_ = false;
    if (input.cancel || placementScene_ != engine.scene() ||
        (gameplayPlacement_ && (!level_.waves() || level_.waves()->phase() != LevelPhase::preparation))) {
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
                         session_.selectedAnimal(),
                         session_.destinationHeight()};
    if (!previewKey_ || *previewKey_ != key) {
        previewKey_ = key;
        rejectedPlacement_.clear();
    }
    auto footprintError = gameplayPlacement_ ? level_.constructionError(session_, *tower) : std::string{};
    if (footprintError.empty()) {
        footprintError = session_.checkTower(engine,
                                             *tower,
                                             false,
                                             gameplayPlacement_ ? PlacementMode::gameplay : PlacementMode::authoring);
    }
    previewError_ = footprintError.empty() ? rejectedPlacement_ : footprintError;
    if (input.pressRay && footprintError.empty()) {
        try {
            // Route validation belongs to the commit, not the cursor-following preview.
            if (gameplayPlacement_) {
                (void)level_.buildTower(engine, session_, *tower);
            } else {
                (void)session_.placeTower(engine, *tower, PlacementMode::authoring);
            }
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

void NavigationDebug::combatDebug(Engine& engine) {
    combatLines_.clear();
    if (!showCombat_) {
        return;
    }
    const auto selected = engine.selectedObject();
    const Tower* tower = nullptr;
    for (const auto& item : session_.towers().towers()) {
        if (selected == item.object) {
            tower = &item;
            break;
        }
    }
    const Enemy* inspected =
        inspectedEnemy_ && session_.enemies() ? session_.enemies()->find(*inspectedEnemy_) : nullptr;
    if (!tower && !inspected) {
        return;
    }
    const glm::dvec3 origin = tower ? firingOrigin(*tower) : combatAim(*inspected);
    const ObjectId ignore = tower ? tower->object : 0;
    const auto line = [&](glm::dvec3 a, glm::dvec3 b, glm::vec4 color) {
        combatLines_.push_back({a, b, color});
    };
    for (const auto& enemy : session_.enemies()->enemies()) {
        if (!enemy.alive || !enemy.active || (&enemy == inspected && !tower)) {
            continue;
        }
        const auto target = combatAim(enemy);
        const auto hit = session_.combatGeometry().trace(origin, target, 0, ignore);
        line(origin, target, hit ? glm::vec4{1, 0.25F, 0.2F, 0.7F} : glm::vec4{0.2F, 1, 0.4F, 0.7F});
        if (hit) {
            for (const auto axis : {glm::dvec3{0.08, 0, 0}, glm::dvec3{0, 0.08, 0}, glm::dvec3{0, 0, 0.08}}) {
                line(hit->position - axis, hit->position + axis, {1, 0.8F, 0.1F, 1});
            }
        }
    }
    if (tower) {
        const auto radius = session_.towers()
                                .definition(tower->type)
                                .statistics(session_.towers().progress(tower->object).level)
                                .attackRange;
        const auto center = tower->position - glm::dvec3{0, tower->halfExtent.y, 0};
        for (int plane = 0; plane < 3; ++plane) {
            for (int i = 0; i < 64; ++i) {
                const auto point = [&](int step) {
                    const double angle = glm::radians(360.0) * step / 64;
                    glm::dvec3 p{0};
                    p[plane] = std::cos(angle) * radius;
                    p[(plane + 1) % 3] = std::sin(angle) * radius;
                    return center + p;
                };
                line(point(i), point(i + 1), {0.3F, 0.8F, 1, 0.5F});
            }
        }
    }
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
                requestLevelLoad(engine, engine.assetRoot() / "levels/first_watch.json");
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        if (ImGui::Button("Load Hill Watch")) {
            try {
                requestLevelLoad(engine, engine.assetRoot() / "levels/hill_watch.json");
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        if (ImGui::Button("Load Cover Watch")) {
            try {
                requestLevelLoad(engine, engine.assetRoot() / "levels/cover_watch.json");
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        ImGui::TextUnformatted("Handcrafted waves. Level status appears in its own panel.");
        levelDefinitionMenu(engine);
        ImGui::EndMenu();
    }
}

void NavigationDebug::requestLevelLoad(Engine& engine, const std::filesystem::path& path) {
    const auto resolved = path.is_absolute() ? path : engine.assetRoot().parent_path() / path;
    auto definition = loadLevelDefinition(resolved);
    levelDraft_ = definition;
    const auto text = resolved.u8string();
    levelSavePath_.assign(text.begin(), text.end());
    requestLevel(engine, std::move(definition));
}

void NavigationDebug::levelDefinitionMenu(Engine& engine) {
    if (!ImGui::BeginMenu("Level Definition", levelDraft_.has_value())) {
        return;
    }
    auto& definition = *levelDraft_;
    ImGui::InputText("Level Name", &definition.name);
    auto scene = definition.scene.generic_string();
    if (ImGui::InputText("Scene (asset relative)", &scene)) {
        definition.scene = scene;
    }
    ImGui::InputScalar("Starting Gold", ImGuiDataType_U64, &definition.startingGold);
    ImGui::InputScalarN("Objective X/Z", ImGuiDataType_Double, &definition.objectivePosition.x, 2);
    ImGui::InputDouble("Objective Height", &definition.objectiveHeight, 0.1, 1, "%.3f");
    ImGui::InputScalar("Objective Health", ImGuiDataType_U64, &definition.objectiveHealth);
    ImGui::InputScalar("Arrival Damage", ImGuiDataType_U64, &definition.damagePerArrival);
    for (std::size_t i = 0; i < definition.spawns.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("Spawn", "%s", definition.spawns[i].name.c_str())) {
            auto& spawn = definition.spawns[i];
            ImGui::InputText("Name", &spawn.name);
            ImGui::InputScalarN("Minimum X/Z", ImGuiDataType_Double, &spawn.area.minimum.x, 2);
            ImGui::InputScalarN("Maximum X/Z", ImGuiDataType_Double, &spawn.area.maximum.x, 2);
            ImGui::InputDouble("Elevation", &spawn.area.height);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    for (std::size_t i = 0; i < definition.waves.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("Wave", "%s", definition.waves[i].name.c_str())) {
            auto& wave = definition.waves[i];
            ImGui::InputText("Name", &wave.name);
            ImGui::InputScalar("Reward", ImGuiDataType_U64, &wave.goldReward);
            for (std::size_t groupIndex = 0; groupIndex < wave.groups.size(); ++groupIndex) {
                ImGui::PushID(static_cast<int>(groupIndex));
                auto& group = wave.groups[groupIndex];
                ImGui::Separator();
                if (ImGui::BeginCombo("Animal", group.enemy.c_str())) {
                    for (EnemyTypeId type = 0; type < session_.enemies()->typeCount(); ++type) {
                        const auto& name = session_.enemies()->definition(type).name;
                        if (ImGui::Selectable(name.c_str(), name == group.enemy)) {
                            group.enemy = name;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Spawn", definition.spawns[group.spawn].name.c_str())) {
                    for (std::size_t spawn = 0; spawn < definition.spawns.size(); ++spawn) {
                        if (ImGui::Selectable(definition.spawns[spawn].name.c_str(), spawn == group.spawn)) {
                            group.spawn = spawn;
                        }
                    }
                    ImGui::EndCombo();
                }
                std::uint64_t count = group.count, batch = group.batchSize;
                if (ImGui::InputScalar("Count", ImGuiDataType_U64, &count)) {
                    group.count = static_cast<std::size_t>(std::clamp<std::uint64_t>(count, 1, 1000));
                }
                if (ImGui::InputScalar("Batch Size", ImGuiDataType_U64, &batch)) {
                    group.batchSize = static_cast<std::size_t>(std::clamp<std::uint64_t>(batch, 1, group.count));
                }
                ImGui::InputDouble("Start (seconds)", &group.start);
                ImGui::InputDouble("Interval (seconds)", &group.interval);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::InputText("Level File", &levelSavePath_);
    if (ImGui::Button("Save Level Definition")) {
        try {
            saveLevelDefinition(std::filesystem::path{std::u8string{levelSavePath_.begin(), levelSavePath_.end()}},
                                definition);
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    if (ImGui::Button("Apply and Restart Preview")) {
        try {
            validateLevelDefinition(definition);
            requestLevel(engine, definition);
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    ImGui::TextWrapped("%s", error_.c_str());
    ImGui::EndMenu();
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
        ImGui::Text("Gold: %llu", static_cast<unsigned long long>(level_.economy().gold()));
        if (const auto reward = level_.lastWaveReward()) {
            ImGui::Text("Last wave reward: +%llu gold", static_cast<unsigned long long>(*reward));
        }
        ImGui::TextDisabled("Gold marker: objective | Teal markers: spawn areas");
        constructionControls(engine);
        if (waves->phase() == LevelPhase::preparation) {
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

void NavigationDebug::constructionControls(Engine& engine) {
    ImGui::SeparatorText("Construction");
    for (const auto& definition : session_.towers().definitions()) {
        if (ImGui::Selectable(definition.name.c_str(), definition.id == towerType_, 0, ImVec2{130, 0})) {
            towerType_ = definition.id;
            previewKey_.reset();
        }
        ImGui::SameLine();
        ImGui::Text("%llu gold | %s",
                    static_cast<unsigned long long>(definition.constructionCost),
                    level_.economy().canAfford(definition.constructionCost) ? "Affordable" : "Insufficient gold");
    }
    const auto tower = session_.towers().definition(towerType_).instance();
    const auto error = level_.constructionError(session_, tower);
    ImGui::BeginDisabled(!error.empty());
    if (ImGui::Button("Build Tower")) {
        gameplayPlacement_ = placingTower_ = true;
        placementScene_ = engine.scene();
        previewKey_.reset();
        rejectedPlacement_.clear();
    }
    ImGui::EndDisabled();
    if (!error.empty()) {
        ImGui::TextWrapped("%s", error.c_str());
    }
    if (gameplayPlacement_ && placingTower_) {
        ImGui::TextUnformatted("Left-click to build | Escape cancels");
    }
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
        zone.position = engine.camera().target;
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
