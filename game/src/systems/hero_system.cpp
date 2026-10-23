#include "systems/hero_system.hpp"

#include "systems/health.hpp"
#include "systems/movement_contact.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock::game {
namespace {
std::optional<std::size_t> clip(const ModelInfo& model, const std::string& requested) {
    if (requested.empty()) {
        return {};
    }
    for (std::size_t i = 0; i < model.animationNames.size(); ++i) {
        if (model.animationNames[i] == requested) {
            return i;
        }
    }
    for (std::size_t i = 0; i < model.animationNames.size(); ++i) {
        auto name = model.animationNames[i];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name.find(requested) != std::string::npos && name.find("start") == std::string::npos &&
            name.find("end") == std::string::npos) {
            return i;
        }
    }
    return {};
}

void stop(Hero& hero, HeroState state) {
    hero.avoidanceEnemy.reset();
    hero.state = state;
    hero.speed = hero.stuckTime = 0;
    hero.destination.reset();
    hero.path.valid = false;
}
} // namespace

HeroSystem::HeroSystem(Engine& engine)
    : engine_(engine), scene_(engine.scene()), definition_(loadHeroDefinition(engine.assetRoot())),
      model_(engine.loadModel(engine.assetRoot() / definition_.model)), modelInfo_(engine.modelInfo(model_)) {
    if (!scene_ || !std::isfinite(glm::length(modelInfo_.maximum - modelInfo_.minimum)) ||
        modelInfo_.maximum.y <= modelInfo_.minimum.y) {
        throw std::invalid_argument("Hero requires an active scene and finite model bounds.");
    }
    idleClip_ = clip(modelInfo_, definition_.idleAnimation);
    attackClip_ = clip(modelInfo_, definition_.combat.attackAnimation);
    deathClip_ = clip(modelInfo_, "death");
    if (!deathClip_) {
        deathClip_ = clip(modelInfo_, "die");
    }
    movementClip_ = clip(modelInfo_, definition_.runAnimation);
    if (!movementClip_) {
        movementClip_ = clip(modelInfo_, definition_.walkAnimation);
    }
}

HeroSystem::~HeroSystem() {
    clear();
}

void HeroSystem::clear() noexcept {
    preview({});
    if (engine_.scene() == scene_) {
        for (const auto& hero : heroes_) {
            if (engine_.scene()->contains(hero.object)) {
                engine_.scene()->removeObject(hero.object);
            }
        }
    }
    heroes_.clear();
}

void HeroSystem::synchronize(const TowerSystem& towers) {
    if (engine_.scene() != scene_) {
        throw std::logic_error("Reset heroes when replacing a scene.");
    }
    scratchBoxes_.clear();
    if (const auto* grid = scene_->navigationGrid()) {
        if (terrainRevision_ != grid->revision() || terrainConfiguration_ != grid->configuration()) {
            terrain_ = terrainBoxes(*grid);
            terrainRevision_ = grid->revision();
            terrainConfiguration_ = grid->configuration();
        }
        scratchBoxes_ = terrain_;
    }
    for (const auto& tower : towers.towers()) {
        scratchBoxes_.push_back(tower.bounds());
    }
    for (const auto& [id, object] : scene_->objects()) {
        if (!object.model || !scene_->enabledInHierarchy(id) || object.role == "grimlock.hero" ||
            object.role == "grimlock.tower" || object.role.starts_with("grimlock.tower:")) {
            continue;
        }
        auto found = props_.find(*object.model);
        if (found == props_.end()) {
            found =
                props_.emplace(*object.model, engine_.modelInfo(engine_.loadModel(engine_.assetRoot() / *object.model)))
                    .first;
        }
        const auto& info = found->second;
        const glm::dmat4 world{scene_->worldTransform(id)};
        const glm::dvec3 center = world * glm::dvec4{(info.minimum + info.maximum) * 0.5F, 1};
        const auto size = glm::dvec3{info.maximum - info.minimum} * 0.5;
        const auto extent = glm::abs(glm::dvec3{world[0]}) * size.x + glm::abs(glm::dvec3{world[1]}) * size.y +
                            glm::abs(glm::dvec3{world[2]}) * size.z;
        if (extent.y * 2 > 0.05) {
            scratchBoxes_.push_back({glm::vec3{center}, glm::vec3{extent}, false});
        }
    }
    const bool same = boxes_.size() == scratchBoxes_.size() &&
                      std::equal(boxes_.begin(), boxes_.end(), scratchBoxes_.begin(), [](const auto& a, const auto& b) {
                          return a.center == b.center && a.halfExtent == b.halfExtent && a.walkableTop == b.walkableTop;
                      });
    if (!same || !navigation_.ready()) {
        boxes_.swap(scratchBoxes_);
        NavMeshConfiguration config;
        config.agentRadius = static_cast<float>(definition_.radius);
        config.agentHeight = static_cast<float>(definition_.height);
        if (!navigation_.build(navigationGeometry(boxes_), config)) {
            throw std::runtime_error("Cannot build hero navigation: " + navigation_.status());
        }
    }
}

