#pragma once

#include "entities/build_zone.hpp"

#include <grimlock/engine.hpp>
#include <map>

namespace grimlock::game {

// Render-only construction pads shared by Game and Editor. Engine must outlive this owner.
class BuildZoneMarkers final {
public:
    BuildZoneMarkers() = default;

    ~BuildZoneMarkers() {
        clear();
    }

    BuildZoneMarkers(const BuildZoneMarkers&) = delete;
    BuildZoneMarkers& operator=(const BuildZoneMarkers&) = delete;

    void synchronize(Engine& engine, std::span<const BuildZone> zones);
    void clear();

private:
    Engine* engine_ = nullptr;
    const Scene* scene_ = nullptr;
    std::map<ObjectId, InstanceId> instances_;
};

} // namespace grimlock::game
