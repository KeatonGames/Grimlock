# Hero types

Choose **Knight**, **Ranger**, or **Mage** beside **Deploy Hero** during Preparation,
then click clear navigable ground. Escape cancels placement. Deploying a living
hero of an existing type repositions it without changing its entity identity or
restoring health. Dead heroes remain unavailable until level restart.

Select a hero through its roster button or in the world. Right-click enemies to
attack, right-click ground to move, and right-drag to pan the camera. Manual orders
override autonomous targeting temporarily. Each hero resumes defense after orders
finish. All three share one ECS HeroSystem and can fight simultaneously.

| Hero | Health | Damage | Reach | Attack interval |
| --- | --- | --- | --- | --- |
| Knight | 100 | 25, melee single target | 1.2 | 0.8 s |
| Ranger | 90 | 18, arrow single target | 5.0 | 3.0 s |
| Mage | 100 | 20, projectile with 1.5 splash radius | 4.5 | 1.1 s |

All attacks are autonomous basic attacks. None of these heroes exposes an active
ability. Range follows the existing surface-gap convention, including physical
actor radii. Splash uses distance between enemy aim points at impact and checks
visibility to each victim; geometry absorbs projectiles without splash.

## Configuration and compatibility

`game/assets/config/hero.json` defines Knight. Its legacy `assassin` type key and
first catalog slot remain unchanged. `config/heroes.json` defines Ranger and Mage
in slots two and three. Mage replaces the previous placeholder Warrior; authored
level spawn positions, free deployment limits, selection and scene roles remain
compatible. Existing custom `area` attack definitions still parse with their
original instant-area behavior; Mage uses the new `ranged_area` basic attack.
There is no serialized class-specific entity implementation to migrate.

Definitions configure health, movement, collision radius/height, visual height,
forward yaw, clip names and combat statistics. Visual normalization does not affect
Recast agent dimensions. Attack settings include damage, range, interval, impact
time, acquisition/pursuit ranges, projectile speed/radius, model/scale/tint, socket
and local muzzle offset. `splash_radius` applies to `ranged_area` projectiles.
`special_abilities` is false for all three playable definitions.

## Shared asset preparation

Hero base models are retained at `game/assets/heroes/<type>/base.glb`. Shared
Rig_Medium and Rig_Large GLBs are under `heroes/other/animations/`; weapon props
are organized by purpose. Rebuild the animated heroes from these embedded GLBs:

```powershell
powershell -ExecutionPolicy Bypass -File tools/prepare_knight.ps1
powershell -ExecutionPolicy Bypass -File tools/prepare_knight.ps1 -Character Ranger
powershell -ExecutionPolicy Bypass -File tools/prepare_knight.ps1 -Character Mage
```

The original Knight command remains supported. Each model retains its skin joints,
inverse bind matrices, mesh parts, colors and embedded material atlas. The shared
script checks animation node names, parent hierarchy and local bind transforms
before remapping channels. Root animation channels are removed because navigation
owns world movement. Useful original GLB models and animations remain available for future work.

| Character | Attachment | Socket | Running | Basic attack |
| --- | --- | --- | --- | --- |
| Knight | sword_1handed | handslot.r | Running_A | Melee_1H_Attack_Slice_Horizontal |
| Ranger | bow_withString | handslot.l | Running_HoldingBow | Ranged_Bow_Draw / Ranged_Bow_Release |
| Mage | staff | handslot.r | Running_A | Ranged_Magic_Shoot |

All models include `Idle_A`, `Walking_A`, `Running_A`, `Hit_A` and `Death_A` from
Rig_Medium General and MovementBasic. Ranger additionally imports Running_HoldingBow
from MovementAdvanced and all four requested bow clips from CombatRanged. Its normal
idle is `Ranged_Bow_Idle`. Mage imports only the basic magic shooting clip, without
spellcasting. Weapon geometry uses the corresponding character material and authored
grip origin/scale. The bow's existing draw morph animates during draw and release.

Ranger arrows load `towers/projectiles/kaykit/arrow_bow.glb`, with geometry and
texture embedded. The native +Z arrow axis follows projectile velocity; visual
scale remains independent of the swept collision radius. Mage uses a small blue
instance of the existing cube projectile, without a particle system.

## Combat and animation timing

Knight retains its 0.25-second impact and 0.8-second cooldown. Each basic swing
starts once; the next attack can interrupt the source clip's recovery.

Ranger aims for 0.2 seconds, plays the full 1.333-second draw, then spawns one arrow
and starts the 1.333-second release at 1.533 seconds. The configurable three-second
interval accommodates those source clips at their original playback speed. Mage
spawns one projectile 0.4 seconds into its shooting clip. Projectile origins sample
the hand socket from the attack pose using the combat clock, independent of render
frame rate. Targets are checked again at release; damage occurs on swept impact.

Mage impacts collect stable enemy IDs before damage, preventing lethal storage
compaction from skipping victims or applying damage twice. Missing or recycled
targets remove the projectile safely. Existing terrain sweeps, attacker attribution,
manual overrides, pathfinding, retargeting and encounter cleanup remain shared.

Idle and movement clips loop; walk is selected below 55% of running speed. Attack,
hit and death are one-shots. Repeated attack orders do not restart windup. Hits do
not interrupt an active attack or restart a current reaction. Death cancels movement,
targets and combat and retains the corpse until normal hero/level cleanup. The
existing animation player switches clips without crossfading.

## Validation

Debug Game and Editor builds succeed. The full suite passes 42 of 43 checks; the
only failure is the existing Level.Playable regression described below. Final focused
hero, terrain, combat, Game HUD and Editor runtime checks pass. Hero checks cover all three models, animated attachments,
bow morphs and attack phases, hit/death lifecycle, independent commands and damage,
lethal splash compaction, stale projectile generations and 1/5/60 FPS splash timing.
No extended manual visual playthrough was performed.

The Phase 42 `Level.Playable` small-tower strategy regression remains: that old
strategy relied on Assassin Cleave. The mixed-defense strategy still wins. Its
original assertion is retained; this phase does not rebalance Knight or the levels.
Current build and test logs are under `out/phase43_*.log`.
