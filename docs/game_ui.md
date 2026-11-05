# Game interface

Grimlock Game uses a slate, warm gold and muted teal palette, vector command icons,
rounded borders and clear type sizes. Main menu, selection, results, command cards,
health meters and pause/settings share `ui/game_style.*`. Button outlines animate
on hover, menus have a short entrance motion, and battle notices fade out. No art
download, new UI framework or Vulkan rendering feature is required.

## HUD and controls

- The top strip shows phase, wave, objective health, gold and **Pause**. The level
  name, authored wave composition and reward are available on hover. Boss health stays below this strip, with individual
  named crimson bars and overflow scrolling.
- The bottom-left command dock contains tower cards with icons/costs, affordability
  and selection states, hero selection, deployment and **Start Wave** during
  Preparation. Hover a tower card for its combat values or disabled reason.
- Selecting a tower opens its bottom-right health/statistics/upgrade card. It shows
  next-level improvements, the actual upgrade cost, a disabled reason or MAX LEVEL.
  The existing attack-range overlay and upgrade flash remain active.
- Select a hero from its command portrait or the world. Its contextual card shows
  health, current orders/target, combat statistics, Cancel Order and Cleave with a
  readiness meter/countdown. Right-click orders and camera gestures are unchanged.
- Placement keeps the existing terrain-aware checks and translucent preview, with
  softer pulsing valid/invalid colors and a contextual rejection explanation.
  Health bars have a contrasting frame, low-health tint and brief damage outline.
- Wave-start/clear notices use real wave/reward data and do not intercept world
  input. Existing target markers, hero orders and ability feedback are preserved.

**Escape** first cancels an active placement/deployment. Otherwise it pauses or
resumes an active level. Pause also provides Resume, Settings, Restart Level,
Choose Level, Main Menu and Quit. Leaving/restarting resets pause state. Result
screens keep Retry, Next Level and level selection, with existing progression.

Escape resumes a paused placement without cancelling it; another Escape cancels
the placement once gameplay resumes. Save failures keep a visible status warning
with details on hover, and the strip grows to accommodate it without clipping.

Pause freezes navigation/movement, combat, cooldowns, wave scheduling, progression
time and animation clocks while continuing to render UI. `LevelRuntime` owns the
paused simulation flag. A small generic Engine animation-clock pause hook avoids
altering individual playback settings; it defaults off and Editor never enables it.

Settings are available from the main menu and Pause. UI Scale and World Health
Bars are session preferences. Boss health remains visible regardless of the world
bar setting. Settings does not introduce gameplay, graphics or progression options.

## Responsive layout and input

Font size and spacing scale together from logical window size and the UI scale
preference. The status strip, bottom dock, selected-unit card and boss region have
separate bounds. Smaller windows give contextual cards/long explanations scrolling
instead of extending beyond the viewport. Selected-unit cards remain contextual;
camera instructions and construction details live in Settings/tooltips rather than
permanent debug text.

UI hover and modal state explicitly suppress world selection and commands. Pause,
settings and results capture input, release relative mouse mode and clear pending
ground gestures. Returning to play restores camera navigation. Noninteractive
notifications remain transparent to input.

Editor keeps its own context, style and tools. Game UI does not reference or link
Editor controls. Player saves, level discovery and combat definitions are unchanged.

## Verification

`Game.Ui` exercises actual Vulkan-rendered controls, pause/resume/restart, settings
and real UI scaling, and verifies frozen movement, timing and animation while
paused. It checks status/dock/hero/boss bounds and non-overlap at 1920x1080,
1280x720, 960x540, 800x600 and 640x480, including an increased UI scale. Existing
Game runtime tests cover tower purchase/upgrades, hero commands/abilities,
selection, camera gestures and victory/defeat. Boss, menu/progression and Editor
checks remain separate.

Desktop screenshot capture was unavailable in this execution session (invalid
desktop handle), so no screenshot is presented as a visual review. To inspect
locally, launch Grimlock normally; `GameUiChecks.exe --preview` also holds a HUD
scene and then the main menu for inspection. Verification logs are in
`out/archive/phase33/`.

Game and Editor builds pass in Debug and Release. Full suites passed 38/38 in each
configuration (269.18 seconds Debug, 131.82 seconds Release); focused UI regressions
were rerun after the final warning/escape refinements. Vulkan validation was enabled.
A pre-existing C4456 shadow warning in `game/src/navigation/level_geometry.cpp`
was observed during the full rebuild; this UI phase leaves that module unchanged.
