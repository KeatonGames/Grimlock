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
    hero.avoidanceHero.reset();
    hero.state = state;
    hero.speed = hero.stuckTime = 0;
    hero.destination.reset();
    hero.path.valid = false;
}
} // namespace

HeroSystem::HeroSystem(Engine& engine, std::optional<std::span<const std::string>> availableHeroes)
    : engine_(engine), scene_(engine.scene()), projectiles_(engine), heroes_(registry_.storage<Hero>()),
      teams_(registry_.storage<TeamComponent>()) {
    registry_.reserveEntities(8);
    registry_.reserve<Hero>(8);
    registry_.reserve<TeamComponent>(8);
    for (auto definition : loadHeroDefinitions(engine.assetRoot())) {
        if (availableHeroes && !std::any_of(availableHeroes->begin(), availableHeroes->end(), [&](const auto& id) {
                // The Knight retains the original Assassin type key in the character catalog.
                return definition.id == id || (id == "knight" && definition.id == "assassin");
            })) {
            continue;
        }
        auto type = std::make_unique<TypeResources>();
        type->definition = std::move(definition);
        type->model = engine.loadModel(engine.assetRoot() / type->definition.model);
        type->modelInfo = engine.modelInfo(type->model);
        if (!scene_ || !std::isfinite(glm::length(type->modelInfo.maximum - type->modelInfo.minimum)) ||
            type->modelInfo.maximum.y <= type->modelInfo.minimum.y) {
            throw std::invalid_argument("Hero requires an active scene and finite model bounds.");
        }
        type->idleClip = clip(type->modelInfo, type->definition.idleAnimation);
        type->attackClip = clip(type->modelInfo, type->definition.combat.attackAnimation);
        type->releaseClip = clip(type->modelInfo, type->definition.combat.releaseAnimation);
        type->aimingClip = clip(type->modelInfo, type->definition.combat.aimingAnimation);
        type->hitClip = clip(type->modelInfo, type->definition.hitAnimation);
        type->walkClip = clip(type->modelInfo, type->definition.walkAnimation);
        type->deathClip = clip(type->modelInfo, type->definition.deathAnimation);
        if (!type->deathClip) {
            type->deathClip = clip(type->modelInfo, "die");
        }
        type->movementClip = clip(type->modelInfo, type->definition.runAnimation);
        if (!type->movementClip) {
            type->movementClip = clip(type->modelInfo, type->definition.walkAnimation);
        }
        types_.push_back(std::move(type));
    }
    if (availableHeroes && types_.size() != availableHeroes->size()) {
        throw std::invalid_argument("Hero availability contains duplicate or unknown character IDs.");
    }
}

HeroSystem::~HeroSystem() {
    clear();
}

void HeroSystem::clear() noexcept {
    preview({});
    projectiles_.clear();
    if (engine_.scene() == scene_) {
        for (const auto& hero : heroes_) {
            if (engine_.scene()->contains(hero.object)) {
                engine_.scene()->removeObject(hero.object);
            }
        }
    }
    registry_.clear();
}

bool HeroSystem::remove(HeroId id) noexcept {
    const auto* hero = heroes_.get(id);
    if (!hero) {
        return false;
    }
    if (engine_.scene() == scene_ && engine_.scene()->contains(hero->object)) {
        engine_.scene()->removeObject(hero->object);
    }
    return registry_.destroy(id);
}

void HeroSystem::synchronize(const TowerSystem& towers) {
    if (!sharedGeometry_) {
        combatGeometry_.synchronize(engine_, towers.towers());
    }
    if (engine_.scene() != scene_) {
        throw std::logic_error("Reset heroes when replacing a scene.");
    }
    for (std::size_t index = 0; index < heroes_.size();) {
        if (!engine_.scene()->contains(heroes_[index].object)) {
            registry_.destroy(heroes_[index].id);
        } else {
            ++index;
        }
    }
    levelGeometry_.synchronize(engine_, true);
    if (geometryRevision_ == levelGeometry_.revision() && towerRevision_ == towers.revision()) {
        return;
    }
    auto geometry = levelGeometry_.navigation();
    for (const auto& tower : towers.towers()) {
        glm::mat4 transform{glm::mat4_cast(tower.rotation)};
        transform[3] = glm::vec4{tower.position, 1};
        geometry.addBox(-glm::vec3{tower.halfExtent}, glm::vec3{tower.halfExtent}, transform);
    }
    for (auto& type : types_) {
        NavMeshConfiguration config;
        config.agentRadius = static_cast<float>(type->definition.radius);
        config.agentHeight = static_cast<float>(type->definition.height);
        if (!type->navigation.build(geometry, config)) {
            throw std::runtime_error("Cannot build hero navigation: " + type->navigation.status());
        }
    }
    geometryRevision_ = levelGeometry_.revision();
    towerRevision_ = towers.revision();
}

