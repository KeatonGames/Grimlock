# First Watch: Meadow Gates

Run `Grimlock.exe` to play. In Editor, use **Navigation > Level > Load First Watch**.
The default level is `game/assets/levels/first_watch.json`; the hand-authored engine
scene is `game/assets/scenes/first_watch.json`. The technical `demo.json` is unchanged.

## The battlefield

Two marked meadow entrances at approximately (-5, -3) and (-5, 3) feed a west gate,
then diverge around the central wall. World positions below are (X, Z).
Three build zones cover west interception (-3.3, 0), central coverage (0.1, 0),
and the expanded rear courtyard (5.3, 0). The objective is the gold marker at (7, 0).
The extra east-side ground makes rear tower placement useful and gives melee groups
room to clear the objective. Towers must preserve routes for every scheduled animal.

Start with **90 gold**, one Assassin and **10 objective health**. Each arrival costs
one health. No income or kill rewards are generated. Damage, hero deaths and tower
losses persist between waves; preparation does not heal defenses. Restart resets all
runtime state, including upgrades and gold. Hero deployment/repositioning is free.

## Five authored waves

| Wave | Composition and entrance | Timing in seconds | Reward |
| --- | --- | --- | ---: |
| First footsteps | 6 Chickens north | Pairs at 0, 4, 8 | 25 |
| Southern sprint | 6 Chickens north; 6 Rats south | Chicken pairs 0, 4, 8; Rat pairs 3, 6, 9 | 35 |
| Hold both gates | 4 Dogs north; 8 Chickens south; 4 Rats north | Dogs singly 0, 4, 8, 12; Chicken pairs 2, 5, 8, 11; Rat pairs 9, 12 | 45 |
| Heavy crossing | 4 Deer south; 6 Dogs north; 12 Rats south | Deer singly 0, 5, 10, 15; Dog pairs 2, 6, 10; Rat trios 5, 8, 11, 14 | 60 |
| The last watch | 6 Deer north; 8 Dogs south; 12 Rats north; 8 Chickens south | Deer pairs 0, 6, 12; Dog pairs 2, 6, 10, 14; Rat trios 5, 8, 11, 14; Chicken groups of four 12, 17 | 0 |

There are exactly **90 animals**. All counts, groups, start times, intervals and
rewards are literals in level JSON. No difficulty formula or randomness is used.
The first wave teaches interception, the second pressures both gates, the third
adds sustained melee pressure, and the last two combine heavy bodies with fast leaks.

## Balance and decisions

| Enemy | Health | Speed | Damage | Aggression / pursuit |
| --- | ---: | ---: | ---: | --- |
| Chicken | 100 | 1.4 | 6 | 2.6 / 4 |
| Black Rat | 60 | 2.0 | 5 | 1.8 / 3 |
| Dog | 150 | 1.55 | 10 | 3 / 4.5 |
| Deer | 240 | 1.15 | 14 | 3.5 / 5.5 |

Small Towers cost 30 and offer cheap coverage: 25/36/50 damage, 1.5/1.7/2 shots per
second and 3.5/3.9/4.3 range. Upgrades cost 25 then 40. Large Towers cost 60 and offer
heavier shots, reach and durability: 55/80/110 damage, 1/1.15/1.3 shots per second,
4.2/4.7/5.2 range and upgrades of 45 then 65. Health remains 250 versus 500. Both
retain separate physical footprints. See [tower upgrades](tower_upgrades.md).

The hero retains 100 health, 25 basic damage and 1.2 physical melee reach, with
3.5 detection and 6 pursuit range. Cleave remains 45 damage with a six-second
cooldown. It can interrupt an automatic basic attack; the cancelled basic swing
cannot also land. The hero supports a defended lane but cannot clear the level alone.
Do not leave a wounded hero surrounded by Dogs or Deer; retreat orders override AI.

The HUD previews the next wave's animals, entrances and reward during Preparation.
During Battle it shows active/pending enemies and wave clearance progress. Damaged
Dogs/Deer and the selected hero's target show health bars using the existing overlay.
Selected heroes, targets, movement markers, hits and deaths retain existing feedback.

## Tested plans and limits

`Level.Playable` uses the real authored scene, economy, navigation, combat and waves.
It simulates player decisions, not a privileged build path or modified enemy health:

- **Mixed battery:** Small Tower (-3.3, 0), Large Tower (5, 0), forward hero
  (-0.3, 1.5). Upgrade Small after wave one; buy a rear Small at (3.7, 0) after
  wave two; upgrade Large after waves three and four. Move the hero back before
  Dogs arrive and use Cleave against groups. Tested victory: 10 objective health.
- **Small Tower coverage:** west Small plus Small Towers at (0.1, -2.5) and
  (0.1, 2.5), a central hero, staged upgrades and a rear Small before wave four.
  Elevation-aware regression result: 2 objective health, with defenders lost during the final wave.
- **Rear defense:** Small at (4, -2.5), Large at (4, 2.5), hero (4, 0), upgrading
  available towers between waves. Tested victory: only 2 objective health remaining.
- **No construction:** the default hero alone loses in wave two.
- **No development:** the mixed opening without upgrades, reinforcements or Cleave
  loses in wave four despite unspent gold.

Every run verifies one-time exact rewards, all scheduled enemies, navigation
containment, a bounded battle duration and restart from the resulting win/loss.
These deterministic checks establish a playable balance baseline, not a guarantee
that every placement wins. Human playtesting is still needed to tune subjective feel.
The Assassin still has no animation clips; it uses the established timed static-pose
fallback. Indicators remain ordinary HUD overlays and are not depth-occluded.

Original playable-level logs are under `out/phase27/`; elevation-aware verification
and updated playthrough traces are under `out/terrain/`.
Game and Editor build in Debug and Release; all 33 CTest checks
pass in both configurations, including identical win/loss outcomes for the five
plans above. Vulkan validation reports no errors or synchronization hazards.
Formatting and whitespace checks pass.
