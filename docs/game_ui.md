# Game UI

Game uses its own HUD and input routing without Editor dependencies.

## Main layout

- Top left: round number.
- Top center: objective health bar.
- Top right: Start Wave, enabled during Preparation.
- Bottom center: towers-only shop, with gold, prices and tower cards.

The three top widgets have transparent backgrounds; the old status rectangle is
removed. The shop has no hero roster, deployment controls, hero information or wave
controls. Tower placement feedback remains inside the shop. Selecting a tower opens
its upgrade/details panel above the shop. Boss encounters retain their health bars
below the top row; transient wave notices and result screens keep their existing
behavior.

Knight, Ranger and Mage spawn near the objective automatically. Click a hero, then
click ground to move it in Preparation or Battle. The Tower Defense kit corner marker and destination
markers provide feedback without hero panels or world-space hero health bars.
Clicking another hero changes selection; clicking a tower opens tower information.
The marker uses `towers/structures/tower_defense/selection-a.glb` with its original
orange material. It follows the selected unit, aligns to hero navigation slopes,
and is a render-only instance removed on deselection, menus and HUD shutdown.
Camera gestures and UI interactions do not issue map orders.

Escape cancels placement, clears selection/orders, then opens Pause. The Pause menu
provides Resume, Settings, Restart Level, Choose Level, Main Menu and Quit. Settings
controls session UI scale and world health bars. Pausing freezes simulation and
animation clocks while allowing UI rendering. Result screens retain Retry, Next
Level and level selection.

## Layout and validation

The shop stays horizontally centered as the window and UI scale change. Top widgets
remain anchored to their respective corners/center, without a full-width panel.
Tower details have a separate area above the shop and can scroll when space is
limited. Noninteractive wave notices remain transparent to world input.

Runtime checks cover automatic party placement, independent movement during
Preparation, real click-to-select/click-to-move input, UI command capture, tower
purchase/upgrade, pause/settings/restart, level switching and responsive bounds from
640x480 through 1920x1080. Current build/test results are in
`out/simple_hud_*.log`.


Debug Game and Editor builds pass. Across the full 45-check run and focused reruns,
44 checks pass; the known Cleave-dependent Level.Playable small-tower strategy
failure remains. Marker lifecycle, input, layout, forest, level-menu and gameplay
stress checks pass on the final implementation.