bool HeroSystem::validPosition(glm::dvec3 position, std::optional<HeroId> ignore, std::size_t type) const {
    const auto& config = definition(type);
    const auto& mesh = navigation(type);
    if (!mesh.contains(position)) {
        return false;
    }
    for (const auto& hero : heroes_) {
        if (ignore == hero.id || hero.health <= 0) {
            continue;
        }
        if (std::abs(hero.position.y - position.y) >= std::max(config.height, definition(hero).height)) {
            continue;
        }
        if (glm::length(glm::dvec2{hero.position.x - position.x, hero.position.z - position.z}) <
            (config.radius + definition(hero).radius)) {
            return false;
        }
    }
    return true;
}

Transform HeroSystem::transform(glm::dvec3 position, double yaw, std::size_t type) const {
    const auto& config = definition(type);
    const auto& modelInfo = types_.at(type)->modelInfo;
    const auto center = (modelInfo.minimum + modelInfo.maximum) * 0.5F;
    const float scale = config.visualHeight / (modelInfo.maximum.y - modelInfo.minimum.y);
    Transform result;
    result.rotation = glm::angleAxis(static_cast<float>(yaw + config.forwardYaw), glm::vec3{0, 1, 0});
    result.scale = glm::vec3{scale};
    result.position =
        glm::vec3{position} + result.rotation * (glm::vec3{-center.x, -modelInfo.minimum.y, -center.z} * scale);
    return result;
}

HeroId HeroSystem::deploy(glm::dvec3 position, std::optional<HeroId> existing, std::size_t type) {
    if (existing) {
        const auto* hero = find(*existing);
        if (!hero) {
            throw std::invalid_argument("Unknown hero.");
        }
        type = hero->type;
    }
    const auto& config = definition(type);
    const auto& mesh = navigation(type);
    const auto idleClip = types_.at(type)->idleClip;
    if (!validPosition(position, existing, type)) {
        throw std::invalid_argument("Deploy on clear navigable ground.");
    }
    position = mesh.project(position, 0.03)->position;
    Hero* hero = existing ? findMutable(*existing) : nullptr;
    if (existing && !hero) {
        throw std::invalid_argument("Unknown hero.");
    }
    if (hero && hero->health <= 0) {
        throw std::invalid_argument("A dead hero cannot be redeployed. Restart the level to restore heroes.");
    }
    if (!hero) {
        const auto object = engine_.scene()->createObject(config.name);
        ecs::Entity entity = 0;
        try {
            engine_.scene()->setRole(object, "grimlock.hero");
            engine_.scene()->setSelectionHighlight(object, false);
            engine_.scene()->setModel(object, config.model);
            engine_.prepareScene(*engine_.scene());
            auto& resources = *types_.at(type);
            if (!config.combat.projectileSocket.empty() && !resources.attackPose) {
                auto pose = std::make_unique<assets::ModelInstance>(engine_.scene()->animation(object).asset());
                std::optional<std::size_t> socket;
                const auto& nodes = pose->model().nodes;
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    if (nodes[i].name == config.combat.projectileSocket) {
                        socket = i;
                        break;
                    }
                }
                if (!socket) {
                    throw std::invalid_argument("Hero projectile socket is missing from the model.");
                }
                resources.projectileNode = socket;
                resources.attackPose = std::move(pose);
            }
            Hero created;
            created.type = type;
            entity = registry_.create();
            created.id = entity;
            created.spawnOrder = nextSpawnOrder_++;
            created.object = object;
            created.health = created.maximumHealth = config.maximumHealth;
            registry_.emplace<Hero>(entity, std::move(created));
            registry_.emplace<TeamComponent>(entity, TeamComponent{Team::player});
        } catch (...) {
            registry_.destroy(entity);
            engine_.scene()->removeObject(object);
            throw;
        }
        hero = &heroes_.back();
    }
    hero->position = position;
    hero->visualYaw = hero->yaw;
    hero->avoidanceEnemy.reset();
    hero->defensivePosition = position;
    hero->combat = {};
    hero->hitAnimationRemaining = 0;
    hero->locomotionClip = idleClip;
    stop(*hero, HeroState::idle);
    updateModel(*hero);
    if (idleClip) {
        engine_.scene()->playAnimation(hero->object, *idleClip);
    }
    return hero->id;
}

