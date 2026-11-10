#include "systems/enemy_system.hpp"

#include "systems/enemy_ai.hpp"
#include "systems/health.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace grimlock::game {

namespace {

bool validSpeed(double speed) {
    return std::isfinite(speed) && speed >= 0 && speed <= 20;
}

std::optional<std::size_t> namedClip(const std::vector<std::string>& names, const std::string& requested) {
    if (requested.empty()) {
        return {};
    }
    const auto found = std::find(names.begin(), names.end(), requested);
    if (found == names.end()) {
        throw std::invalid_argument("Enemy model is missing configured clip: " + requested);
    }
    return static_cast<std::size_t>(found - names.begin());
}

std::optional<std::size_t> deathClip(const std::vector<std::string>& names) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto name = names[index];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name.find("death") != std::string::npos || name.find("die") != std::string::npos) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> movementClip(const std::vector<std::string>& names, const std::string& requested) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == requested) {
            return index;
        }
    }
    for (const auto* gait : {"walk", "run"}) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            auto name = names[index];
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (name.find(gait) != std::string::npos && name.find("start") == std::string::npos &&
                name.find("end") == std::string::npos) {
                return index;
            }
        }
    }
    return std::nullopt; // A static model or an unrelated clip stays at rest.
}

} // namespace

EnemySystem::EnemySystem(Engine& engine, EnemyDefinition definition)
    : engine_(engine), enemies_(registry_.storage<Enemy>()), teams_(registry_.storage<TeamComponent>()) {
    registry_.reserveEntities(maximumEnemies);
    (void)addDefinition(std::move(definition));
}

EnemyTypeId EnemySystem::addDefinition(EnemyDefinition definition) {
    validateEnemyDefinition(definition);
    const auto type = types_.size();
    types_.push_back({std::move(definition), {}, {}, {}, {}, {}});
    return type;
}

EnemySystem::~EnemySystem() {
    clear();
}

void EnemySystem::attackAnimation(Enemy& enemy, bool attacking) {
    auto& playback = engine_.animation(*enemy.instance);
    const auto& animal = types_[enemy.type];
    if (attacking && animal.attackClip) {
        enemy.locomotionClip.reset();
        playback.setSpeed(1);
        playback.play(*animal.attackClip, false);
    } else if (animal.idleClip) {
        playback.setSpeed(animal.definition.animationSpeed);
        playback.play(*animal.idleClip);
        enemy.locomotionClip = animal.idleClip;
    } else {
        if (animal.movementClip) {
            playback.setSpeed(animal.definition.animationSpeed);
            playback.play(*animal.movementClip);
        }
        playback.pause();
    }
    enemy.animationMoving = false;
    if (enemy.hitFlashRemaining <= 0) {
        engine_.setTint(*enemy.instance, attacking ? glm::vec4{1, 0.8F, 0.55F, 1} : glm::vec4{1});
    }
}

void EnemySystem::loadModel(EnemyTypeId type) {
    auto& animal = types_.at(type);
    const auto& definition = animal.definition;
    if (animal.model) {
        return;
    }
    const auto model = engine_.loadModel(engine_.assetRoot() / definition.model);
    auto info = engine_.modelInfo(model);
    if (!std::isfinite(glm::length(info.maximum - info.minimum)) || info.maximum.y <= info.minimum.y) {
        throw std::runtime_error("Enemy model needs finite, nonempty framing bounds.");
    }
    animal.movementClip = movementClip(info.animationNames, definition.movementAnimation);
    animal.idleClip = namedClip(info.animationNames, definition.idleAnimation);
    animal.runClip = namedClip(info.animationNames, definition.runAnimation);
    animal.deathClip = definition.deathAnimation.empty() ? deathClip(info.animationNames)
                                                         : namedClip(info.animationNames, definition.deathAnimation);
    for (std::size_t i = 0; i < info.animationNames.size(); ++i) {
        auto name = info.animationNames[i];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!definition.combat.attackAnimation.empty() &&
            (info.animationNames[i] == definition.combat.attackAnimation ||
             (name.find(definition.combat.attackAnimation) != std::string::npos &&
              name.find("start") == std::string::npos && name.find("end") == std::string::npos))) {
            animal.attackClip = i;
            break;
        }
    }
    if (animal.idleClip) {
        animal.movementClip = namedClip(info.animationNames, definition.movementAnimation);
        animal.attackClip = namedClip(info.animationNames, definition.combat.attackAnimation);
    }
    animal.info = std::move(info);
    animal.model = model;
}

