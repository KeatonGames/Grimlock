#include "systems/build_zone_markers.hpp"

#include <algorithm>

namespace grimlock::game {

void BuildZoneMarkers::clear() {
    for (const auto& [zone, instance] : instances_) {
        engine_->removeInstance(instance);
    }
    instances_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

void BuildZoneMarkers::synchronize(Engine& engine, std::span<const BuildZone> zones) {
    if (engine_ != &engine || scene_ != engine.scene()) {
        clear();
    }
    engine_ = &engine;
    scene_ = engine.scene();
    if (!scene_) {
        return;
    }
    std::erase_if(instances_, [&](const auto& entry) {
        const bool available = std::any_of(zones.begin(), zones.end(), [&](const BuildZone& zone) {
            return zone.object == entry.first && zone.occupants.empty();
        });
        if (!available) {
            engine.removeInstance(entry.second);
        }
        return !available;
    });
    std::optional<ModelId> model;
    for (const auto& zone : zones) {
        if (!zone.occupants.empty()) {
            continue;
        }
        auto found = instances_.find(zone.object);
        if (found == instances_.end()) {
            if (!model) {
                model = engine.loadModel(engine.assetRoot() / "towers/structures/tower_defense/construction-zone.glb");
            }
            const auto instance = engine.createInstance(*model, true);
            try {
                found = instances_.emplace(zone.object, instance).first;
            } catch (...) {
                engine.removeInstance(instance);
                throw;
            }
        }
        Transform transform;
        transform.position = zone.constructionPosition() + glm::dvec3{0, 0.015, 0};
        transform.rotation = glm::angleAxis(zone.yaw + zone.constructionYaw, glm::dvec3{0, 1, 0});
        transform.scale = {2, 1, 2};
        engine.setTransform(found->second, transform);
    }
}

} // namespace grimlock::game