bool HeroSystem::validPosition(glm::dvec3 position, std::optional<HeroId> ignore) const {
    if (!navigation_.contains(position)) {
        return false;
    }
    for (const auto& hero : heroes_) {
        if (ignore == hero.id || hero.health <= 0) {
            continue;
        }
        if (glm::length(glm::dvec2{hero.position.x - position.x, hero.position.z - position.z}) <
            definition_.radius * 2) {
            return false;
        }
    }
    return true;
}

Transform HeroSystem::transform(glm::dvec3 position, double yaw) const {
    const auto center = (modelInfo_.minimum + modelInfo_.maximum) * 0.5F;
    const float scale = definition_.visualHeight / (modelInfo_.maximum.y - modelInfo_.minimum.y);
    Transform result;
    result.rotation = glm::angleAxis(static_cast<float>(yaw + definition_.forwardYaw), glm::vec3{0, 1, 0});
    result.scale = glm::vec3{scale};
    result.position =
        glm::vec3{position} + result.rotation * (glm::vec3{-center.x, -modelInfo_.minimum.y, -center.z} * scale);
    return result;
}

HeroId HeroSystem::deploy(glm::dvec3 position, std::optional<HeroId> existing) {
    if (!validPosition(position, existing)) {
        throw std::invalid_argument("Deploy on clear navigable ground.");
    }
    position = navigation_.project(position, 0.03)->position;
    Hero* hero = existing ? findMutable(*existing) : nullptr;
    if (existing && !hero) {
        throw std::invalid_argument("Unknown hero.");
    }
    if (hero && hero->health <= 0) {
        throw std::invalid_argument("A dead hero cannot be redeployed. Restart the level to restore heroes.");
    }
    if (!hero) {
        const auto object = engine_.scene()->createObject(definition_.name);
        try {
            engine_.scene()->setRole(object, "grimlock.hero");
            engine_.scene()->setModel(object, definition_.model);
            engine_.prepareScene(*engine_.scene());
            Hero created;
            created.id = nextId_++;
            created.object = object;
            created.health = created.maximumHealth = definition_.maximumHealth;
            heroes_.push_back(std::move(created));
        } catch (...) {
            engine_.scene()->removeObject(object);
            throw;
        }
        hero = &heroes_.back();
    }
    hero->position = position;
    hero->avoidanceEnemy.reset();
    hero->defensivePosition = position;
    hero->combat = {};
    stop(*hero, HeroState::idle);
    updateModel(*hero);
    if (idleClip_) {
        engine_.scene()->playAnimation(hero->object, *idleClip_);
    }
    return hero->id;
}

bool HeroSystem::command(HeroId id, glm::dvec3 destination) {
    auto* hero = findMutable(id);
    if (!hero || !hero->controllable || hero->health <= 0) {
        return false;
    }
    finishAttack(*hero);
    hero->combat.target.reset();
    hero->combat.excludedTarget.reset();
    hero->combat.playerMoving = true;
    hero->combat.returning = false;
    const bool accepted = navigate(*hero, destination);
    if (accepted) {
        hero->defensivePosition = *hero->destination;
    }
    return accepted;
}