std::size_t EnemySystem::spawn(const NavMesh& navigation, SpawnArea area, std::size_t count, EnemyTypeId type) {
    const auto& definition = types_.at(type).definition;
    if (count > maximumEnemies || !std::isfinite(area.minimum.x) || !std::isfinite(area.minimum.y) ||
        !std::isfinite(area.maximum.x) || !std::isfinite(area.maximum.y) || area.minimum.x > area.maximum.x ||
        area.minimum.y > area.maximum.y || !std::isfinite(area.height)) {
        throw std::invalid_argument("Invalid enemy count or spawn rectangle.");
    }
    if (!navigation.ready()) {
        return 0;
    }
    if (definition.radius > navigation.configuration().agentRadius + 1e-6 ||
        definition.height > navigation.configuration().agentHeight + 1e-6) {
        throw std::invalid_argument("Rebuild the NavMesh for at least this enemy's physical dimensions.");
    }
    const double padding = definition.radius * 0.02;
    const double spacing = definition.radius * 2 + padding;
    if (area.minimum.x + spacing <= area.minimum.x || area.minimum.y + spacing <= area.minimum.y) {
        throw std::invalid_argument("Spawn spacing is too small for this world origin.");
    }
    const auto before = enemies_.size();
    const auto limit = std::min(maximumEnemies, before + count);
    registry_.reserve<Enemy>(limit);
    registry_.reserve<TeamComponent>(limit);
    neighborsDirty_ = true;
    double maximumRadius = definition.radius;
    for (const auto& enemy : enemies_) {
        maximumRadius = std::max(maximumRadius, enemy.radius);
    }
    spawnNeighbors_.build(enemies_, maximumRadius * 2 + padding, glm::dvec3{0}, limit);

    for (double z = area.minimum.y; z <= area.maximum.y && enemies_.size() < limit; z += spacing) {
        for (double x = area.minimum.x; x <= area.maximum.x && enemies_.size() < limit; x += spacing) {
            const auto surface = navigation.project({x, area.height, z}, 0.03);
            if (!surface || !navigation.contains({x, area.height, z}, 0.03)) {
                continue;
            }
            const glm::dvec3 position = surface->position;
            bool occupied = false;
            spawnNeighbors_.visit(position, definition.radius + maximumRadius + padding, [&](std::size_t index) {
                if (enemies_[index].intangible) {
                    return;
                }
                if (position.y >= enemies_[index].position.y + enemies_[index].height ||
                    enemies_[index].position.y >= position.y + definition.height) {
                    return;
                }
                const auto offset = enemies_[index].position - position;
                const double footprint = enemies_[index].radius + definition.radius;
                const double distance = std::max(footprint, footprint + padding - 1e-5);
                occupied = occupied || offset.x * offset.x + offset.z * offset.z < distance * distance;
            });
            if (occupied && !definition.intangible) {
                continue;
            }
            loadModel(type);
            const auto& animal = types_[type];
            Enemy enemy;
            enemy.spawnOrder = nextSpawnOrder_++;
            enemy.type = type;
            enemy.intangible = definition.intangible;
            enemy.navigation = &navigation;
            enemy.despawnOnArrival = definition.despawnOnArrival;
            enemy.position = position;
            enemy.speed = definition.speed;
            enemy.radius = definition.radius;
            enemy.height = definition.height;
            enemy.acceleration = definition.acceleration;
            enemy.deceleration = definition.deceleration;
            enemy.stoppingDistance = definition.stoppingDistance;
            enemy.retryInterval = definition.retryInterval;
            enemy.stuckTimeout = definition.stuckTimeout;
            EnemyAi::setDestination(enemy, sharedDestination_);
            enemy.maximumHealth = enemy.health = definition.maximumHealth;
            const auto entity = registry_.create();
            enemy.id = entity;
            std::optional<InstanceId> instance;
            try {
                instance = engine_.createInstance(*animal.model, true);
                enemy.instance = instance;
                registry_.emplace<Enemy>(entity, std::move(enemy));
                registry_.emplace<TeamComponent>(entity);
            } catch (...) {
                if (instance) {
                    engine_.removeInstance(*instance);
                }
                registry_.destroy(entity);
                throw;
            }
            spawnNeighbors_.insert(enemies_.size() - 1, position);
            auto& playback = engine_.animation(*enemies_.back().instance);
            playback.setSpeed(definition.animationSpeed);
            if (animal.idleClip) {
                playback.play(*animal.idleClip);
                enemies_.back().locomotionClip = animal.idleClip;
            } else if (animal.movementClip) {
                playback.play(*animal.movementClip);
                const auto& clip = playback.model().animations[*animal.movementClip];
                playback.seek(clip.start + (clip.end - clip.start) *
                                               static_cast<float>((enemyOrder(enemies_.back()) * 37) % 100) / 100);
                playback.pause();
            }
            updateModel(enemies_.back());
        }
    }
    return enemies_.size() - before;
}

