# Hero types

During Preparation, choose **Assassin**, **Ranger**, or **Warrior** beside **Deploy Hero**,
then click valid navigable ground. Cancel with Escape. Deploying an existing living
hero of that type repositions it without replacing its identity or restoring health.
A type not yet deployed can be placed alongside the others. This uses the existing
free hero deployment rules; gold, tower costs, and battle placement restrictions
are unchanged. Dead heroes cannot be restored through deployment.

Select each hero through its roster button or in the world. Right-click enemies to
attack, right-click ground to move, and right-drag to move the camera. Manual moves
retain priority; autonomous defense resumes afterward. Names, health, damage,
range, and attack rate come from the selected hero's definition.

## Configuration

- `game/assets/config/hero.json` retains the original Assassin definition and statistics.
- `game/assets/config/heroes.json` contains the additional reusable definitions.
  Each entry has a unique `id`, display `name`, and asset-relative `model` path.
- `combat.attack` chooses `melee`, `ranged`, or `area`.
- `combat.damage`, `range`, `attack_interval`, and `impact_time` control the basic attack.
  Range uses the existing surface-gap convention, including actor physical radii.
  An area attack covers all valid bodies within that reach, in every direction.
- `combat.acquisition_range` and `pursuit_range` control autonomous targeting/chasing.
- Ranged entries additionally use `projectile_speed` and `projectile_radius`.
- `maximum_health`, `speed`, `radius`, and `height` control health and physical movement.
  `visual_height` and `forward_yaw` independently control model presentation.
- Existing idle/walk/run/attack animation names can be configured per definition.
  Missing clips fall back to idle or no animation; damage timing remains reliable.

| Hero | Health | Damage | Reach | Attack interval |
| --- | --- | --- | --- | --- |
| Assassin | 100 | 25, single target | 1.2 | 0.8 s |
| Ranger | 90 | 18, projectile | 5.0 | 0.9 s |
| Warrior | 160 | 20, area | 1.4 | 1.1 s |

Both new types currently use `models/assassin.glb` as a placeholder because no other
hero character model is available. Change their independent `model` fields to use
other existing assets. Model/GPU resources remain shared through Engine's cache.
Assassin retains its existing Cleave ability; Ranger and Warrior have no active ability.
Existing level hero spawn positions continue spawning Assassin by default. Runtime
hero deployment does not edit the original scene or level JSON.

## Shared implementation

One ECS `HeroSystem` owns all hero records. A record stores its definition index;
position, health, target, commands, path, and cooldown stay independent per entity.
Per-type model/animation resources and radius/height-aware Recast profiles are
shared across that type's instances and refreshed through the existing geometry
revision checks. Enemy combat resolves each hero's own physical radius.

`HeroCombatSystem` retains the same acquisition, pursuit, return-to-defense,
manual-command, and attack-timing logic. Ranged impacts spawn through the existing
`ProjectileSystem`; projectiles sweep terrain and retain a generation-safe hero
attacker reference. Area impacts collect victim IDs before applying damage so
lethal packed-storage removal cannot skip or invalidate another victim. Shots are
cleared at encounter end and during hero/level cleanup.

`Hero.Runtime` covers mixed types, ranged hits/approach/terrain collision, lethal
area damage, independent targets, command overrides, enemy attacks, free typed
deployment, and restart cleanup. `Terrain.Runtime` drives each type up the existing
3D ramp. Original Assassin combat/ability and movement regressions remain enabled.

Validation: Game and Editor built successfully in Release and Debug. The complete
Debug suite passed 41/41 tests with Vulkan validation enabled. The initial Release
suite passed 40/41; its HUD input check exposed the moved deployment button. After
restoring that button's position and wrapping crowded rows, Game.Runtime and
Game.Ui both passed on rerun (2/2). Release hero and terrain checks also passed.
Modified C++ passed clang-format verification; local build/test logs are in
`out/archive/phase38/`. No extended manual playthrough was performed.
