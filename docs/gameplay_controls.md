# Gameplay controls

Knight, Ranger and Mage spawn automatically on clear, reachable ground near the
defense objective whenever a level starts or restarts. There is no hero shop,
roster panel or deployment step.

- Left-click a hero to select it. The orange kit corner marker identifies the selected hero.
- Left-click a position on the map to move the selected hero there. This works
  during Preparation and Battle, with the same terrain-aware navigation.
- A successful move deselects the hero and hides its square. Its yellow route
  and destination arrows stay visible during movement. Click the hero again
  before issuing another order.
- A dashed yellow line previews the selected hero's route to the mouse. Yellow arrows
  mark the destination after clicking and remain while the hero moves there.
  These are visual guides; movement and attack commands work as before.
- The dashed route follows movement steering and terrain height. Travelled sections
  disappear behind the hero; the remaining dashes stay fixed rather than sliding.
- Selected heroes keep their original material colors and use only the square
  corner marker for selection.
- Click another hero to select that hero. Click a tower to inspect or upgrade it.
- During Battle, clicking an enemy with a hero selected issues an attack order.
  Short right-click commands also remain supported; right-drag pans the camera.
- The selected hero keeps its command while you move the camera. Manual commands
  temporarily override autonomous combat; normal defense resumes afterward.
- Middle-drag orbits and the mouse wheel zooms.
- Escape cancels tower placement first, then clears selection/orders, then opens
  Pause. Escape resumes paused gameplay or closes Settings.

The towers-only shop is centered at the bottom. Select a tower card, then click
an available build zone to construct at its fixed point and facing. Each zone holds
one tower; destroying it frees the zone. Gold and tower costs appear in the shop. Escape or Cancel
Placement exits build mode. Tower placement takes precedence over hero movement.

The top HUD has Round at the left, objective health in the center, and Start Wave
at the right. Start Wave is enabled during Preparation. Pause, Settings, Restart,
level selection and exit controls are available through Escape and the Pause menu.
Hero statistics, portraits, health overlays and ability buttons are absent.

UI clicks, camera gestures, paused gameplay and result screens suppress map
commands. Invalid destinations preserve the selected hero and its current valid
order. Hero selection and movement do not spend gold or modify authored level files.