void EnemySystem::updateModel(Enemy& enemy) {
    const auto& animal = types_.at(enemy.type);
    const auto& definition = animal.definition;
    const auto center = (animal.info.minimum + animal.info.maximum) * 0.5F;
    const float scale = definition.visualHeight / (animal.info.maximum.y - animal.info.minimum.y);
    const float hover = static_cast<float>(
        definition.hoverHeight + definition.hoverAmplitude * std::sin(enemy.visualTime * 3 + enemy.spawnOrder));
    enemy.hoverOffset = hover;
    const auto matrix =
        glm::translate(glm::mat4{1}, glm::vec3{enemy.position} + glm::vec3{0, hover, 0}) *
        glm::mat4_cast(glm::angleAxis(static_cast<float>(enemy.yaw + definition.forwardYaw), glm::vec3{0, 1, 0})) *
        glm::scale(glm::mat4{1}, glm::vec3{scale}) *
        glm::translate(glm::mat4{1}, glm::vec3{-center.x, -animal.info.minimum.y, -center.z});
    engine_.setTransform(*enemy.instance, matrix);
    if (enemy.life != Enemy::Life::living) {
        return;
    }
    const bool moving = enemy.movement == MovementStatus::moving;
    if (!enemy.combat.swinging && animal.idleClip) {
        const auto selected =
            moving ? (animal.runClip && glm::length(enemy.velocity) > enemy.speed * 0.7 ? animal.runClip
                                                                                        : animal.movementClip)
                   : animal.idleClip;
        if (selected && selected != enemy.locomotionClip) {
            auto& playback = engine_.animation(*enemy.instance);
            playback.setSpeed(definition.animationSpeed);
            playback.play(*selected);
            enemy.locomotionClip = selected;
        }
    } else if (!enemy.combat.swinging && animal.movementClip && moving != enemy.animationMoving) {
        auto& playback = engine_.animation(*enemy.instance);
        if (moving) {
            playback.resume();
        } else {
            playback.pause();
        }
    }
    enemy.animationMoving = moving;
}