bool HeroSystem::cancelCommand(HeroId id) {
    auto* hero = findMutable(id);
    if (!hero || hero->health <= 0) {
        return false;
    }
    finishAttack(*hero);
    hero->combat.target.reset();
    hero->combat.playerMoving = hero->combat.explicitTarget = hero->combat.returning = false;
    hero->combat.retryDelay = 0;
    stop(*hero, HeroState::idle);
    return true;
}

bool HeroSystem::applyDamage(HeroId id, float damage) {
    // Validate even when the stable reference no longer identifies a living hero.
    validateDamage(damage);
    auto* hero = findMutable(id);
    if (!hero || hero->health <= 0 || damage == 0) {
        return false;
    }
    if (receiveDamage(hero->health, damage)) {
        finishAttack(*hero);
        hero->combat = {};
        stop(*hero, HeroState::idle);
        hero->controllable = false;
        engine_.scene()->setTint(hero->object, {0.4F, 0.35F, 0.35F, 1});
        if (deathClip_) {
            engine_.scene()->playAnimation(hero->object, *deathClip_, false);
        } else {
            engine_.scene()->stopAnimation(hero->object);
        }
    } else {
        hero->hitFlashRemaining = 0.15;
        engine_.scene()->setTint(hero->object, {1, 0.35F, 0.3F, 1});
    }
    return true;
}

bool HeroSystem::navigate(Hero& hero, glm::dvec3 destination) {
    if (!navigation_.contains(destination) || !navigation_.findPath(hero.position, destination, hero.path)) {
        stop(hero, HeroState::blocked);
        updateModel(hero);
        return false;
    }
    hero.destination = hero.path.destination;
    hero.state = HeroState::moving;
    hero.stuckTime = 0;
    return true;
}

void HeroSystem::step(Hero& hero, double delta, std::span<const Enemy> enemies) {
    if (!hero.destination || hero.health <= 0 || !hero.controllable) {
        stop(hero, HeroState::idle);
        return;
    }
    if (hero.path.revision != navigation_.revision() &&
        !navigation_.findPath(hero.position, *hero.destination, hero.path)) {
        stop(hero, HeroState::blocked);
        return;
    }
    double remaining = 0;
    auto target = navigation_.steering(hero.position, hero.path, &remaining);
    // Surface clipping can enter an adjacent polygon outside the cached corridor near a corner.
    if (!target && navigation_.findPath(hero.position, *hero.destination, hero.path)) {
        target = navigation_.steering(hero.position, hero.path, &remaining);
    }
    if (!target) {
        stop(hero, HeroState::blocked);
        return;
    }
    if (remaining <= definition_.stoppingDistance) {
        stop(hero, HeroState::reached);
        return;
    }
    const auto steering = steerAroundEnemies(hero, *target, enemies);
    const glm::dvec2 offset{steering.x - hero.position.x, steering.z - hero.position.z};
    const double distance = glm::length(offset);
    if (distance < 1e-8) {
        stop(hero, HeroState::blocked);
        return;
    }
    const auto direction = offset / distance;
    const auto desired = std::min(definition_.speed, std::sqrt(2 * definition_.deceleration * remaining));
    hero.speed = std::clamp(desired,
                            std::max(0.0, hero.speed - definition_.deceleration * delta),
                            hero.speed + definition_.acceleration * delta);
    auto displacement = direction * std::min(remaining, hero.speed * delta);
    auto next = hero.position;
    for (int contact = 0; contact < 4 && glm::length(displacement) > 1e-10; ++contact) {
        const auto surface = navigation_.move(next, displacement);
        displacement = {surface.x - next.x, surface.z - next.z};
        const Enemy* neighbor = nullptr;
        double fraction = 1;
        for (const auto& enemy : enemies) {
            if (!enemy.active || !enemy.alive || std::abs(next.y - enemy.position.y) > definition_.height) {
                continue;
            }
            const double t = contactFraction({next.x - enemy.position.x, next.z - enemy.position.z},
                                             displacement,
                                             definition_.radius + enemy.radius);
            if (t < fraction) {
                fraction = t;
                neighbor = &enemy;
            }
        }
        next = navigation_.move(next, displacement * fraction);
        if (!neighbor) {
            break;
        }
        const glm::dvec2 contactOffset{next.x - neighbor->position.x, next.z - neighbor->position.z};
        const auto normal = glm::length(contactOffset) > 1e-8 ? glm::normalize(contactOffset) : glm::dvec2{1, 0};
        displacement *= 1 - fraction;
        displacement -= normal * std::min(0.0, glm::dot(displacement, normal));
    }
    const double traveled = glm::length(next - hero.position);
    hero.stuckTime = traveled < 1e-6 ? hero.stuckTime + delta : 0;
    if (hero.stuckTime > 1) {
        stop(hero, HeroState::blocked);
        return;
    }
    if (traveled > 1e-6) {
        const auto yaw = std::atan2(next.x - hero.position.x, next.z - hero.position.z);
        hero.yaw +=
            std::remainder(yaw - hero.yaw, 2 * std::acos(-1.0)) * (1 - std::exp(-definition_.turningSpeed * delta));
    }
    hero.position = next;
}

