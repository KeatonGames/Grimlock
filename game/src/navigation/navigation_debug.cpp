#include "navigation/navigation_debug.hpp"

#include <cmath>
#include <imgui.h>

namespace grimlock::game {

void NavigationDebug::menu(Engine& engine) {
    demo_.synchronize(engine);
    auto* scene = engine.scene();
    auto* grid = scene ? scene->navigationGrid() : nullptr;
    ImGui::Separator();
    ImGui::BeginDisabled(!grid);
    ImGui::TextUnformatted("Flow Field");
    auto destination = demo_.destination();
    int cell[2]{destination ? static_cast<int>(destination->x) : 0, destination ? static_cast<int>(destination->z) : 0};
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputInt2("Destination X/Z", cell)) {
        demo_.setDestination(cell[0] >= 0 && cell[1] >= 0 ? std::optional{GridCell{static_cast<std::uint32_t>(cell[0]),
                                                                                   static_cast<std::uint32_t>(cell[1])}}
                                                          : std::nullopt);
        demo_.synchronize(engine);
    }
    ImGui::Checkbox("Show Flow Directions", &showDirections_);
    ImGui::Checkbox("Show Integration Costs", &showCosts_);
    if (showCosts_) {
        ImGui::TextDisabled("Green: near | Orange: far | Grey: unreachable");
    }
    float speed = static_cast<float>(demo_.speed());
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("Agent Speed", &speed, 0, 5, "%.2f")) {
        demo_.setSpeed(speed);
    }
    if (ImGui::Button("Spawn Test Agents")) {
        demo_.spawn(engine);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Test Agents")) {
        demo_.reset(engine);
    }
    ImGui::Text("Agents: %zu | Field builds: %llu",
                demo_.agents().size(),
                static_cast<unsigned long long>(demo_.field().generation()));
    destination = demo_.destination();
    if (!destination || !std::isfinite(demo_.field().cost(*destination))) {
        ImGui::TextUnformatted("Destination is outside the grid or blocked. Agents stop.");
    }
    ImGui::EndDisabled();
}

editor::GridDebugView NavigationDebug::view(Engine& engine) {
    demo_.synchronize(engine);
    if (!engine.scene() || !engine.scene()->navigationGrid()) {
        return {};
    }
    return {demo_.field().directions(), demo_.field().costs(), demo_.destination(), showDirections_, showCosts_};
}

void NavigationDebug::update(Engine& engine, double delta) {
    demo_.update(engine, delta);
}

} // namespace grimlock::game