void EnemySystem::update(const NavMesh& navigation, std::optional<glm::dvec2> destination, double delta) {
    const auto start = std::chrono::steady_clock::now();
    updateEffects(delta);
    if (destination != sharedDestination_) {
        setSharedDestination(destination);
    }
    neighborsDirty_ = true;
    movement_.update(navigation, enemies_, delta);
    metrics_.crowd = movement_.metrics();
    for (std::size_t index = 0; index < enemies_.size();) {
        auto& enemy = enemies_[index];
        if ((!enemy.active || !enemy.alive) && enemy.life == Enemy::Life::living) {
            metrics_.arrived += enemy.movement == MovementStatus::arrived ? 1 : 0;
            removeAt(index);
            continue;
        }
        if (enemy.life == Enemy::Life::living) {
            updateModel(enemy);
        }
        ++index;
    }
    metrics_.updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

bool EnemySystem::canDamage(const Enemy& enemy, DamageType type) const noexcept {
    return enemy.type < types_.size() && enemy.active && enemy.alive && enemy.health > 0 &&
           (!types_[enemy.type].definition.magicOnly || type == DamageType::magic);
}

bool EnemySystem::applyDamage(EnemyId id, float damage, DefensiveTarget attacker, DamageType damageType) {
    validateDamage(damage);
    auto* found = enemies_.get(id);
    if (!found || damage == 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - enemies_.data());
    auto& enemy = *found;
    if (!canDamage(enemy, damageType)) {
        return false;
    }
    receiveDamage(enemy, damage);
    if (attacker.kind != DefensiveTargetKind::none) {
        enemy.combat.attacker = attacker;
        enemy.combat.threatRemaining = definition(enemy.type).combat.threatMemory;
    }
    enemy.hitFlashRemaining = 0.12;
    engine_.setTint(*enemy.instance, {1, 0.35F, 0.3F, 1});
    if (enemy.health > 0) {
        return true;
    }

    const auto& configuration = types_[enemy.type].definition;
    if (configuration.boneHealth > 0 && enemy.life == Enemy::Life::living) {
        const auto objective = enemy.combat.managed ? enemy.combat.objective : enemy.destination;
        const auto height =
            enemy.combat.managed ? std::optional<double>{enemy.combat.objectiveHeight} : enemy.destinationHeight;
        enemy.combat = {};
        EnemyAi::setDestination(enemy, objective);
        enemy.destinationHeight = height;
        enemy.velocity = enemy.desiredVelocity = {0, 0};
        enemy.path.valid = false;
        enemy.movement = MovementStatus::idle;
        enemy.life = Enemy::Life::dying;
        enemy.alive = false;
        enemy.locomotionClip.reset();
        auto& playback = engine_.animation(*enemy.instance);
        enemy.lifeRemaining = 0.25;
        if (const auto clip = types_[enemy.type].deathClip) {
            playback.setSpeed(1);
            playback.play(*clip, false);
            const auto& animation = playback.model().animations[*clip];
            enemy.lifeRemaining = std::max(0.25, static_cast<double>(animation.end - animation.start));
        } else {
            playback.pause();
        }
        engine_.setTint(*enemy.instance, glm::vec4{1});
        return true;
    }

    if (configuration.killReward > std::numeric_limits<std::uint64_t>::max() - metrics_.killGold) {
        throw std::overflow_error("Enemy kill rewards exceed the currency limit.");
    }
    enemy.active = enemy.alive = false;
    enemy.velocity = enemy.desiredVelocity = {0, 0};
    enemy.path.valid = false;
    if (!coinModel_) {
        coinModel_ = engine_.loadModel(engine_.assetRoot() / "environment/dungeon/coin.glb");
    }
    const auto coin = engine_.createInstance(*coinModel_, true);
    try {
        coins_.push_back({coin, enemy.position + glm::dvec3{0, definition(enemy.type).visualHeight * 0.55, 0}});
    } catch (...) {
        engine_.removeInstance(coin);
        throw;
    }
    Transform coinTransform;
    coinTransform.position = coins_.back().origin;
    coinTransform.scale = glm::dvec3{0.7};
    engine_.setTransform(coin, coinTransform);
    engine_.setTint(*enemy.instance, glm::vec4{1});
    auto& playback = engine_.animation(*enemy.instance);
    double duration = 0.25;
    if (const auto clip = types_[enemy.type].deathClip; clip && enemy.life == Enemy::Life::living) {
        playback.setSpeed(1);
        playback.play(*clip, false);
        const auto& animation = playback.model().animations[*clip];
        duration = std::max(duration, static_cast<double>(animation.end - animation.start));
    } else {
        playback.pause();
    }
    // The render-only effect retains the instance; dead enemies leave navigation and targeting immediately.
    deaths_.push_back({*enemy.instance, duration, !types_[enemy.type].definition.deathAnimation.empty()});
    enemy.instance.reset();
    ++metrics_.defeated;
    metrics_.killGold += configuration.killReward;
    removeAt(index);
    return true;
}

void EnemySystem::updateEffects(double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Enemy effects delta must be finite and nonnegative.");
    }
    registry_.each<Enemy, TeamComponent>([&](ecs::Entity, Enemy& enemy, const TeamComponent&) {
        enemy.visualTime += delta;
        if (enemy.life != Enemy::Life::living) {
            enemy.lifeRemaining -= delta;
            if (enemy.life == Enemy::Life::dying && enemy.lifeRemaining <= 0 &&
                !engine_.animation(*enemy.instance).running()) {
                enemy.life = Enemy::Life::bones;
                enemy.alive = true;
                enemy.maximumHealth = enemy.health = definition(enemy.type).boneHealth;
                enemy.lifeRemaining = definition(enemy.type).resurrectionTime;
                engine_.animation(*enemy.instance).pause();
                // The Kenney library has no separate bones prop. Flatten its completed death pose.
                auto pile = engine_.transform(*enemy.instance);
                const auto origin = glm::vec3{enemy.position};
                pile = glm::translate(glm::mat4{1}, origin) * glm::scale(glm::mat4{1}, glm::vec3{1.1F, 0.25F, 1.1F}) *
                       glm::translate(glm::mat4{1}, -origin) * pile;
                engine_.setTransform(*enemy.instance, pile);
            } else if (enemy.life == Enemy::Life::bones && enemy.lifeRemaining <= 0) {
                enemy.life = Enemy::Life::living;
                enemy.maximumHealth = enemy.health = definition(enemy.type).maximumHealth;
                enemy.combat = {};
                EnemyAi::setDestination(enemy, enemy.destination);
                enemy.locomotionClip.reset();
                enemy.animationMoving = false;
                updateModel(enemy);
            }
        }
        if (enemy.hitFlashRemaining > 0) {
            enemy.hitFlashRemaining = std::max(0.0, enemy.hitFlashRemaining - delta);
            if (enemy.hitFlashRemaining == 0) {
                engine_.setTint(*enemy.instance, glm::vec4{1});
            }
        }
    });
    for (std::size_t index = 0; index < deaths_.size();) {
        auto& death = deaths_[index];
        death.remaining -= delta;
        if (death.remaining > 0 || (death.finishAnimation && engine_.animation(death.instance).running())) {
            ++index;
            continue;
        }
        death.dissolveTime += delta;
        if (death.dissolveTime < 0.65) {
            engine_.setDissolve(death.instance, static_cast<float>(death.dissolveTime / 0.65));
            ++index;
            continue;
        }
        engine_.removeInstance(death.instance);
        death = deaths_.back();
        deaths_.pop_back();
    }
    for (std::size_t index = 0; index < coins_.size();) {
        auto& coin = coins_[index];
        coin.age += delta;
        constexpr double duration = 1.25;
        const auto progress = coin.age / duration;
        const double scale = 0.7 * std::min(1.0, (1 - progress) / 0.25);
        // Retire subpixel coins before their shrinking transform becomes effectively singular.
        if (scale < 0.01) {
            engine_.removeInstance(coin.instance);
            coin = coins_.back();
            coins_.pop_back();
            continue;
        }
        Transform transform;
        transform.position = coin.origin + glm::dvec3{0, 1.4 * (1 - (1 - progress) * (1 - progress)), 0};
        transform.rotation = glm::angleAxis(coin.age * 9, glm::dvec3{0, 1, 0});
        transform.scale = glm::dvec3{scale};
        engine_.setTransform(coin.instance, transform);
        ++index;
    }
}

