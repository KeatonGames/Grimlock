#include "debug/navigation_debug.hpp"

#include <algorithm>
#include <imgui.h>

namespace grimlock::game {

void NavigationDebug::menu(Engine& engine) {
    session_.synchronize(engine);
    auto& enemies = *session_.enemies();
    ImGui::Separator();
    ImGui::TextUnformatted("Chickens");
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
    float speed = static_cast<float>(enemies.definition().speed);
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("Movement Speed", &speed, 0, 5, "%.2f")) {
        enemies.setSpeed(speed);
    }
    if (ImGui::BeginMenu("Spawn Area (world X/Z)")) {
        auto& area = session_.spawnArea();
        ImGui::InputDouble("Minimum X", &area.minimum.x);
        ImGui::InputDouble("Minimum Z", &area.minimum.y);
        ImGui::InputDouble("Maximum X", &area.maximum.x);
        ImGui::InputDouble("Maximum Z", &area.maximum.y);
        ImGui::EndMenu();
    }
    if (ImGui::Button("Spawn Chickens")) {
        try {
            lastSpawned_ = session_.spawn(engine, static_cast<std::size_t>(count_));
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Chickens")) {
        session_.reset(engine);
        error_.clear();
    }
    ImGui::Text("Active: %zu | Arrived: %llu | Last spawn: %zu",
                enemies.enemies().size(),
                static_cast<unsigned long long>(enemies.metrics().arrived),
                lastSpawned_);
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
    ImGui::TextDisabled("Chicken asset has no clips: rest pose is supported.");
    if (!error_.empty()) {
        ImGui::TextWrapped("%s", error_.c_str());
    }
}

void NavigationDebug::update(Engine& engine, double delta) {
    session_.update(engine, delta);
}

} // namespace grimlock::game
