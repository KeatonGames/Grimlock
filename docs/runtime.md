# Game and Editor applications

`Game` produces **Grimlock**. It launches First Watch by default, or accepts a
handcrafted level JSON path. Its compact HUD shows gold, health, wave, phase,
construction costs, combat statistics, completion rewards, and restart. Tower
buttons enter paid placement during preparation; left-click confirms and Escape cancels. Build zone
boundaries appear only while placing. Start Wave begins the authored schedule.
Victory and defeat have dedicated result cards. Mouse camera controls are unchanged;
clicking a placed tower shows its range and statistics. There is no general scene
selection, hierarchy, Inspector, debug menu, grid painting or docking.
Its Tower Info card also offers [preparation-only upgrades](tower_upgrades.md),
including next-level statistics, cost, affordability and maximum-level status.
The [hero controls](heroes.md) deploy/reposition during Preparation and accept
selected-hero movement/attack commands and Cleave during Battle, using the same camera
and shared scene picking/highlighting infrastructure.

`Editor` produces **GrimlockEditor**. It retains the existing docked editor and
navigation, build-zone, tower, animal and preview controls. Its tower tools remain
free authoring operations. `GrimlockEditor --level assets/levels/first_watch.json`
opens the same scene and configuration used by Game. A positional scene JSON still
opens just that scene. The former `Grimlock --editor` and standalone model-viewer
entry points are replaced by this separate authoring application; use its existing
hierarchy/model browser to inspect imported models.

## Shared implementation

- **Engine** owns scene/model/animation/input/rendering infrastructure.
- **GameLogic** remains the internal shared gameplay library. Definitions,
  NavigationSession, LevelSession, economy, waves, enemies, towers and placement
  validation live here. It has no dependency on Editor or ImGui. LevelRuntime in
  `game/src/runtime/` owns independent live level/navigation state and player
  placement input policy. It delegates purchases and route validation to the
  existing systems; it does not reimplement navigation or economy.
- **GameUi** in `game/src/ui/` is the player HUD and input/preview overlay. It
  depends on GameLogic and the generic UI bridge, not EditorTools or GameDebug.
- **EditorTools** contains the preserved editor implementation. **GameDebug**
  integrates shared gameplay into those development tools. Only the Editor
  executable links these libraries. Editor preview state is independent of Game.
- **ImGuiSupport** is an optional Engine UI integration target used by both apps.
  It extracts the existing SDL/Vulkan backend lifecycle, resize handling and
  polygon projection. Editor keeps its theme and docking layout; Game supplies its
  own theme and HUD. Engine's core target still does not link Dear ImGui.

Tower combat and projectiles run during Battle through the same LevelSession used
by editor previews. See [combat configuration and ownership](combat.md).

## Editing and persistence

Use **Navigation > Level > Level Definition** after loading a level to edit its
name, asset-relative scene reference, objective, starting gold, world spawn
rectangles, existing enemy groups, counts, batches, timing and rewards. **Level
File** selects where **Save Level Definition** writes the validated JSON. **Apply
and Restart Preview** applies the draft without saving. Scene edits are saved
separately through File > Save Scene, including build zones and authored towers.
The file format remains version 1 and is shared with Game.

Save source changes under `game/assets`, rather than relying on staged build
assets that the next build replaces. Game never saves runtime towers, gold, health,
waves or other runtime changes to these files. Restart reloads the selected file
and scene, restoring authored state and creating fresh gameplay state. Model/GPU
resources remain shared within each Engine instance; applications stage one shared
asset directory per build configuration.

Paths are executable-relative unless absolute. Passing an absolute level path
inside an `assets/levels` directory selects that directory's parent as the asset
root in both launchers. This allows direct play of source edits without restaging:

```powershell
.\out\build\release\Release\GrimlockEditor.exe --level "$PWD/game/assets/levels/first_watch.json"
.\out\build\release\Release\Grimlock.exe "$PWD/game/assets/levels/first_watch.json"
```

## Build and checks

Build either app independently with `cmake --build --preset release --target Game`
or `--target Editor`. With `GRIMLOCK_BUILD_EDITOR=OFF`, neither EditorTools,
GameDebug nor the Editor executable is created; the full player HUD still builds.
No new dependency or UI framework is introduced.

`Game.Runtime` links GameUi without any Editor target. It saves edited scene and
level data through the same serialization APIs used by the editor, loads them into
an independent runtime, and exercises real HUD input, paid placement, navigation,
waves, victory/defeat, restart, and unchanged source-file contents. It also asserts
that editor panels, docking and scene selection are absent. Existing Editor and
enemy runtime tests cover the preserved development tools and backend lifecycle.