void EnemySystem::removeAt(std::size_t index) noexcept {
    neighborsDirty_ = true;
    const auto entity = enemies_.entities()[index];
    if (enemies_[index].instance) {
        engine_.removeInstance(*enemies_[index].instance);
    }
    registry_.destroy(entity);
}

bool EnemySystem::remove(EnemyId id) noexcept {
    const auto* found = enemies_.get(id);
    if (!found) {
        return false;
    }
    removeAt(static_cast<std::size_t>(found - enemies_.data()));
    return true;
}

void EnemySystem::clear() noexcept {
    movement_.setObstacles({});
    while (!enemies_.empty()) {
        removeAt(enemies_.size() - 1);
    }
    for (const auto& death : deaths_) {
        engine_.removeInstance(death.instance);
    }
    deaths_.clear();
    for (const auto& coin : coins_) {
        engine_.removeInstance(coin.instance);
    }
    coins_.clear();
    metrics_ = {};
}

void EnemySystem::setSpawnSpeed(double speed, EnemyTypeId type) {
    if (!validSpeed(speed)) {
        throw std::invalid_argument("Enemy speed must be between 0 and 20.");
    }
    types_.at(type).definition.speed = speed;
}

void EnemySystem::setSpeed(double speed, EnemyTypeId type) {
    setSpawnSpeed(speed, type);
    for (auto& enemy : enemies_) {
        if (enemy.type == type) {
            enemy.speed = speed;
        }
    }
}

void EnemySystem::setSharedDestination(std::optional<glm::dvec2> destination) {
    // Validate even when no enemies exist.
    if (destination && (!std::isfinite(destination->x) || !std::isfinite(destination->y))) {
        throw std::invalid_argument("Enemy destination must contain finite world X/Z coordinates.");
    }
    sharedDestination_ = destination;
    for (auto& enemy : enemies_) {
        EnemyAi::setDestination(enemy, destination);
    }
}

void EnemySystem::setDestinationHeight(double height) {
    if (!std::isfinite(height)) {
        throw std::invalid_argument("Enemy destination height must be finite.");
    }
    for (auto& enemy : enemies_) {
        enemy.combat.objectiveHeight = height;
        if (!enemy.combat.managed && enemy.destinationHeight != height) {
            enemy.destinationHeight = height;
            EnemyAi::setDestination(enemy, enemy.destination);
        }
    }
}

bool EnemySystem::setDestination(EnemyId id, std::optional<glm::dvec2> destination) {
    auto* found = enemies_.get(id);
    if (!found) {
        return false;
    }
    EnemyAi::setDestination(*found, destination);
    return true;
}

const Enemy* EnemySystem::find(EnemyId id) const noexcept {
    return enemies_.get(id);
}

} // namespace grimlock::game