glm::dvec3 HeroSystem::steerAroundEnemies(Hero& hero, glm::dvec3 target, std::span<const Enemy> enemies) const {
    const auto horizontal = [](glm::dvec3 value) {
        return glm::dvec2{value.x, value.z};
    };
    if (hero.avoidanceEnemy) {
        const auto found = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy& enemy) {
            return enemy.id == *hero.avoidanceEnemy && enemy.alive && enemy.active;
        });
        if (found != enemies.end() && glm::length(horizontal(hero.avoidancePoint - hero.position)) > 0.12 &&
            navigation_.visible(hero.position, hero.avoidancePoint)) {
            return hero.avoidancePoint;
        }
        hero.avoidanceEnemy.reset();
    }
    const auto offset = horizontal(target - hero.position);
    const double distance = glm::length(offset);
    if (distance < 1e-8) {
        return target;
    }
    const auto forward = offset / distance;
    const glm::dvec2 side{-forward.y, forward.x};
    const Enemy* obstacle = nullptr;
    double nearest = distance;
    for (const auto& enemy : enemies) {
        if (!enemy.alive || !enemy.active || std::abs(hero.position.y - enemy.position.y) > definition_.height) {
            continue;
        }
        const auto relative = horizontal(enemy.position - hero.position);
        const double along = glm::dot(relative, forward);
        const double clearance = definition_.radius + enemy.radius + 0.1;
        if (along > 0 && along < nearest && along < clearance + definition_.speed * 0.35 &&
            std::abs(glm::dot(relative, side)) < clearance) {
            obstacle = &enemy;
            nearest = along;
        }
    }
    if (!obstacle) {
        return target;
    }
    const double clearance = definition_.radius + obstacle->radius + 0.18;
    std::optional<glm::dvec3> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (const double sign : {-1.0, 1.0}) {
        const auto desired = obstacle->position + glm::dvec3{side.x, 0, side.y} * (clearance * sign);
        const auto point = navigation_.project(desired, 0.1);
        if (!point || !navigation_.visible(hero.position, point->position)) {
            continue;
        }
        const double cost = glm::length(horizontal(point->position - hero.position)) +
                            glm::length(horizontal(target - point->position));
        if (cost < bestDistance) {
            bestDistance = cost;
            best = point->position;
        }
    }
    if (!best) {
        return target;
    }
    hero.avoidanceEnemy = obstacle->id;
    hero.avoidancePoint = *best;
    return *best;
}

void HeroSystem::updateModel(Hero& hero) {
    engine_.scene()->setTransform(hero.object, transform(hero.position, hero.yaw));
    if (hero.combat.swinging || hero.health <= 0) {
        return;
    }
    const bool moving = hero.state == HeroState::moving && hero.speed > 0;
    if (moving != hero.animationMoving) {
        const auto selected = moving ? movementClip_ : idleClip_;
        if (selected) {
            engine_.scene()->playAnimation(hero.object, *selected);
        } else {
            engine_.scene()->stopAnimation(hero.object);
        }
    }
    hero.animationMoving = moving;
}