bool HeroSystem::command(HeroId id, glm::dvec3 destination) {
    auto* hero = findMutable(id);
    if (!hero || !hero->controllable || hero->health <= 0) {
        return false;
    }
    const auto& mesh = navigation(hero->type);
    if (hero->combat.playerMoving && hero->destination && hero->path.revision == mesh.revision() &&
        glm::length(destination - *hero->destination) <= definition(*hero).stoppingDistance) {
        return true;
    }
    // Reject invalid orders before interrupting the current move or attack.
    if (!mesh.contains(destination) || !mesh.findPath(hero->position, destination, commandPath_)) {
        return false;
    }
    finishAttack(*hero);
    hero->combat.target.reset();
    hero->combat.excludedTarget.reset();
    hero->combat.playerMoving = true;
    hero->combat.returning = false;
    std::swap(hero->path, commandPath_);
    hero->destination = hero->path.destination;
    hero->defensivePosition = *hero->destination;
    hero->state = HeroState::moving;
    hero->stuckTime = 0;
    hero->progressRemaining = std::numeric_limits<double>::max();
    hero->recoveryAttempts = 0;
    hero->blockage = MovementBlockage::none;
    hero->avoidanceEnemy.reset();
    hero->avoidanceHero.reset();
    return true;
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
    hero->defensivePosition = hero->position;
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
    const auto deathClip = types_.at(hero->type)->deathClip;
    if (receiveDamage(*hero, damage)) {
        finishAttack(*hero);
        hero->combat = {};
        stop(*hero, HeroState::idle);
        hero->controllable = false;
        engine_.scene()->setTint(hero->object, {0.4F, 0.35F, 0.35F, 1});
        if (deathClip) {
            engine_.scene()->playAnimation(hero->object, *deathClip, false);
        } else {
            engine_.scene()->stopAnimation(hero->object);
        }
    } else {
        const auto hitClip = types_.at(hero->type)->hitClip;
        if (hitClip && !hero->combat.swinging && hero->hitAnimationRemaining <= 0) {
            engine_.scene()->playAnimation(hero->object, *hitClip, false);
            const auto& animation = engine_.scene()->animation(hero->object).model().animations.at(*hitClip);
            hero->hitAnimationRemaining = animation.end - animation.start;
            hero->locomotionClip.reset();
        }
        hero->hitFlashRemaining = 0.15;
        engine_.scene()->setTint(hero->object, {1, 0.35F, 0.3F, 1});
    }
    return true;
}

bool HeroSystem::navigate(Hero& hero, glm::dvec3 destination) {
    const auto& mesh = navigation(hero.type);
    if (!mesh.contains(destination) || !mesh.findPath(hero.position, destination, hero.path)) {
        stop(hero, HeroState::blocked);
        updateModel(hero);
        return false;
    }
    hero.destination = hero.path.destination;
    hero.state = HeroState::moving;
    hero.stuckTime = 0;
    hero.progressRemaining = std::numeric_limits<double>::max();
    hero.recoveryAttempts = 0;
    hero.blockage = MovementBlockage::none;
    hero.avoidanceEnemy.reset();
    hero.avoidanceHero.reset();
    return true;
}

