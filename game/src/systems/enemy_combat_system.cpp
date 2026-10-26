#include "systems/enemy_combat_system.hpp"

#include "systems/enemy_ai.hpp"

#include <algorithm>
#include <cmath>

namespace grimlock::game {
namespace {
double distance(glm::dvec3 a, glm::dvec3 b) {
    return glm::length(a - b);
}
} // namespace

int EnemyCombatSystem::priority(const EnemyCombatDefinition& definition, DefensiveTargetKind kind) {
    switch (kind) {
    case DefensiveTargetKind::hero:
        return definition.heroPriority;
    case DefensiveTargetKind::tower:
        return definition.towerPriority;
    case DefensiveTargetKind::objective:
        return definition.objectivePriority;
    default:
        return 0;
    }
}

const EnemyCombatSystem::Target* EnemyCombatSystem::find(DefensiveTarget reference) const {
    const auto found = std::find_if(targets_.begin(), targets_.end(), [reference](const Target& target) {
        return target.reference == reference;
    });
    return found == targets_.end() ? nullptr : &*found;
}

void EnemyCombatSystem::release(EnemySystem& enemies, Enemy& enemy, bool rejected) {
    auto& combat = enemy.combat;
    if (combat.swinging) {
        enemies.attackAnimation(enemy, false);
    }
    if (rejected) {
        combat.rejected = combat.target;
        combat.rejectedOrigin = combat.origin;
        combat.pursuitExhausted = false;
        combat.rejectDelay = 2;
    }
    combat.target = {};
    combat.swinging = false;
    combat.impactRemaining = -1;
    combat.searchDelay = 0.4;
    EnemyAi::setDestination(enemy, combat.objective);
    enemy.destinationHeight = combat.objectiveHeight;
}

void EnemyCombatSystem::prepare(EnemySystem& enemies,
                                const TowerSystem& towers,
                                HeroSystem* heroes,
                                glm::dvec2 objective,
                                double delta,
                                const CombatGeometry* geometry) {
    geometry_ = geometry;
    targets_.clear();
    for (const auto& tower : towers.towers()) {
        targets_.push_back({{DefensiveTargetKind::tower, tower.object}, meleeBody(tower)});
    }
    if (heroes) {
        for (const auto& hero : heroes->heroes()) {
            if (hero.health > 0) {
                targets_.push_back(
                    {{DefensiveTargetKind::hero, hero.id}, {hero.position, heroes->definition().radius}});
            }
        }
    }
    for (auto& enemy : enemies.enemies_) {
        if (!enemy.active || !enemy.alive || !enemy.navigation) {
            continue;
        }
        auto& state = enemy.combat;
        const auto& stats = enemies.definition(enemy.type).combat;
        if (!state.managed) {
            state.managed = true;
            state.objective = enemy.destination ? enemy.destination : std::optional{objective};
            state.objectiveHeight = enemy.destinationHeight.value_or(enemy.position.y);
        }
        state.searchDelay = std::max(0.0, state.searchDelay - delta);
        state.pathDelay = std::max(0.0, state.pathDelay - delta);
        state.rejectDelay = std::max(0.0, state.rejectDelay - delta);
        state.threatRemaining = std::max(0.0, state.threatRemaining - delta);
        const auto& mesh = *enemy.navigation;
        // The objective uses the authored arrival damage/removal rule after its melee windup.
        Target goal{{DefensiveTargetKind::objective, 0},
                    {{state.objective->x, state.objectiveHeight, state.objective->y}, 0}};
        const auto relevant = [&](const Target& candidate) {
            if (!stats.objectiveFirst || candidate.reference.kind == DefensiveTargetKind::objective) {
                return true;
            }
            const auto offset = candidate.body.position - enemy.position;
            const auto advance = goal.body.position - enemy.position;
            const double gap = meleeGap(enemy.position, enemy.radius, candidate.body);
            const bool obstructing = gap <= stats.obstructionRange && glm::dot(offset, advance) >= 0;
            bool engaging = state.threatRemaining > 0 && state.attacker == candidate.reference;
            if (heroes && candidate.reference.kind == DefensiveTargetKind::hero) {
                const auto* hero = heroes->find(candidate.reference.id);
                engaging = engaging || (hero && hero->combat.target == enemy.id);
            }
            return obstructing || (engaging && gap <= stats.aggressionRange);
        };
        const Target* target = state.target.kind == DefensiveTargetKind::objective ? &goal : find(state.target);
        const bool pursuitExceeded = target && (distance(state.origin, target->body.position) > stats.pursuitRange ||
                                                distance(state.origin, enemy.position) > stats.pursuitRange);
        if (state.target.kind != DefensiveTargetKind::none && (!target || !relevant(*target) || pursuitExceeded)) {
            release(enemies, enemy, true);
            state.pursuitExhausted = pursuitExceeded;
            target = nullptr;
        }
        if (state.swinging) {
            continue;
        }
        if (!target && state.searchDelay <= 0) {
            candidates_.clear();
            const auto consider = [&](const Target& candidate, std::size_t index) {
                const int rank = priority(stats, candidate.reference.kind);
                const double gap = meleeGap(enemy.position, enemy.radius, candidate.body);
                if (rank > 0 && gap <= stats.aggressionRange && relevant(candidate) &&
                    !(state.rejected == candidate.reference &&
                      (state.rejectDelay > 0 ||
                       (state.pursuitExhausted &&
                        distance(state.rejectedOrigin, candidate.body.position) > stats.pursuitRange)))) {
                    candidates_.emplace_back(-static_cast<double>(rank), index);
                }
            };
            for (std::size_t index = 0; index < targets_.size(); ++index) {
                consider(targets_[index], index);
            }
            consider(goal, targets_.size());
            if (stats.objectivePriority == 0 &&
                meleeContact(mesh, enemy.position, enemy.radius, goal.body, stats.range)) {
                candidates_.emplace_back(0, targets_.size());
            }
            std::sort(candidates_.begin(), candidates_.end(), [&](const auto& a, const auto& b) {
                if (a.first != b.first) {
                    return a.first < b.first;
                }
                const auto& first = a.second == targets_.size() ? goal : targets_[a.second];
                const auto& second = b.second == targets_.size() ? goal : targets_[b.second];
                return meleeGap(enemy.position, enemy.radius, first.body) <
                       meleeGap(enemy.position, enemy.radius, second.body);
            });
            state.searchDelay = 0.4;
            const auto attempts = std::min<std::size_t>(4, candidates_.size());
            for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
                const auto& candidate = candidates_[(state.candidateOffset + attempt) % candidates_.size()];
                const auto& option = candidate.second == targets_.size() ? goal : targets_[candidate.second];
                const bool contact =
                    meleeContact(mesh, enemy.position, enemy.radius, option.body, stats.range) &&
                    clearStrike(enemy,
                                option.body,
                                option.reference.kind == DefensiveTargetKind::tower ? option.reference.id : 0);
                if (contact ||
                    approachMelee(mesh,
                                  enemy.position,
                                  enemy.radius,
                                  option.body,
                                  stats.range,
                                  approachPath_,
                                  enemy.id,
                                  {},
                                  0,
                                  geometry_,
                                  enemy.height,
                                  option.reference.kind == DefensiveTargetKind::tower ? option.reference.id : 0)) {
                    state.target = option.reference;
                    state.candidateOffset = 0;
                    state.origin = enemy.position;
                    state.pathDelay = 0;
                    target = &option;
                    if (!contact) {
                        std::swap(enemy.path, approachPath_);
                        enemy.destination = glm::dvec2{enemy.path.destination.x, enemy.path.destination.z};
                        enemy.destinationHeight = enemy.path.destination.y;
                        enemy.requestedDestination = enemy.destination;
                        enemy.destinationAssigned = true;
                        enemy.retryPath = false;
                        enemy.state = EnemyState::moving;
                        state.pathTarget = option.body.position;
                        state.pathRevision = mesh.revision();
                        state.pathDelay = 0.5;
                    }
                    break;
                }
            }
            if (!target && !candidates_.empty()) {
                state.candidateOffset = (state.candidateOffset + attempts) % candidates_.size();
            }
        }
        if (!target) {
            continue;
        }
        if (meleeContact(mesh, enemy.position, enemy.radius, target->body, stats.range) &&
            clearStrike(enemy,
                        target->body,
                        target->reference.kind == DefensiveTargetKind::tower ? target->reference.id : 0)) {
            EnemyAi::setDestination(enemy, {});
            enemy.yaw =
                std::atan2(target->body.position.x - enemy.position.x, target->body.position.z - enemy.position.z);
            if (state.cooldown <= 1e-10) {
                state.swinging = true;
                state.impactRemaining = stats.impactTime;
                state.cooldown = stats.interval;
                enemies.attackAnimation(enemy, true);
            }
            continue;
        }
        const bool changed =
            distance(state.pathTarget, target->body.position) > 0.3 || state.pathRevision != mesh.revision();
        if (state.pathDelay > 0 || (!changed && enemy.state == EnemyState::moving)) {
            continue;
        }
        state.pathDelay = 0.5;
        if (!approachMelee(mesh,
                           enemy.position,
                           enemy.radius,
                           target->body,
                           stats.range,
                           enemy.path,
                           enemy.id,
                           {},
                           0,
                           geometry_,
                           enemy.height,
                           target->reference.kind == DefensiveTargetKind::tower ? target->reference.id : 0)) {
            release(enemies, enemy, true);
            continue;
        }
        state.pathTarget = target->body.position;
        state.pathRevision = mesh.revision();
        enemy.destination = glm::dvec2{enemy.path.destination.x, enemy.path.destination.z};
        enemy.destinationHeight = enemy.path.destination.y;
        enemy.requestedDestination = enemy.destination;
        enemy.destinationAssigned = true;
        enemy.retryPath = false;
        enemy.state = EnemyState::moving;
        enemy.stuckTime = 0;
    }
}

