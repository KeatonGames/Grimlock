# Level 01 - Forest Defense

Level 1 follows the supplied overview reference: an entrance meadow on the left,
a wooden bridge in the center, a raised garden behind the entrance, and a broad
ramp leading to the walled mausoleum on the right. The level retains its UUID,
five waves, starting gold, rewards and objective combat rules. Level 1 has no
available heroes and is designed for tower-only play.

Choose **Level 01 - Forest Defense** in Game, or open its level file in Editor:

```powershell
$levelOne = (Resolve-Path game/assets/levels/forest_defense.json).Path
.\out\build\debug\Debug\Grimlock.exe $levelOne
.\out\build\debug\Debug\GrimlockEditor.exe --level $levelOne
```

The [level definition](../game/assets/levels/forest_defense.json) and
[scene](../game/assets/scenes/forest_defense.json) remain ordinary editable JSON.
The scene is handcrafted; there is no runtime terrain generation or navigation grid.

## Layout

Coordinates below are world X/Y/Z. The authored camera places the entrance at
screen left and the mausoleum at screen right.

| Feature | Position | Notes |
| --- | --- | --- |
| Enemy entrance | (10, 0, -3.5) | Stone arch, red light, entrance banner |
| Entrance construction point | (5.5, 0, -4.6) | Beside crates and a barrel |
| Raised garden construction point | (7, 1, 4.6) | Short grass ramp, fences and pines |
| Timber bridge | (-1, 0, 0) | Real ravine below; approximately 3.7 metres of usable width |
| Eastern construction point | (-7.5, 0, -2.4) | Lower meadow before the main ascent |
| Main ascent | (-7, 0 to 2, 6) | Broad grassy ramp |
| Terrace construction point | (-5.8, 2, 10.8) | Covers the final approach to the mausoleum |
| Objective route point | (-8.5, 2, 10.8) | Walkable approach outside the building |
| Mausoleum | (-8.5, 2.12, 14.2) | Modular crypt, pitched roof, glowing door and low steps |

Each of the four construction points accepts one tower using the existing shop.
Occupied frames disappear and return when their towers are destroyed. Their
current size and border thickness are preserved. `available_heroes` is empty for
Level 1; future levels can enable any combination of Knight, Ranger and Mage.

The courtyard has low stone walls, banners and two lit braziers. Pines, low timber
fences, rock groups, plants and flowers frame the islands while leaving tower
footprints and routes clear. A purple scenic backdrop matches the reference's
presentation and is excluded from navigation and terrain picking.

## Geometry and assets

The terrain uses the Kenney Platformer Kit's `block-grass-large` and
`block-grass-large-slope` GLBs. Overlapping island sections keep rounded grass
edges connected. Ramp ends overlap the adjoining flat ground. The existing
Recast/Detour radius, slope, step and crowd settings remain unchanged.

The bridge's visible solid timber base provides the walkable geometry; the kit's
planks, edge beams, caps and support legs provide detail. This prevents the source
plank gaps from disconnecting navigation for large enemies. The ground beneath
and beside the bridge remains a real gap rather than an invisible terrain plane.

The mausoleum uses the existing Graveyard Kit's `crypt-large` and matching roof.
Its roof and arched doorway are children of the objective, so they follow its
collapse and reset. The building's foot remains close enough to the terrace for
existing melee contact checks. Enemies stop at its perimeter and repeatedly attack;
they do not disappear on arrival. At zero health the level ends in defeat.

Three project-authored, self-contained GLBs provide the warm doorway, red portal
and brazier flame shapes. They are scenery with no gameplay or animation system
changes. Regenerate them with `tools/prepare_level_one_details.ps1`.

`tools/author_level_one.ps1` reproduces the authored scene and its spawn/objective
coordinates while retaining the level's existing wave data. Running it overwrites
scene edits, so normal authoring should use Editor. No generation tool runs when
the game starts. All scene models remain discoverable in Editor's model picker.

The old Kenney Nature Kit remains removed. All 153 supplied Platformer Kit GLBs
are retained with embedded textures: 147 environment/prop models, five future
characters, and one bomb projectile. Their [CC0 license](../game/assets/licenses/kenney_platformer_LICENSE.txt)
is preserved. Unused library models are not loaded at game startup.

## Waves and economy

The player starts with **120 gold** and the mausoleum has **10 health**.

| Wave | Composition | Fixed reward |
| --- | --- | --- |
| Scouts on the trail | 6 Goblins | 30 |
| Beneath the roots | 10 Goblins | 40 |
| The woodland raiders | 12 Goblins | 50 |
| Hold the ascent | 12 Goblins | 70 |
| The Goblin Boss | 1 Goblin Boss and 4 Goblins together | 0 |

The first four waves use only regular Goblins, retaining their existing group
timing. The final wave contains exactly five enemies: the Goblin Boss and four
Goblins, all scheduled at wave start as one swarm. The level totals 45 enemies.
Other enemy definitions remain available to later levels. Coin effects remain
visual; only the existing wave rewards add gold.

A verified tower-only plan starts with a Small Tower in the entrance meadow,
a Large Tower in the eastern meadow, and a Small Tower on the terrace. Upgrade
the terrace tower after waves one and three, and the eastern tower after waves
two and four. The terrace point covers the final approach without hero support.

## Validation and captures

`Level.Forest` validates all Platformer environment assets, four construction
points, both ascents, bridge clearance for the boss, the real ravine,
the absence of heroes, elevated tower placement and cost, downhill projectile hits, and
scene/Editor round-tripping. It also plays defended and undefended strategies
through all five waves and checks rewards, Goblin-only spawning, the exact final swarm, boss health bars,
restart and resource cleanup. `Undead.Runtime` checks repeated mausoleum attacks,
defeat, building collapse and restart for all enemy archetypes. Objective pursuit
is never rejected because the building's center lies beyond a defender pursuit
limit; those limits still apply when chasing heroes and towers.

```powershell
ctest --test-dir out/build/debug -C Debug -R '^(Level.Forest|Undead.Runtime)$' --output-on-failure
```

The level-selection preview is a real renderer capture from the authored camera.
For a fresh gameplay capture, build the optional capture tool and run:

```powershell
cmake --build --preset debug --target MenuCapture --parallel 4
.\out\build\debug\Debug\MenuCapture.exe "$PWD\game\assets" --level "$PWD\game\assets\levels\forest_defense.json" "$PWD\out\level_one_game.png"
```

Current build, test and capture logs use `out/reference_layout_*` and
`out/level_one_*`. Validation checks the authored level and existing mechanics;
it does not establish exhaustive gameplay balance or long-session leak freedom.