void HeroSystem::step(Hero& hero, double delta, std::span<const Enemy> enemies) {
    const auto& config = definition(hero);
    const auto& mesh = navigation(hero.type);
    if (!hero.destination || hero.health <= 0 || !hero.controllable) {
        stop(hero, HeroState::idle);
        return;
    }
    if (hero.path.revision != mesh.revision() && !mesh.findPath(hero.position, *hero.destination, hero.path)) {
        stop(hero, HeroState::blocked);
        return;
    }
    double remaining = 0;
    auto target = mesh.steering(hero.position, hero.path, &remaining);
    // Surface clipping can enter an adjacent polygon outside the cached corridor near a corner.
    if (!target && mesh.findPath(hero.position, *hero.destination, hero.path)) {
        target = mesh.steering(hero.position, hero.path, &remaining);
    }
    if (!target) {
        hero.blockage = MovementBlockage::navigation;
        stop(hero, HeroState::blocked);
        return;
    }
    if (remaining <= config.stoppingDistance) {
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
    const auto desired = std::min(config.speed, std::sqrt(2 * config.deceleration * remaining));
    hero.speed = std::clamp(desired,
                            std::max(0.0, hero.speed - config.deceleration * delta),
                            hero.speed + config.acceleration * delta);
    auto displacement = direction * std::min({remaining, distance, hero.speed * delta});
    auto next = hero.position;
    bool contactedUnit = false;
    for (int contact = 0; contact < 4 && glm::length(displacement) > 1e-10; ++contact) {
        const auto surface = mesh.move(next, displacement);
        displacement = {surface.x - next.x, surface.z - next.z};
        std::optional<glm::dvec3> neighbor;
        double fraction = 1;
        const auto collide = [&](glm::dvec3 position, double radius, double height) {
            if (next.y >= position.y + height || position.y >= next.y + config.height) {
                return;
            }
            const double t =
                contactFraction({next.x - position.x, next.z - position.z}, displacement, config.radius + radius);
            if (t < fraction) {
                fraction = t;
                neighbor = position;
            }
        };
        for (const auto& enemy : enemies) {
            if (!enemy.active || !enemy.alive || enemy.intangible ||
                (next.y >= enemy.position.y + enemy.height || enemy.position.y >= next.y + config.height)) {
                continue;
            }
            collide(enemy.position, enemy.radius, enemy.height);
        }
        for (const auto& other : heroes_) {
            if (other.id != hero.id && other.health > 0) {
                collide(other.position, definition(other).radius, definition(other).height);
            }
        }
        next = mesh.move(next, displacement * fraction);
        if (!neighbor) {
            break;
        }
        contactedUnit = true;
        const glm::dvec2 contactOffset{next.x - neighbor->x, next.z - neighbor->z};
        const auto normal = glm::length(contactOffset) > 1e-8 ? glm::normalize(contactOffset) : glm::dvec2{1, 0};
        displacement *= 1 - fraction;
        displacement -= normal * std::min(0.0, glm::dot(displacement, normal));
    }
    const auto actual = next - hero.position;
    const double traveled = glm::length(actual);
    if (remaining < hero.progressRemaining - config.radius * 0.25) {
        hero.progressRemaining = remaining;
        hero.stuckTime = 0;
    } else {
        hero.stuckTime += delta;
    }
    hero.position = next;
    if (hero.stuckTime > 1) {
        hero.blockage = contactedUnit ? MovementBlockage::crowd : MovementBlockage::noProgress;
        hero.avoidanceEnemy.reset();
        hero.avoidanceHero.reset();
        hero.stuckTime = 0;
        hero.progressRemaining = std::numeric_limits<double>::max();
        ++hero.recoveryAttempts;
        if (hero.recoveryAttempts > 3 || !mesh.findPath(hero.position, *hero.destination, hero.path)) {
            stop(hero, HeroState::blocked);
        }
        return;
    }
    if (traveled > 1e-6) {
        const auto yaw = std::atan2(actual.x, actual.z);
        hero.yaw += std::remainder(yaw - hero.yaw, 2 * std::acos(-1.0)) * (1 - std::exp(-config.turningSpeed * delta));
    }
}

glm::dvec3 HeroSystem::steerAroundEnemies(Hero& hero, glm::dvec3 target, std::span<const Enemy> enemies) const {
    const auto& config = definition(hero);
    const auto& mesh = navigation(hero.type);
    const auto horizontal = [](glm::dvec3 value) {
        return glm::dvec2{value.x, value.z};
    };
    if (hero.avoidanceEnemy || hero.avoidanceHero) {
        const auto found = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy& enemy) {
            return hero.avoidanceEnemy && enemy.id == *hero.avoidanceEnemy && enemy.alive && enemy.active &&
                   !enemy.intangible;
        });
        const auto* other = hero.avoidanceHero ? find(*hero.avoidanceHero) : nullptr;
        if ((found != enemies.end() || (other && other->health > 0)) &&
            glm::length(horizontal(hero.avoidancePoint - hero.position)) > 0.12 &&
            mesh.visible(hero.position, hero.avoidancePoint)) {
            return hero.avoidancePoint;
        }
        hero.avoidanceEnemy.reset();
        hero.avoidanceHero.reset();
    }
    const auto offset = horizontal(target - hero.position);
    const double distance = glm::length(offset);
    if (distance < 1e-8) {
        return target;
    }
    const auto forward = offset / distance;
    const glm::dvec2 side{-forward.y, forward.x};
    std::optional<MovementBody> obstacle;
    std::optional<HeroId> obstacleHero;
    double nearest = distance;
    for (const auto& enemy : enemies) {
        if (!enemy.alive || !enemy.active || enemy.intangible ||
            (hero.position.y >= enemy.position.y + enemy.height ||
             enemy.position.y >= hero.position.y + config.height)) {
            continue;
        }
        const auto relative = horizontal(enemy.position - hero.position);
        const double along = glm::dot(relative, forward);
        const double clearance = config.radius + enemy.radius + 0.1;
        if (along > 0 && along < nearest && along < clearance + config.speed * 0.35 &&
            std::abs(glm::dot(relative, side)) < clearance) {
            obstacle = MovementBody{enemy.position, enemy.radius, enemy.height, enemy.id};
            nearest = along;
        }
    }
    for (const auto& other : heroes_) {
        if (other.id == hero.id || other.health <= 0 ||
            hero.position.y >= other.position.y + definition(other).height ||
            other.position.y >= hero.position.y + config.height) {
            continue;
        }
        const auto relative = horizontal(other.position - hero.position);
        const double along = glm::dot(relative, forward);
        const double clearance = config.radius + definition(other).radius + 0.1;
        if (along > 0 && along < nearest && along < clearance + config.speed * 0.35 &&
            std::abs(glm::dot(relative, side)) < clearance) {
            obstacle = MovementBody{other.position, definition(other).radius, definition(other).height, other.id};
            obstacleHero = other.id;
            nearest = along;
        }
    }
    if (!obstacle) {
        return target;
    }
    const double clearance = config.radius + obstacle->radius + 0.18;
    std::optional<glm::dvec3> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (const double sign : {-1.0, 1.0}) {
        const auto desired = obstacle->position + glm::dvec3{side.x, 0, side.y} * (clearance * sign);
        const auto point = mesh.project(desired, 0.1);
        if (!point || !mesh.visible(hero.position, point->position)) {
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
    hero.avoidanceEnemy = obstacleHero ? std::nullopt : std::optional{obstacle->id};
    hero.avoidanceHero = obstacleHero;
    hero.avoidancePoint = *best;
    return *best;
}

void HeroSystem::updateModel(Hero& hero) {
    const auto& type = *types_.at(hero.type);
    engine_.scene()->setTransform(hero.object, transform(hero.position, hero.visualYaw, hero.type));
    if (hero.combat.swinging || hero.health <= 0 || hero.hitAnimationRemaining > 0) {
        return;
    }
    const bool moving = hero.state == HeroState::moving && hero.speed > 0;
    const auto selected =
        moving ? (hero.speed < type.definition.speed * 0.55 && type.walkClip ? type.walkClip : type.movementClip)
               : (hero.combat.target && type.aimingClip ? type.aimingClip : type.idleClip);
    if (selected != hero.locomotionClip) {
        hero.locomotionClip = selected;
        if (selected) {
            engine_.scene()->playAnimation(hero.object, *selected);
        } else {
            engine_.scene()->stopAnimation(hero.object);
        }
    }
    hero.animationMoving = moving;
}

void HeroSystem::playAttack(Hero& hero) {
    const auto& type = *types_.at(hero.type);
    hero.hitAnimationRemaining = 0;
    hero.locomotionClip.reset();
    hero.combat.drawRemaining = -1;
    if (type.aimingClip && type.definition.combat.aimingTime > 0) {
        hero.combat.drawRemaining = type.definition.combat.aimingTime;
        engine_.scene()->playAnimation(hero.object, *type.aimingClip);
    } else if (type.attackClip) {
        engine_.scene()->playAnimation(hero.object, *type.attackClip, false);
    } else if (type.idleClip) {
        engine_.scene()->playAnimation(hero.object, *type.idleClip);
    } else {
        engine_.scene()->stopAnimation(hero.object);
    }
}

void HeroSystem::finishAttack(Hero& hero) {
    const auto& type = *types_.at(hero.type);
    if (!hero.combat.swinging) {
        return;
    }
    hero.combat.swinging = false;
    const auto idle = hero.combat.target && type.aimingClip
                          ? type.aimingClip
                          : (hero.combat.target && type.aimingClip ? type.aimingClip : type.idleClip);
    if (idle) {
        engine_.scene()->playAnimation(hero.object, *idle);
    } else {
        engine_.scene()->stopAnimation(hero.object);
    }
    hero.animationMoving = false;
    hero.locomotionClip = idle;
}

glm::dvec3 HeroSystem::projectileOrigin(const Hero& hero) {
    auto& type = *types_.at(hero.type);
    if (!type.projectileNode || !type.attackPose || !type.attackClip) {
        return hero.position + glm::dvec3{0, type.definition.height * 0.5, 0};
    }
    // Sample the contact pose from the combat clock, independent of render frame rate.
    type.attackPose->play(*type.attackClip, false);
    type.attackPose->seek(static_cast<float>(type.definition.combat.impactTime - type.definition.combat.aimingTime));
    const auto local =
        type.attackPose->pose().world.at(*type.projectileNode) * glm::vec4{type.definition.combat.projectileOffset, 1};
    return glm::dvec3{engine_.scene()->worldTransform(hero.object) * local};
}

void HeroSystem::update(double delta, bool movingAllowed, std::span<const Enemy> enemies) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Hero delta must be finite and nonnegative.");
    }
    for (auto& hero : heroes_) {
        hero.hitAnimationRemaining = std::max(0.0, hero.hitAnimationRemaining - delta);
        if (hero.health > 0 && hero.hitFlashRemaining > 0) {
            hero.hitFlashRemaining = std::max(0.0, hero.hitFlashRemaining - delta);
            if (hero.hitFlashRemaining == 0) {
                engine_.scene()->setTint(hero.object, {1, 1, 1, 1});
            }
        }
        if (!movingAllowed && hero.state == HeroState::moving) {
            stop(hero, HeroState::idle);
        }
        const auto& config = definition(hero);
        double remaining = delta;
        while (movingAllowed && hero.state == HeroState::moving && remaining > 1e-10) {
            const double stepTime = std::min(remaining, std::min(1.0 / 60, config.radius * 0.4 / config.speed));
            step(hero, stepTime, enemies);
            remaining -= stepTime;
        }
        hero.visualYaw += std::remainder(hero.yaw - hero.visualYaw, 2 * std::acos(-1.0)) *
                          (1 - std::exp(-config.turningSpeed * delta));
        updateModel(hero);
    }
}

