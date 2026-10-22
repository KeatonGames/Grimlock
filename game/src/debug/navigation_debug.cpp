#include "debug/navigation_debug.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>

namespace grimlock::game {

void NavigationDebug::menu(Engine& engine) {
    session_.synchronize(engine);
    auto& enemies = *session_.enemies();
    ImGui::Separator();
    ImGui::TextUnformatted("Animals");
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
    session_.update(engine, delta);
}

} // namespace grimlock::game
