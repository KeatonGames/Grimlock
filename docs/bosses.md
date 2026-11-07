# Basic bosses

Bosses are ordinary EnemySystem instances with a data-driven `boss` flag. They
share enemy targeting, animation timing, damage/death handling, local crowd
movement and Recast/Detour navigation. GameLogic owns their behavior; only GameUi
adds the prominent health display. Editor uses the same definitions and combat
without depending on the Game HUD. There are no boss phases or special attacks.

## Configuration

`game/assets/config/enemy_archetypes.json` includes **Iron Stag**, using the existing
Deer model and shared GPU resources:

| Setting | Value |
| --- | ---: |
| Maximum health | 1600 |
| Damage per hit | 45 |
| Tower damage modifier | 1.5 (67.5 damage) |
| Movement speed | 0.85 |
| Attack interval / impact time | 1.5 / 0.4 seconds |
| Attack range | 0.65 |
| Detection / pursuit range | 5 / 7 |
| Collision radius / height | 1.0 / 2.0 |
| Visual height | 1.8 |

The definition uses `boss: true`, `maximum_health`, `speed`, `radius`, `height`,
`visual_height` and the existing `combat` fields. Collision radius/height are world
dimensions independent of visual scale; `visual_height` scales only presentation.
`animal` chooses the model and default animations. Tower, hero and objective
priorities are 3, 2 and 1, respectively. Objective arrivals keep the level's existing
`damage_per_arrival` rule. High health supplies durability without introducing armor
or a separate resistance system.

Any animal or archetype can opt into the boss HUD with `boss: true`; names and
health limits are read from the definition/runtime instance. Omitted boss and size
override fields preserve older catalogs. Invalid physical dimensions and statistics
use existing validation. Definitions load with the navigation session, so restart
the application after editing configuration files.

Each distinct physical size uses the existing radius/height NavMesh profile.
Bosses cannot take routes through gaps that are too narrow or ceilings that are too
low. Tower placement validation also preserves routes for scheduled boss groups.
Author spawn centers with enough clearance for the entire footprint. Unreachable
targets use the existing rejection, fallback and bounded pursuit behavior.

## Game health display

During Battle, living active bosses have an individually named crimson health bar
with gold text at the top center. Numeric health shows the actual current/maximum
values; the fill interpolates exponentially toward the actual fraction, independent
of frame rate. Bars stack in stable spawn order. A bounded panel gains a scrollbar
when necessary and moves below the status panel on narrow windows to avoid hiding
its controls. Boss death/removal, restart or leaving Battle clears the display.

Layout, colors and smoothing are local to `GameHud::bossHealth()` in
`game/src/ui/game_hud.cpp`: maximum width 560, bar height 32, panel height capped at
40% of the viewport, and smoothing response 8 per second. There are no boss-name
checks or Editor UI dependencies.

## Handcrafted encounter

Run Game with the new level:

```powershell
.\out\build\release\Release\Grimlock.exe assets/test_fixtures/levels/boss_watch.json
```

In Editor, choose **Navigation > Level > Load Boss Watch**. Its wave definition
editor's existing **Enemy Type** dropdown includes Iron Stag. Boss groups use the
same `enemy`, `spawn`, `count`, `batch_size`, `start` and `interval` fields as minions.
No level schema changes are needed.

Boss Watch reuses Hill Watch's terrain, ramp and elevated build zones. It starts
with 200 gold and a hero. Wave 1 has four Grunts and a fixed 30-gold reward. Wave 2
has six Grunts plus one Iron Stag scheduled at three seconds, with no final reward.
The boss has a separate authored spawn at (-6, 0, 0), clear of the map boundary.
Build towers during preparation and start each wave through the normal HUD.
Game's level selection also lists this encounter; First Watch remains available.

`Boss.Runtime` checks damage to both defender categories, hero and tower kills,
radius-restricted passages, elevated movement, simultaneous/scrollable HUD bars,
smooth health updates, death cleanup and wave completion only after the boss is
resolved. Definition and Editor runtime checks cover configuration and loading.

Both Game and Editor build in Debug and Release. Full CTest runs passed 35/35 in
each configuration (239.04 seconds Debug, 107.81 seconds Release), with Vulkan
validation enabled and no validation errors or compiler warnings detected.
Modified C++ files pass clang-format and Git whitespace checks. Verification logs
are under `out/archive/phase30/`.