void EnemyCombatSystem::resolve(Engine& engine,
                                EnemySystem& enemies,
                                TowerSystem& towers,
                                HeroSystem* heroes,
                                double delta) {
    arrivals_.clear();
    for (auto& enemy : enemies.enemies_) {
        auto& state = enemy.combat;
        state.cooldown = std::max(0.0, state.cooldown - delta);
        if (!enemy.alive || !enemy.active || !state.swinging) {
            continue;
        }
        if (state.impactRemaining >= 0) {
            state.impactRemaining -= delta;
            if (state.impactRemaining <= 1e-10) {
                const auto& stats = enemies.definition(enemy.type).combat;
                MeleeBody body;
                bool valid = false;
                if (state.target.kind == DefensiveTargetKind::hero && heroes) {
                    const auto* hero = heroes->find(state.target.id);
                    valid = hero && hero->health > 0;
                    if (valid) {
                        body = {hero->position, heroes->definition().radius};
                    }
                } else if (state.target.kind == DefensiveTargetKind::tower) {
                    const auto list = towers.towers();
                    const auto found = std::find_if(list.begin(), list.end(), [&](const Tower& tower) {
                        return tower.object == state.target.id;
                    });
                    valid = found != list.end();
                    if (valid) {
                        body = meleeBody(*found);
                    }
                } else if (state.target.kind == DefensiveTargetKind::objective && state.objective) {
                    body = {{state.objective->x, state.objectiveHeight, state.objective->y}, 0};
                    valid = true;
                }
                if (valid && meleeContact(*enemy.navigation, enemy.position, enemy.radius, body, stats.range) &&
                    clearStrike(enemy, body, state.target.kind == DefensiveTargetKind::tower ? state.target.id : 0)) {
                    switch (state.target.kind) {
                    case DefensiveTargetKind::hero:
                        (void)heroes->applyDamage(state.target.id, stats.damage * stats.heroDamageModifier);
                        break;
                    case DefensiveTargetKind::tower:
                        (void)towers.applyDamage(engine, state.target.id, stats.damage * stats.towerDamageModifier);
                        break;
                    case DefensiveTargetKind::objective:
                        arrivals_.push_back(enemy.id);
                        break;
                    default:
                        break;
                    }
                }
                state.impactRemaining = -1;
            }
        }
        if (state.cooldown <= 1e-10) {
            state.swinging = false;
            enemies.attackAnimation(enemy, false);
        }
    }
    for (const auto id : arrivals_) {
        if (enemies.remove(id)) {
            ++enemies.metrics_.arrived;
        }
    }
}

void EnemyCombatSystem::stop(EnemySystem& enemies) {
    geometry_ = nullptr;
    for (auto& enemy : enemies.enemies_) {
        release(enemies, enemy, false);
        enemy.combat = {};
    }
    targets_.clear();
}

bool EnemyCombatSystem::clearStrike(const Enemy& enemy, const MeleeBody& body, ObjectId target) const {
    return !geometry_ || geometry_->visible(combatAim(enemy), body.position + glm::dvec3{0, 0.25, 0}, 0, target);
}

} // namespace grimlock::game
