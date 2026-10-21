#include "systems/enemy_system.hpp"

#include "navigation/grid_clearance.hpp"

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

bool validRadius(double radius) {
    return std::isfinite(radius) && radius > 0 && radius <= 1000;
}

std::optional<std::size_t> movementClip(const std::vector<std::string>& names, const std::string& requested) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == requested) {
            return index;
        }
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto name = names[index];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name.find("walk") != std::string::npos || name.find("run") != std::string::npos) {
            return index;
        }
    }
    return std::nullopt; // A static model or an unrelated clip stays at rest.
}

} // namespace

EnemySystem::EnemySystem(Engine& engine, EnemyDefinition definition)
    : engine_(engine), definition_(std::move(definition)) {
    if (!validSpeed(definition_.speed) || !validRadius(definition_.radius) ||
        !std::isfinite(definition_.acceleration) || definition_.acceleration <= 0 ||
        !std::isfinite(definition_.deceleration) || definition_.deceleration <= 0 ||
        !std::isfinite(definition_.visualHeight) || definition_.visualHeight <= 0 ||
        !std::isfinite(definition_.maximumHealth) || definition_.maximumHealth <= 0 ||
        !std::isfinite(definition_.animationSpeed) || definition_.animationSpeed < 0 ||
        !std::isfinite(definition_.forwardYaw)) {
        throw std::invalid_argument("Invalid enemy movement, health or animation configuration.");
    }
}

EnemySystem::~EnemySystem() {
    clear();
}

void EnemySystem::loadModel() {
    if (model_) {
        return;
    }
    const auto model = engine_.loadModel(engine_.assetRoot() / definition_.model);
    auto info = engine_.modelInfo(model);
    if (!std::isfinite(glm::length(info.maximum - info.minimum)) || info.maximum.y <= info.minimum.y) {
        throw std::runtime_error("Enemy model needs finite, nonempty framing bounds.");
    }
    movementClip_ = movementClip(info.animationNames, definition_.movementAnimation);
    modelInfo_ = std::move(info);
    model_ = model;
}

std::size_t EnemySystem::spawn(const Grid* walls, SpawnArea area, std::size_t count) {
    if (count > maximumEnemies || !std::isfinite(area.minimum.x) || !std::isfinite(area.minimum.y) ||
        !std::isfinite(area.maximum.x) || !std::isfinite(area.maximum.y) || area.minimum.x > area.maximum.x ||
        area.minimum.y > area.maximum.y) {
        throw std::invalid_argument("Invalid chicken count or spawn rectangle.");
    }
    const double height = walls ? walls->configuration().origin.y : 0;
    const double padding = definition_.radius * 0.15;
    const double spacing = definition_.radius * 2 + padding;
    if (area.minimum.x + spacing <= area.minimum.x || area.minimum.y + spacing <= area.minimum.y) {
        throw std::invalid_argument("Spawn spacing is too small for this world origin.");
    }
    const auto before = enemies_.size();
    const auto limit = std::min(maximumEnemies, before + count);
    enemies_.reserve(limit);
    spawnNeighbors_.build(enemies_, spacing, glm::dvec3{0}, limit);

    for (double z = area.minimum.y; z <= area.maximum.y && enemies_.size() < limit; z += spacing) {
        for (double x = area.minimum.x; x <= area.maximum.x && enemies_.size() < limit; x += spacing) {
            const glm::dvec3 position{x, height, z};
            if (walls && !segmentWalkable(*walls, position, position, definition_.radius)) {
                continue;
            }
            bool occupied = false;
            spawnNeighbors_.visit(position, spacing, [&](std::size_t index) {
                const auto offset = enemies_[index].position - position;
                const double distance = enemies_[index].radius + definition_.radius + padding;
                occupied = occupied || offset.x * offset.x + offset.z * offset.z < distance * distance;
            });
            if (occupied) {
                continue;
            }
            loadModel();
            Enemy enemy;
            enemy.id = nextId_++;
            enemy.position = position;
            enemy.speed = definition_.speed;
            enemy.radius = definition_.radius;
            enemy.acceleration = definition_.acceleration;
            enemy.deceleration = definition_.deceleration;
            enemy.health = definition_.maximumHealth;
            enemy.instance = engine_.createInstance(*model_, true);
            enemies_.push_back(enemy);
            spawnNeighbors_.insert(enemies_.size() - 1, position);
            auto& playback = engine_.animation(*enemy.instance);
            playback.setSpeed(definition_.animationSpeed);
            if (movementClip_) {
                playback.play(*movementClip_);
                const auto& clip = playback.model().animations[*movementClip_];
                playback.seek(clip.start + (clip.end - clip.start) * static_cast<float>((enemy.id * 37) % 100) / 100);
                playback.pause();
            }
            updateModel(enemies_.back());
        }
    }
    return enemies_.size() - before;
}

void EnemySystem::updateModel(Enemy& enemy) {
    const auto center = (modelInfo_.minimum + modelInfo_.maximum) * 0.5F;
    const float scale = definition_.visualHeight / (modelInfo_.maximum.y - modelInfo_.minimum.y);
    const auto matrix =
        glm::translate(glm::mat4{1}, glm::vec3{enemy.position}) *
        glm::mat4_cast(glm::angleAxis(static_cast<float>(enemy.yaw + definition_.forwardYaw), glm::vec3{0, 1, 0})) *
        glm::scale(glm::mat4{1}, glm::vec3{scale}) *
        glm::translate(glm::mat4{1}, glm::vec3{-center.x, -modelInfo_.minimum.y, -center.z});
    engine_.setTransform(*enemy.instance, matrix);
    const bool moving = enemy.movement == MovementStatus::moving;
    if (movementClip_ && moving != enemy.animationMoving) {
        auto& playback = engine_.animation(*enemy.instance);
        if (moving) {
            playback.resume();
        } else {
            playback.pause();
        }
    }
    enemy.animationMoving = moving;
}

void EnemySystem::update(const Grid* walls, std::optional<glm::dvec2> destination, double delta) {
    const auto start = std::chrono::steady_clock::now();
    movement_.update(walls, destination, enemies_, delta, definition_.despawnOnArrival);
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

void EnemySystem::setSpeed(double speed) {
    if (!validSpeed(speed)) {
        throw std::invalid_argument("Enemy speed must be between 0 and 20.");
    }
    definition_.speed = speed;
    for (auto& enemy : enemies_) {
        enemy.speed = speed;
    }
}

} // namespace grimlock::game
