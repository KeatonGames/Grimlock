# Heroes: movement, melee and Cleave

First Watch starts with an Assassin. During Preparation, **Deploy Hero** places or
repositions the selected hero (otherwise the first hero) on clear navigable ground.
Deployment is free and independent of tower build zones. Escape cancels placement.

During Battle:

- Left-click a hero to select it. Its mesh is highlighted and the Hero Info card
  shows health, attack statistics, current enemy target and Cleave cooldown.
- Short right-click ground to move. This cancels a pending attack and suppresses
  automatic targeting until the movement order finishes or fails.
- Short right-click an enemy to attack it. The hero approaches through the NavMesh
  and stops within melee range. A red ring marks the selected hero's target.
- Heroes automatically acquire the nearest reachable enemy in acquisition range,
  keep that target, and return to their defensive position when no target remains.
  Deployment and successful ground movement set that defensive position.
  Automatic pursuit is bounded from the defensive position; explicit attack orders
  are bounded from the position where the command was issued. A fleeing
  enemy outside that limit is ignored until it returns within acquisition range
  of that position, or the player issues another order.
- **Cancel Order** interrupts a manual order or swing and resumes automatic defense.
  The Hero Info card distinguishes manual movement/attacks, returning and automatic AI.
- Press **Cleave** in Hero Info for a delayed cone attack in the hero's current
  facing direction. A manual Cleave can interrupt an automatic basic swing so the
  ability is usable during sustained combat. It has an independent per-hero cooldown and a brief gold cone
  on impact. Cancelling the windup with another order does not refund cooldown.
- Right-drag pans, middle-drag orbits, and wheel zooms. The five-pixel total drag
  threshold distinguishes commands from camera gestures. Commands occur on release;
  UI gestures and unfocused input do not issue orders.

Tower/hero placement and tower selection retain their existing controls. Restart
restores authored hero positions and health, clearing combat, cooldowns and selection.
Enemies can damage and kill heroes. Damaged heroes show a world-space health bar
and a brief hit flash. Dead heroes stop moving, targeting and using abilities, play
an available death clip or retain a dim stationary pose, and cannot be redeployed.
Restart restores them; this phase adds no respawning.

## Configuration

`game/assets/config/hero.json` defines the shared model, name, movement parameters,
physical radius/headroom, visual height, health and Idle/Walk/Run clip names.
Physical dimensions are independent of visual scale. Its `combat` object contains:

```json
"combat": {
  "damage": 25,
  "range": 1.2,
  "attack_interval": 0.8,
  "impact_time": 0.25,
  "acquisition_range": 3.5,
  "pursuit_range": 6,
  "attack_animation": "attack",
  "cleave": { "damage": 45, "radius": 2, "angle": 100, "cooldown": 6 }
}
```

Distances are world units, angles are full cone degrees, and times are seconds.
`attack_interval` defines the swing duration/cadence. `impact_time` is the single
contact event after a swing starts; author it to match the configured animation's
contact frame. It must be between zero and the attack interval. Pursuit range
must be at least acquisition range. Invalid/nonfinite statistics are rejected.
Melee reach measures the gap between both physical bodies, including the hero radius.
Cleave additionally
requires its center to lie inside the facing cone. Both attacks require bounded
vertical separation and a clear NavMesh surface ray to prevent hits through obstacles.
Cooldowns use Battle simulation time. There are no percentage multipliers.

Attack clips play once and return to configured Idle/Movement playback. The supplied
`assassin.glb` has **no animation clips** and incomplete rest joint transforms. It
uses the existing undeformed bind-pose fallback and timed damage; the implementation
does not invent animation data or modify the asset. Animated assets use the same
playback path; exact names and common clip-name prefixes are supported.

Level JSON optionally defines foot positions in world X/Y/Z:

```json
"hero_spawns": [[4, 0, 2]]
```

Multiple initial heroes share model/GPU resources but have independent IDs,
movement, health, targets, attacks and ability cooldowns. Runtime objects and
combat state are never written into authored levels. Editor preserves hero spawns;
there is no new editor mode or party management UI.

## Ownership and verification

`entities/hero.*` separates immutable definitions from per-instance runtime state.
`HeroSystem` owns scene objects, radius-aware Recast navigation and animation
playback. `HeroCombatSystem` makes targeting/attack decisions and delegates all
damage, hit feedback, deaths and defeated counts to `EnemySystem::applyDamage`.
Enemy IDs remain stable across packed-storage removal; Cleave gathers IDs before
applying damage. Unreachable targets stop safely and retry at bounded intervals.

`melee_navigation.*` samples reachable perimeter positions on the attacker's own
Recast profile. It tests a clear stand-off surface ray rather than requiring the
attacker to occupy the enemy's differently eroded mesh or a tower's solid center.
Occupied attack positions are skipped. Paths are reused until the target moves,
navigation changes or movement fails, with bounded retries. Movement owns positions
and uses the crowd's swept-circle contact calculation plus cached local detours
around animals. Combat decides destinations and never teleports units.

`LevelRuntime` advances existing waves/towers/enemies and hero combat together in
bounded steps, including slow frames. GameLogic has no ImGui/Editor dependency.
GameHud handles commands/cards and projects target/ability feedback with existing
overlays. Engine's generic runtime-instance ray query reuses animated mesh picking;
terrain/obstacle hits occlude attack clicks.

`Hero.Runtime` checks movement/obstacles, acquisition, windup/one-hit damage,
1/5/60 FPS cadence, pursuit limits/recovery, unreachable/removed targets, move-order
priority, Cleave cone/cooldown and lethal packed-storage removal, independent heroes,
and the missing-animation fallback. `Game.Runtime` exercises actual HUD enemy
commands and Cleave alongside placement, upgrades, camera gestures, waves and restart.
`Combat.Battle` additionally checks the radius/profile regression, wall-end attack
positions, unreachable-nearest-target fallback, return to defense, manual commands
around stationary animals, enemy hero/tower attacks, destruction/rebaking, priorities,
crowd engagement and a simulation with 508 mixed animals and two active heroes.
See [combat](combat.md).
Build/test commands remain the README presets and CTest commands. Phase 26 logs
are under `out/phase26/`.