void HeroSystem::preview(std::optional<glm::dvec3> position, bool valid, std::size_t type) {
    if (!position) {
        if (preview_) {
            engine_.removeInstance(*preview_);
            preview_.reset();
        }
        return;
    }
    if (preview_ && previewType_ != type) {
        preview({});
    }
    if (!preview_) {
        preview_ = engine_.createInstance(types_.at(type)->model, true);
        previewType_ = type;
    }
    engine_.setTransform(*preview_, transform(*position, 0, type));
    engine_.setTint(*preview_, valid ? glm::vec4{0.35F, 1, 0.55F, 0.4F} : glm::vec4{1, 0.25F, 0.2F, 0.4F});
}

std::optional<glm::dvec3> HeroSystem::ground(const Ray& ray) const {
    return levelGeometry_.intersect(ray);
}

Hero* HeroSystem::findMutable(HeroId id) noexcept {
    return const_cast<Hero*>(find(id));
}

const Hero* HeroSystem::find(HeroId id) const noexcept {
    const auto* hero = heroes_.get(id);
    return hero && engine_.scene() == scene_ && scene_ && scene_->contains(hero->object) ? hero : nullptr;
}

std::optional<HeroId> HeroSystem::fromObject(ObjectId object) const noexcept {
    for (const auto& hero : heroes_) {
        if (hero.object == object && find(hero.id)) {
            return hero.id;
        }
    }
    return {};
}

std::string HeroSystem::towerError(const Tower& tower) const {
    const auto box = tower.bounds();
    for (const auto& hero : heroes_) {
        const auto& config = definition(hero);
        if (hero.health <= 0 || hero.position.y >= box.center.y + box.halfExtent.y ||
            hero.position.y + config.height <= box.center.y - box.halfExtent.y) {
            continue;
        }
        const auto nearest = glm::clamp(glm::dvec2{hero.position.x, hero.position.z},
                                        glm::dvec2{box.center.x - box.halfExtent.x, box.center.z - box.halfExtent.z},
                                        glm::dvec2{box.center.x + box.halfExtent.x, box.center.z + box.halfExtent.z});
        if (glm::length(nearest - glm::dvec2{hero.position.x, hero.position.z}) < config.radius + 0.15) {
            return "Tower overlaps the hero's navigation clearance.";
        }
    }
    return {};
}

} // namespace grimlock::game
