# Game UI

Game uses its own HUD and input routing without Editor dependencies.

Main menu, level cards, persistent display options and Pause share the angular purple, cream and gold
theme described in [Game menus](menus.md). The playable catalog contains only
Forest Defense; former demos are Editor/test fixtures outside it.

## Main layout

- Top left: cream gear button for Pause/Settings.
- Top center: round number beside the cream objective health bar.
- Top right: Start Wave, enabled during Preparation.
- Bottom left: compact bow and mage-hat tower cards, with prices.
- Bottom right: coin model and current gold balance.

The three top widgets have transparent backgrounds; the old status rectangle is
removed. The shop has no hero roster, deployment controls, hero information or wave
controls. Tower placement feedback remains inside the shop. Selecting a tower opens
its cream-edged upgrade/details panel on the right. Boss encounters retain their health bars
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

Gold appears at the bottom right beside the tower shop, with a rotating projection
of the actual dungeon coin mesh beside the current balance. Goblins award two coins immediately on death; Level 1 starts with 20 coins and has no wave bonuses.

The shop stays anchored at the bottom left as the window and UI scale change. Top widgets
remain anchored to their respective corners/center, without a full-width panel.
Tower details have a separate area on the right and can scroll when space is
limited. Noninteractive wave notices remain transparent to world input.

Runtime checks cover automatic party placement, independent movement during
Preparation, real click-to-select/click-to-move input, UI command capture, tower
purchase/upgrade, pause/settings/restart, level switching and responsive bounds from
640x480 through 1920x1080. Current build/test results are in
`out/simple_hud_*.log`.


The shared presentation uses clipped corners, offset dark shadows, cream borders, warm gold primary buttons and dark purple panels. The renderer-capture tool accepts `--tower` after a `--level` output path to inspect the selected-tower layout.

The graphics MSAA option applies to the final HUD composition too, including range/build circles, projected coin triangles and panel silhouettes. Game and Editor overlays use the same selected sample count; text remains at native resolution.
