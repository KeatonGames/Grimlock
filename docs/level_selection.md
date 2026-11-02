# Playing Editor levels in Game

Launch `Grimlock.exe` without arguments to open the main menu. Choose **Choose
Level**, select a battlefield, then press **Play**. Each entry shows its authored
name and optional description. During gameplay, **Restart Level** reloads the
current definition; **Choose Level** ends the current session and opens selection.
Victory and defeat panels also provide **Choose Level**. Selection has a **Main
Menu** button; the main menu provides **Quit**.

## Shared files and asset paths

Game and Editor use the same version-1 level JSON and scene files. No export,
alternate Game format or list of hardcoded levels is required. The active asset
root contains `levels/`, `scenes/`, `models/` and `config/`.

Levels now have stable IDs for [player progression](progression.md), independent
of names and paths. Editor assigns IDs on save; preserve an ID for a rename, and
assign a new one for an independent copy. Game keeps legacy ID-less levels playable
without changing their files.

By default both executables use the staged `assets` directory beside the executable,
independent of the shell's working directory. Game discovers JSON files recursively
under that root's `levels` directory. Saving a new Editor level there makes it
available without restarting Game. Selection refreshes automatically every two
seconds and has a **Refresh** button. Names and selection remain associated with
file paths, so adding, deleting or renaming other entries does not select the wrong
level.

For development, edit the source asset tree directly rather than a staged copy that
a build can overwrite. Point Game at that same asset root:

```powershell
$assets = Join-Path $PWD 'game/assets'
# Editor's existing absolute-level support uses this level's asset tree.
.\out\build\debug\Debug\GrimlockEditor.exe --level (Join-Path $assets 'levels/first_watch.json')
# Game sees new/edited levels in that tree immediately; no rebuild is needed.
.\out\build\debug\Debug\Grimlock.exe --assets $assets
```

`--assets` accepts an absolute path or an executable-relative folder. Packaged
builds simply distribute the executable with its adjacent `assets` directory;
there is no source-tree or Editor working-directory dependency. Existing explicit
level launches still work, e.g. `Grimlock.exe assets/levels/boss_watch.json`. An
absolute level directly inside an asset tree's `levels` folder selects that asset
tree, as before. For nested/external explicit paths, supply `--assets` when needed.
`--environment` remains available independently.

## Level metadata and validation

The existing Editor **Level Definition** menu edits **Level Name** and the new
optional **Description**. They serialize as `name` and `description`; existing files
without a description still load. Scene references, terrain, authored towers,
build zones, hero positions, enemy/boss definitions, exact waves, economy and the
objective continue through the existing loader.

Discovery checks level structure, scene parsing, referenced model presence and
enemy names without uploading models or building NavMeshes. Malformed files,
missing scenes/models and unknown enemies are skipped. The selection footer reports
unavailable entries with details on hover. Empty or missing directories leave a
usable menu rather than terminating Game.

Navigation, GPU asset loading and hero placement are validated when **Play** loads
the level. If those checks fail, partial runtime state is cleaned up and selection
shows an error with details on hover. Fix/save the level in Editor, then retry.
Each Play and Restart rereads the definition and scene from disk. The selected
asset root is fixed for the application session; use `--assets` to choose another
tree when launching.

## Lifecycle and ownership

`runtime/level_catalog.*` provides CPU-only discovery in GameLogic. GameHud owns
menu interaction and display. `LevelRuntime::load()` remains the single complete
loading path, with `unload()` clearing previews, heroes, enemy instances/death
effects, projectiles, tower runtime state, navigation profiles, economy, selections
and combat references before scene replacement. Failed initialization follows the
same cleanup path. Menu frames do not update or recreate gameplay systems.

Engine retains its reusable model/GPU caches where resources are pinned/shared;
temporary instances and scene resources are released normally. Returning to menus
disables camera navigation and clears pending ground commands. Game never writes
authored level or scene data. Editor authoring controls remain outside Game.

`Game.LevelMenu` exercises the actual menus, discovery of new files, optional
descriptions, malformed/missing levels, read-only gameplay, restart, live edits,
switching with active projectiles, asset-cache reuse, elevated boss waves,
victory-to-selection, main menu/quit and failed-load cleanup.

Both Game and Editor build successfully in Debug and Release. The complete suites
passed 36/36 tests in each configuration (256.21 seconds Debug, 121.95 seconds
Release), with Vulkan validation enabled and no validation errors or compiler
warnings detected. Modified C++ files pass clang-format and Git whitespace checks.
Logs are under `out/phase31/`.