void HeroSystem::playAttack(Hero& hero) {
    if (attackClip_) {
        engine_.scene()->playAnimation(hero.object, *attackClip_, false);
    } else if (idleClip_) {
        engine_.scene()->playAnimation(hero.object, *idleClip_);
    } else {
        engine_.scene()->stopAnimation(hero.object);
    }
}

void HeroSystem::finishAttack(Hero& hero) {
    if (!hero.combat.swinging) {
        return;
    }
    hero.combat.swinging = false;
    if (idleClip_) {
        engine_.scene()->playAnimation(hero.object, *idleClip_);
    } else {
        engine_.scene()->stopAnimation(hero.object);
    }
    hero.animationMoving = false;
}

void HeroSystem::update(double delta, bool movingAllowed, std::span<const Enemy> enemies) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Hero delta must be finite and nonnegative.");
    }
    for (auto& hero : heroes_) {
        if (hero.health > 0 && hero.hitFlashRemaining > 0) {
            hero.hitFlashRemaining = std::max(0.0, hero.hitFlashRemaining - delta);
            if (hero.hitFlashRemaining == 0) {
                engine_.scene()->setTint(hero.object, {1, 1, 1, 1});
            }
        }
        if (!movingAllowed && hero.state == HeroState::moving) {
            stop(hero, HeroState::idle);
        }
        double remaining = delta;
        while (movingAllowed && hero.state == HeroState::moving && remaining > 1e-10) {
            const double stepTime =
                std::min(remaining, std::min(1.0 / 60, definition_.radius * 0.4 / definition_.speed));
            step(hero, stepTime, enemies);
            remaining -= stepTime;
        }
        updateModel(hero);
    }
}

void HeroSystem::preview(std::optional<glm::dvec3> position, bool valid) {
    if (!position) {
        if (preview_) {
            engine_.removeInstance(*preview_);
            preview_.reset();
        }
        return;
    }
    if (!preview_) {
        preview_ = engine_.createInstance(model_, true);
    }
    engine_.setTransform(*preview_, transform(*position, 0));
    engine_.setTint(*preview_, valid ? glm::vec4{0.35F, 1, 0.55F, 0.4F} : glm::vec4{1, 0.25F, 0.2F, 0.4F});
}

std::optional<glm::dvec3> HeroSystem::ground(const Ray& ray) const {
    return TowerSystem::terrainHit(ray, boxes_);
}

Hero* HeroSystem::findMutable(HeroId id) noexcept {
    const auto found = std::find_if(heroes_.begin(), heroes_.end(), [id](const Hero& hero) {
        return hero.id == id;
    });
    return found == heroes_.end() ? nullptr : &*found;
}

const Hero* HeroSystem::find(HeroId id) const noexcept {
    const auto found = std::find_if(heroes_.begin(), heroes_.end(), [id](const Hero& hero) {
        return hero.id == id;
    });
    return found == heroes_.end() ? nullptr : &*found;
}

std::optional<HeroId> HeroSystem::fromObject(ObjectId object) const noexcept {
    for (const auto& hero : heroes_) {
        if (hero.object == object) {
            return hero.id;
        }
    }
    return {};
}

std::string HeroSystem::towerError(const Tower& tower) const {
    const auto box = tower.bounds();
    for (const auto& hero : heroes_) {
        if (hero.health <= 0) {
            continue;
        }
        const auto nearest = glm::clamp(glm::dvec2{hero.position.x, hero.position.z},
                                        glm::dvec2{box.center.x - box.halfExtent.x, box.center.z - box.halfExtent.z},
                                        glm::dvec2{box.center.x + box.halfExtent.x, box.center.z + box.halfExtent.z});
        if (glm::length(nearest - glm::dvec2{hero.position.x, hero.position.z}) < definition_.radius + 0.15) {
            return "Tower overlaps the hero's navigation clearance.";
        }
    }
    return {};
}

} // namespace grimlock::game
