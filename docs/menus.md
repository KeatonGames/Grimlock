# Game menus

The playable `game/assets/levels/` library contains only the existing **Level 01 -
Forest Defense**. Its current reference-inspired layout is documented in the
[level guide](forest_defense.md). Five former demo definitions now live under
`game/assets/test_fixtures/levels/`; automated tests and the existing Editor load
shortcuts use those paths. Their reusable scenes and models remain available.

**START GAME** opens Forest Defense directly. **LEVELS** shows discovered levels
as responsive 16:9 preview cards with their configured names and completion state.
Hover a card for saved attempts, victories and best time. Select and press **PLAY**,
or double-click. Future valid files are discovered normally, without locked levels.
Previews use `assets/previews/<level filename stem>.png`.

**OPTIONS** applies real SDL windowed/borderless fullscreen, windowed resolution,
Vulkan VSync, UI scale and world-health-bar preferences. Fullscreen uses the desktop
resolution. **APPLY** saves `options.json` beside the existing SDL user progression
file; failures remain visible. There is no audio system, so no volume control is
shown. Mouse and keyboard navigation use the existing ImGui infrastructure. Escape
returns to the previous menu or opens/closes Pause after canceling gameplay tools.
Pause offers Resume, Restart Level, Options and Main Menu. Returning to Main Menu
unloads gameplay safely. Quit requests normal application shutdown.

The reusable menu theme uses the specified forest, sage, gold, ivory, stone and
muted-red palette, roughly 200 ms hover easing and screen fade-in. The combat HUD
keeps its existing presentation. Menu colors and screenshots are decoded for the
sRGB color attachment to avoid washed-out output. Images are cached as UI textures;
the menu does not render a live environment or require the Editor.

## Captures and licenses

`assets/ui/menu_background.png` is an actual 1600x900 Vulkan capture of a dedicated
Platformer Kit composition: grassy blocks, elevated banks, a winding stone trail,
trees, plants, fences, a supply chest and barrel, with warm directional lighting and a
three-quarter camera. This non-playable composition exists only during capture;
it does not add a level or duplicate terrain assets.

`assets/previews/forest_defense.png` is a real capture of Level 1's authored scene
with a preview camera, without modifying the scene. Both PNGs are staged normally.
Karla is reused from the pinned Dear ImGui source, with its copyright notice and
SIL Open Font License included under `assets/ui/`.

Regenerate from the repository root:

```powershell
cmake --build --preset debug --target MenuCapture --parallel 4
.\out\build\debug\Debug\MenuCapture.exe "$PWD\game\assets"
cmake --build --preset debug --target Game --parallel 4
```

`MenuCapture` is excluded from normal builds. Engine reads back a completed Vulkan
frame, and the existing stb dependency saves PNGs. No stock or AI-generated images
are used. For a main-menu review capture, supply an additional output PNG path.

## Validation

`Game.LevelMenu` checks the single shipped level, captures, direct Start, real
fullscreen/windowed and VSync application, saved-options reload, future discovery,
progression, switching, recovery, restart, quit and unchanged Level 1 metadata.
Its Options and selection review captures are written beside the test executable.
`Game.Ui` covers responsive layouts, pause, settings and gameplay input capture.
Other Game and Editor checks retain their relocated fixtures. Logs are under
`out/phase48_*.log`.

Debug Game and Editor build successfully. All 47 checks are verified across the
full suite and focused reruns, including the corrected CPU wave-fixture argument.
The final UI rerun and clang-format/whitespace checks also pass. Main menu, Options
and the single-level selection captures were visually inspected.
