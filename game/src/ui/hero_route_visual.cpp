#include "ui/hero_route_visual.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <limits>

namespace grimlock::game {

void HeroRouteVisual::clear() {
    route_.clear();
    remaining_.clear();
    segment_ = 0;
    revision_ = 0;
    waypoint_.reset();
}

void HeroRouteVisual::update(const NavMesh& mesh,
                             glm::dvec3 position,
                             const NavigationPath& path,
                             std::optional<glm::dvec3> waypoint) {
    if (!path.valid || path.revision != mesh.revision()) {
        clear();
        return;
    }
    const auto rebuild = [&] {
        route_ = mesh.movementRoute(position, path, waypoint);
        destination_ = path.destination;
        waypoint_ = waypoint;
        revision_ = mesh.revision();
        segment_ = 0;
    };
    if (route_.empty() || revision_ != mesh.revision() || glm::distance(destination_, path.destination) > 1e-5 ||
        waypoint_ != waypoint) {
        rebuild();
    }
    double nearest = std::numeric_limits<double>::max();
    auto current = segment_;
    // Search locally along the remaining route, avoiding jumps to a nearby later switchback.
    for (auto i = segment_; i + 1 < route_.size() && i < segment_ + 32; ++i) {
        const auto edge = route_[i + 1] - route_[i];
        const auto squared = glm::dot(edge, edge);
        const auto fraction =
            squared > 1e-12 ? std::clamp(glm::dot(position - route_[i], edge) / squared, 0.0, 1.0) : 0;
        const auto distance = glm::distance(position, route_[i] + edge * fraction);
        if (distance < nearest) {
            nearest = distance;
            current = i;
        }
    }
    if (nearest > 0.16 && route_.size() > 1) {
        // Dynamic collision avoidance or a newly installed corridor can change the remaining route.
        rebuild();
    } else {
        segment_ = current;
    }
    remaining_.clear();
    if (route_.size() < 2) {
        return;
    }
    remaining_.push_back(position);
    for (auto i = segment_ + 1; i < route_.size(); ++i) {
        if (glm::distance(remaining_.back(), route_[i]) > 1e-5) {
            remaining_.push_back(route_[i]);
        }
    }
}

std::vector<RouteDash> HeroRouteVisual::dashes() const {
    constexpr double dashLength = 0.28, period = 0.46;
    std::vector<RouteDash> result;
    double travelled = 0;
    // Anchor the pattern at the destination so walking clips dashes instead of sliding them.
    for (std::size_t i = remaining_.size(); i > 1; --i) {
        const auto from = remaining_[i - 1], to = remaining_[i - 2];
        const auto length = glm::distance(from, to);
        if (length < 1e-8) {
            continue;
        }
        double offset = 0;
        while (offset < length - 1e-8) {
            auto phase = std::fmod(travelled, period);
            if (phase > period - 1e-8) {
                phase = 0;
            }
            const bool visible = phase < dashLength - 1e-8;
            const auto step = std::min(length - offset, (visible ? dashLength : period) - phase);
            if (step < 1e-9) {
                break;
            }
            if (visible) {
                result.push_back({glm::mix(from, to, offset / length), glm::mix(from, to, (offset + step) / length)});
            }
            offset += step;
            travelled += step;
        }
    }
    return result;
}

} // namespace grimlock::game
