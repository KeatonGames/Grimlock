# Navigation debug lab

Run `GrimlockEditor.exe` to open `assets/scenes/debug.json` by default. The older demo, navigation and chicken scenes remain available through File > Open or an explicit command-line scene path.

For an immediate movement demo without editor panels, run `Grimlock.exe assets/scenes/debug.json`; Game mode automatically spawns its usual 100 chickens.

The 24 × 18 map uses the existing cube terrain and shared cube model. The camera frames the entire map between the editor panels. Hierarchy groups identify its test areas:

| Area | Purpose |
| --- | --- |
| Teal staging strip, left | Default spawn rectangle; space for a compact crowd. |
| Gold-capped openings | Four-cell-wide routes usable by every configured animal model. |
| Purple-capped openings | Two-cell entrance and one-cell exit shortcut for testing physical clearance. Chickens fit the single-cell shortcut; larger animals use the wide route. |
| Central plaza | Clear floor for placing towers, redirecting crowds and comparing alternate routes. |
| Two blue sample towers | Real selectable, removable tower obstacles, sharing one model resource. |
| Green pad, bottom right | Default destination/arrival area. |
| Purple pocket, top right | A walkable but isolated region for testing Blocked state and recovery after opening a wall. |

Colored ground markers reserve the staging, destination and isolated test areas through the existing scene-model placement checks. They remain walkable for animals. Ordinary unmarked floor is available for tower placement; no terrain types or gameplay rules were added.

## Quick checks

1. Use **Navigation > Spawn Animals** to start 100 chickens. Try another **Animal Model** for mixed-size crowds. The default route remains connected for all 11 configured animals, including both sample tower footprints.
2. Use **Navigation > Towers > Place Tower** in the central plaza. A useful clear position is world X/Z `(2.4, -2)`. The footprint preview is immediate; clicking also protects valid routes. Try restricting one opening while leaving the alternate opening available.
3. Cancel placement, select either blue tower and remove it. **Show NavMesh** makes the restored space and changed routes visible.
4. To test unreachable movement, clear the existing animals and set **Spawn Area (world X/Z)** to minimum `(8.5, -7.5)`, maximum `(10.5, -5.5)`. Spawn chickens in the purple pocket; their default destination is outside, so they become Blocked. Paint a few cells on the pocket's lower wall walkable to reconnect it and watch them retry.

Default settings for restoring the main scenario:

| Setting | Value |
| --- | --- |
| Spawn minimum X/Z | `(-11.5, -8.5)` |
| Spawn maximum X/Z | `(-4.5, 8.5)` |
| Spawn height | `0` |
| Destination X/Z | `(11.5, 8.5)` |

Animals disappear when they reach the goal, keeping the exit clear. Use Clear Animals to reset remaining animals during an experiment. Save authored changes to the source `game/assets/scenes/debug.json` or a new source scene, since builds replace the staged executable-side assets.

`Towers.Cpu` verifies the scene's actual Recast/Detour routes for every animal configuration, sample tower obstacles, the chicken-only shortcut and the isolated pocket.

Debug and Release builds passed. The tower, NavMesh and scene CPU checks passed in both configurations, and the default Editor launch was opened and visually reviewed. Verification logs are in `out/debug-scene/`.
