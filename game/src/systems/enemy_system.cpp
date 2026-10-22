#include "systems/enemy_system.hpp"

#include "systems/enemy_ai.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <utility>

namespace grimlock::game {

namespace {

bool validSpeed(double speed) {
    return std::isfinite(speed) && speed >= 0 && speed <= 20;
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

EnemySystem::EnemySystem(Engine& engine, EnemyDefinition definition) : engine_(engine) {
    (void)addDefinition(std::move(definition));
}

EnemyTypeId EnemySystem::addDefinition(EnemyDefinition definition) {
    validateEnemyDefinition(definition);
    const auto type = types_.size();
    types_.push_back({std::move(definition), {}, {}, {}});
    return type;
}

EnemySystem::~EnemySystem() {
    clear();
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
    enemies_.reserve(limit);
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
                const auto offset = enemies_[index].position - position;
                const double footprint = enemies_[index].radius + definition.radius;
                const double distance = std::max(footprint, footprint + padding - 1e-5);
                occupied = occupied || offset.x * offset.x + offset.z * offset.z < distance * distance;
            });
            if (occupied) {
                continue;
            }
            loadModel(type);
            const auto& animal = types_[type];
            Enemy enemy;
            enemy.id = nextId_++;
            enemy.type = type;
            enemy.navigation = &navigation;
            enemy.despawnOnArrival = definition.despawnOnArrival;
            enemy.position = position;
            enemy.speed = definition.speed;
            enemy.radius = definition.radius;
            enemy.acceleration = definition.acceleration;
            enemy.deceleration = definition.deceleration;
            enemy.stoppingDistance = definition.stoppingDistance;
            enemy.retryInterval = definition.retryInterval;
            enemy.stuckTimeout = definition.stuckTimeout;
            EnemyAi::setDestination(enemy, sharedDestination_);
            enemy.health = definition.maximumHealth;
            enemy.instance = engine_.createInstance(*animal.model, true);
            enemies_.push_back(std::move(enemy));
            spawnNeighbors_.insert(enemies_.size() - 1, position);
            auto& playback = engine_.animation(*enemies_.back().instance);
            playback.setSpeed(definition.animationSpeed);
            if (animal.movementClip) {
                playback.play(*animal.movementClip);
                const auto& clip = playback.model().animations[*animal.movementClip];
                playback.seek(clip.start + (clip.end - clip.start) * static_cast<float>((enemy.id * 37) % 100) / 100);
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
    const auto matrix =
        glm::translate(glm::mat4{1}, glm::vec3{enemy.position}) *
        glm::mat4_cast(glm::angleAxis(static_cast<float>(enemy.yaw + definition.forwardYaw), glm::vec3{0, 1, 0})) *
        glm::scale(glm::mat4{1}, glm::vec3{scale}) *
        glm::translate(glm::mat4{1}, glm::vec3{-center.x, -animal.info.minimum.y, -center.z});
    engine_.setTransform(*enemy.instance, matrix);
    const bool moving = enemy.movement == MovementStatus::moving;
    if (animal.movementClip && moving != enemy.animationMoving) {
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
    if (destination != sharedDestination_) {
        setSharedDestination(destination);
    }
    movement_.update(navigation, enemies_, delta);
    metrics_.crowd = movement_.metrics();
    for (std::size_t index = 0; index < enemies_.size();) {
        auto& enemy = enemies_[index];
        if (!enemy.active || !enemy.alive) {
            metrics_.arrived += enemy.movement == MovementStatus::arrived ? 1 : 0;
            removeAt(index);
            continue;
        }
        updateModel(enemy);
        ++index;
    }
    metrics_.updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void EnemySystem::removeAt(std::size_t index) noexcept {
    if (enemies_[index].instance) {
        engine_.removeInstance(*enemies_[index].instance);
    }
    if (index != enemies_.size() - 1) {
        enemies_[index] = std::move(enemies_.back());
    }
    enemies_.pop_back();
}

bool EnemySystem::remove(EnemyId id) noexcept {
    const auto found = std::find_if(enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
        return enemy.id == id;
    });
    if (found == enemies_.end()) {
        return false;
    }
    removeAt(static_cast<std::size_t>(found - enemies_.begin()));
    return true;
}

void EnemySystem::clear() noexcept {
    while (!enemies_.empty()) {
        removeAt(enemies_.size() - 1);
    }
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

bool EnemySystem::setDestination(EnemyId id, std::optional<glm::dvec2> destination) {
    for (auto& enemy : enemies_) {
        if (enemy.id == id) {
            EnemyAi::setDestination(enemy, destination);
            return true;
        }
    }
    return false;
}

const Enemy* EnemySystem::find(EnemyId id) const noexcept {
    const auto found = std::find_if(enemies_.begin(), enemies_.end(), [id](const Enemy& enemy) {
        return enemy.id == id;
    });
    return found == enemies_.end() ? nullptr : &*found;
}

} // namespace grimlock::game
