#include "systems/hero_combat_system.hpp"

#include "systems/melee_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <grimlock/ray.hpp>
#include <stdexcept>

namespace grimlock::game {
namespace {
double distance(glm::dvec3 first, glm::dvec3 second) {
    return glm::length(first - second);
}
} // namespace

void HeroCombatSystem::stop(Hero& hero) {
    hero.destination.reset();
    hero.path.valid = false;
    hero.speed = hero.stuckTime = 0;
    hero.state = HeroState::idle;
}

void HeroCombatSystem::abandon(HeroSystem& heroes, Hero& hero) {
    heroes.finishAttack(hero);
    hero.combat.target.reset();
    hero.combat.explicitTarget = false;
    hero.combat.retryDelay = 0.5;
    stop(hero);
}

bool HeroCombatSystem::inRange(const HeroSystem& heroes, const Hero& hero, const Enemy& enemy, double range) {
    return enemy.alive && enemy.active && enemy.health > 0 &&
           heroes.combatGeometry().visible(hero.position + glm::dvec3{0, heroes.definition_.height * 0.5, 0},
                                           combatAim(enemy)) &&
           meleeContact(heroes.navigation_,
                        hero.position,
                        heroes.definition_.radius,
                        {enemy.position, enemy.radius},
                        range);
}

bool HeroCombatSystem::attack(HeroSystem& heroes, const EnemySystem& enemies, HeroId id, EnemyId target) {
    auto* hero = heroes.findMutable(id);
    const auto* enemy = enemies.find(target);
    if (!hero || !hero->controllable || hero->health <= 0 || !enemy || !enemy->alive || !enemy->active ||
        distance(hero->position, enemy->position) > heroes.definition_.combat.pursuitRange) {
        return false;
    }
    heroes.finishAttack(*hero);
    stop(*hero);
    hero->combat.target = target;
    hero->combat.excludedTarget.reset();
    hero->combat.explicitTarget = true;
    hero->combat.playerMoving = false;
    hero->combat.pursuitOrigin = hero->position;
    hero->combat.retryDelay = 0;
    hero->combat.pathDelay = 0;
    hero->combat.returning = false;
    return true;
}

bool HeroCombatSystem::activate(HeroSystem& heroes, HeroId id, HeroAbility ability, bool battle) {
    auto* hero = heroes.findMutable(id);
    if (!battle || ability != HeroAbility::cleave || !hero || !hero->controllable || hero->health <= 0 ||
        hero->combat.cleaveCooldown > 1e-10 || (hero->combat.swinging && hero->combat.abilitySwing)) {
        return false;
    }
    // A deliberate ability command takes priority over the automatic basic attack.
    heroes.finishAttack(*hero);
    stop(*hero);
    auto& state = hero->combat;
    state.playerMoving = false;
    state.returning = false;
    state.swinging = state.abilitySwing = true;
    state.impactRemaining = heroes.definition_.combat.impactTime;
    state.cooldown = heroes.definition_.combat.interval;
    state.cleaveCooldown = heroes.definition_.combat.cleave.cooldown;
    heroes.playAttack(*hero);
    return true;
}

void HeroCombatSystem::impact(HeroSystem& heroes, EnemySystem& enemies, Hero& hero) {
    const auto& stats = heroes.definition_.combat;
    if (!hero.combat.abilitySwing) {
        const auto* enemy = hero.combat.target ? enemies.find(*hero.combat.target) : nullptr;
        if (enemy && inRange(heroes, hero, *enemy, stats.range)) {
            (void)enemies.applyDamage(enemy->id, stats.damage);
        }
        return;
    }
    victims_.clear();
    const glm::dvec2 facing{std::sin(hero.yaw), std::cos(hero.yaw)};
    const double minimumDot = std::cos(glm::radians(stats.cleave.angle * 0.5));
    for (const auto& enemy : enemies.enemies()) {
        const glm::dvec2 offset{enemy.position.x - hero.position.x, enemy.position.z - hero.position.z};
        const double length = glm::length(offset);
        if (inRange(heroes, hero, enemy, stats.cleave.radius) &&
            (length < 1e-8 || glm::dot(facing, offset / length) >= minimumDot)) {
            victims_.push_back(enemy.id);
        }
    }
    // Lethal damage compacts enemy storage; never keep references across impacts.
    for (const auto id : victims_) {
        (void)enemies.applyDamage(id, stats.cleave.damage);
    }
    hero.combat.feedbackRemaining = 0.3;
}

void HeroCombatSystem::step(HeroSystem& heroes, EnemySystem& enemies, Hero& hero, double delta) {
    auto& state = hero.combat;
    const auto& stats = heroes.definition_.combat;
    state.cooldown = std::max(0.0, state.cooldown - delta);
    state.cleaveCooldown = std::max(0.0, state.cleaveCooldown - delta);
    state.feedbackRemaining = std::max(0.0, state.feedbackRemaining - delta);
    state.retryDelay = std::max(0.0, state.retryDelay - delta);
    state.pathDelay = std::max(0.0, state.pathDelay - delta);
    state.unreachableRetry = std::max(0.0, state.unreachableRetry - delta);
    if (!hero.controllable || hero.health <= 0) {
        abandon(heroes, hero);
        return;
    }
    if (state.playerMoving) {
        if (hero.state != HeroState::moving) {
            state.playerMoving = false;
        }
        return;
    }
    if (state.swinging) {
        if (state.impactRemaining >= 0) {
            state.impactRemaining -= delta;
            if (state.impactRemaining <= 1e-10) {
                impact(heroes, enemies, hero);
                state.impactRemaining = -1;
            }
        }
        if (state.cooldown <= 1e-10) {
            heroes.finishAttack(hero);
        } else {
            return;
        }
    }
    const auto* enemy = state.target ? enemies.find(*state.target) : nullptr;
    if (enemy && (distance(state.pursuitOrigin, enemy->position) > stats.pursuitRange ||
                  distance(state.pursuitOrigin, hero.position) > stats.pursuitRange)) {
        // Reacquiring the same fleeing target must not extend pursuit indefinitely.
        state.excludedTarget = enemy->id;
        state.excludedOrigin = state.pursuitOrigin;
        abandon(heroes, hero);
        return;
    }
    if (state.target && (!enemy || !enemy->alive || !enemy->active)) {
        abandon(heroes, hero);
        return;
    }
    if (!enemy && state.retryDelay <= 0) {
        acquire(heroes, enemies, hero);
        enemy = state.target ? enemies.find(*state.target) : nullptr;
    }
    if (!enemy) {
        returnToDefense(heroes, hero);
        return;
    }
    if (inRange(heroes, hero, *enemy, stats.range)) {
        stop(hero);
        state.returning = false;
        hero.yaw = std::atan2(enemy->position.x - hero.position.x, enemy->position.z - hero.position.z);
        if (state.cooldown <= 1e-10) {
            state.swinging = true;
            state.abilitySwing = false;
            state.cooldown = stats.interval;
            state.impactRemaining = stats.impactTime;
            heroes.playAttack(hero);
        }
        return;
    }
    const bool changed =
        distance(state.pathTarget, enemy->position) > 0.35 || hero.path.revision != heroes.navigation_.revision();
    if (state.pathDelay > 0 || (!changed && hero.state == HeroState::moving)) {
        return;
    }
    state.pathDelay = 0.4;
    if (!approachMelee(heroes.navigation_,
                       hero.position,
                       heroes.definition_.radius,
                       {enemy->position, enemy->radius},
                       stats.range,
                       hero.path,
                       hero.id,
                       enemies.enemies(),
                       enemy->id,
                       &heroes.combatGeometry(),
                       heroes.definition_.height)) {
        state.unreachableTarget = enemy->id;
        state.unreachableRetry = 2;
        state.failedRevision = heroes.navigation_.revision();
        abandon(heroes, hero);
    } else {
        state.pathTarget = enemy->position;
        hero.destination = hero.path.destination;
        hero.state = HeroState::moving;
        hero.stuckTime = 0;
    }
}

void HeroCombatSystem::acquire(HeroSystem& heroes, const EnemySystem& enemies, Hero& hero) {
    auto& state = hero.combat;
    const auto& stats = heroes.definition_.combat;
    state.retryDelay = 0.4;
    candidates_.clear();
    for (const auto& candidate : enemies.enemies()) {
        if (!candidate.alive || !candidate.active ||
            distance(hero.defensivePosition, candidate.position) > stats.pursuitRange ||
            (state.unreachableTarget == candidate.id && state.unreachableRetry > 0 &&
             state.failedRevision == heroes.navigation_.revision())) {
            continue;
        }
        if (state.excludedTarget == candidate.id &&
            distance(state.excludedOrigin, candidate.position) > stats.acquisitionRange) {
            continue;
        }
        const double separation = distance(hero.position, candidate.position);
        if (separation <= stats.acquisitionRange) {
            candidates_.emplace_back(separation, candidate.id);
        }
    }
    std::sort(candidates_.begin(), candidates_.end());
    for (const auto& [separation, id] : candidates_) {
        const auto& enemy = *enemies.find(id);
        const bool contact = inRange(heroes, hero, enemy, stats.range);
        if (!contact && !approachMelee(heroes.navigation_,
                                       hero.position,
                                       heroes.definition_.radius,
                                       {enemy.position, enemy.radius},
                                       stats.range,
                                       approachPath_,
                                       hero.id,
                                       enemies.enemies(),
                                       enemy.id,
                                       &heroes.combatGeometry(),
                                       heroes.definition_.height)) {
            continue;
        }
        state.target = id;
        state.explicitTarget = state.returning = false;
        state.pursuitOrigin = hero.defensivePosition;
        state.pathTarget = enemy.position;
        state.pathDelay = 0.4;
        if (!contact) {
            std::swap(hero.path, approachPath_);
            hero.destination = hero.path.destination;
            hero.state = HeroState::moving;
        }
        return;
    }
}

void HeroCombatSystem::returnToDefense(HeroSystem& heroes, Hero& hero) {
    auto& state = hero.combat;
    if (distance(hero.position, hero.defensivePosition) <= heroes.definition_.stoppingDistance + 0.03) {
        state.returning = false;
        return;
    }
    if (state.pathDelay > 0 ||
        (state.returning && hero.state == HeroState::moving && hero.path.revision == heroes.navigation_.revision())) {
        return;
    }
    state.pathDelay = 0.5;
    state.returning = heroes.navigate(hero, hero.defensivePosition);
}

void HeroCombatSystem::update(HeroSystem& heroes, EnemySystem& enemies, double delta, bool battle) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Hero combat delta must be finite and nonnegative.");
    }
    if (!battle) {
        for (auto& hero : heroes.heroes_) {
            abandon(heroes, hero);
            hero.combat.playerMoving = false;
            hero.combat.retryDelay = 0;
            hero.combat.feedbackRemaining = 0;
        }
        heroes.update(delta, false);
        return;
    }
    do {
        const double time = std::min(delta, 1.0 / 120);
        for (auto& hero : heroes.heroes_) {
            step(heroes, enemies, hero, time);
        }
        heroes.update(time, true, enemies.enemies());
        delta -= time;
    } while (delta > 1e-10);
}

std::optional<EnemyId> HeroCombatSystem::pick(Engine& engine, const EnemySystem& enemies, const Ray& ray) {
    std::optional<EnemyId> selected;
    double nearest = ray.maximum;
    for (const auto& enemy : enemies.enemies()) {
        if (enemy.instance && enemy.alive && enemy.active) {
            if (const auto hit = engine.intersectInstance(ray, *enemy.instance); hit && *hit < nearest) {
                nearest = *hit;
                selected = enemy.id;
            }
        }
    }
    return selected;
}

} // namespace grimlock::game
