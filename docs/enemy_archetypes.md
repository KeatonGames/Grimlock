# Enemy archetypes

All enemies share the existing ECS EnemySystem, EnemyCombatSystem, Recast/Detour
navigation, health/damage APIs and renderer. Definitions live in
`game/assets/config/enemy_archetypes.json`; animal-based legacy definitions remain
available through `config/animals.json`.

| Type | Model | Health | Speed | Damage | Cooldown | Reach | Detection / pursuit |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| Goblin | character-orc.glb | 60 | 1.6 | 5 | 0.9 s | 0.35 | 2.6 / 4 |
| Siege | Deer | 300 | 0.95 | 14 | 1.2 s | 0.40 | 4.5 / 6 |
| Hunter | Dog | 80 | 2.2 | 9 | 0.7 s | 0.35 | 4 / 5.5 |

The Iron Stag boss and all other definitions remain available. Siege retains its
preference for towers and 2x tower damage; Hunter retains its preference for heroes
and 1.5x hero damage. Default priority-based targeting and optional objective-first
settings remain supported for those definitions and future use.

## Goblin asset and animations

Phase 45 uses `game/assets/enemies/orcs/character-orc.glb` as the Goblin, as explicitly
authorized. The file remains an Orc asset; no unrelated replacement was hidden and
no model data or materials were changed. It contains two skinned meshes sharing a
seven-joint hierarchy rooted at `root`, with legs, torso, arms and head, and 32
embedded clips. The original colors and embedded textures remain intact.

Configured clips are actual embedded names:

- `idle`: stationary loop.
- `walk`: low-speed movement loop.
- `sprint`: movement above 70% of configured speed.
- `attack-melee-right`: basic one-shot melee attack.
- `die`: one-shot death.

The asset has **no hit-reaction clip**. Hits use the existing brief red damage flash,
without interrupting a swing or inventing an animation name. Other embedded attacks
and actions are retained in the asset but are not used by Goblin combat.

Goblin's collision radius is 0.23, physical height is 0.9 and visual height is 0.9;
these are independent definition fields. The +Z model faces the existing movement
and attack yaw, and model bounds align its feet with the navigable ground. Walk and
sprint contain vertical root bobbing but no X/Z root displacement, so navigation
retains world movement authority. Locomotion clips change only with gait/state,
and the existing player switches clips without crossfading.

Each attack starts once and deals damage at 0.22 seconds into the approximately
0.417-second right-hand melee clip. Cooldown remains independently configurable.
Impacts recheck target lifetime, physical contact, elevation and visibility before
applying damage once. Goblins can damage heroes, towers and the objective; objective
hits preserve the level's existing arrival/removal and `damage_per_arrival` rules.

A killed Goblin immediately leaves ECS targeting, movement and navigation, while
its render instance plays `die`. Explicitly configured death clips retain their
instance until playback actually finishes, even when simulation time advances
faster than presentation. Normal level/system cleanup still releases all instances.

## First-detected targeting

Goblin sets `combat.targeting` to `first_detected`. It advances toward the defense
objective until the existing detector encounters a visible, hostile, reachable
hero or tower within range. Simultaneous detections use detector traversal order;
Goblin does not rank type, strength, health or distance. Once engaged, a valid
victim remains selected even when another defender appears or attacks the Goblin.

A dead, removed, unreachable or pursuit-exhausted victim is released. Goblin
resumes its objective route and can acquire another valid defender. Searches retain
the existing 0.4-second cadence and bounded path attempts; target approach retains
cached paths and the existing 0.5-second recalculation delay. Rejected targets keep
the existing retry/pursuit protection. No tactical behavior or new pathfinder is added.

## Definitions and compatibility

Archetypes can inherit an `animal` as before, or specify an asset-relative `model`
explicitly. Explicit models must exist and stay within the asset root; there is no
fallback to Chicken when Goblin's asset is missing. Health, dimensions, movement,
clip names, damage, detection, pursuit, impact time and cooldown remain data-driven.

Goblin has `aliases: ["Grunt"]`. Old handcrafted wave JSON continues resolving to
the same catalog slot, while runtime wave definitions, the HUD and Editor selection
show **Goblin**. New Editor wave authoring writes the canonical name. This changes
neither the level JSON schema nor wave counts, timing, rewards or progression rules.
No duplicate Grunt enemy implementation or additional playable enemy type is added.

## Validation

`Goblin.Runtime` covers the imported meshes/rig, idle/walk/run/attack/death states,
damage flash, no per-frame attack restart, ignored tactical scores, visibility,
target retention, tower/hero damage, pursuit release, objective impact accounting,
full death playback, shared resources, 100-Goblin crowds, elevated ramp traversal
and legacy Grunt wave spawning. Existing definition and battle checks continue
covering Siege/Hunter behavior and catalog compatibility.

Build and test logs are under `out/phase45_*.log`. No extended manual visual
playthrough has been performed; the lack of an authored hit-reaction clip remains
the asset's animation limitation.

Phase 45 verification: Debug Game and Editor builds succeed. Across the complete
45-check run and focused reruns, 44 checks pass. Enemies.Runtime and Goblin.Runtime
pass on the final build, alongside the full run's battle, hero, terrain, stress,
asset-library, Game HUD and Editor checks. Only the pre-existing Cleave-dependent
Level.Playable strategy remains failing. Modified C++ passes clang-format and Git
whitespace checks.
