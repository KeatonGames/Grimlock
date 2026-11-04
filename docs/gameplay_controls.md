# Gameplay controls and interaction priorities

Camera controls are unchanged in both Game and Editor: **right drag pans**,
**middle drag orbits**, and **wheel zooms**. Game continues distinguishing a short
right-click from a camera drag using the existing five-pixel accumulated motion
threshold. Camera bindings and camera code were left untouched in Phase 39.

- Left-click a hero or tower to select it; click empty ground to deselect. Unit
  orders continue when switching selection or clicking empty ground.
- During Battle, right-click ground to move the selected hero or an enemy to
  attack. Orders are issued on release so a camera drag never becomes a move.
- During Preparation, use Deploy Hero for positioning and the existing tower
  construction/upgrade controls. Battle still prevents construction/deployment.
- Escape first closes settings or resumes a paused game, otherwise cancels
  placement, then cancels an explicit hero order and clears selection. With no
  action/selection, Escape pauses. The Pause button remains available at any time.
- Cancel Order stops the hero at its current position and uses that position as
  its new defensive anchor. Automatic defense can still engage nearby enemies.

UI, focus loss, simultaneous selection clicks, placement mode, middle drag,
zoom, and right-drag movement suppress hero order gestures. A UI button press
cannot also select or place something in the world. Escape is not repeated while
held and suppresses world interactions for its cancellation frame.

Heroes/towers have subtle hover and selection rings. Accepted moves/attacks show
an eight-tenths-second destination marker; the selected hero's target retains its
existing red indicator. Placement retains the last preview while interacting with
UI or navigating the camera. Returning to the viewport updates it against the
actual terrain; an invalid terrain point still clears/rejects the preview through
the existing placement validators. Existing construction/upgrade confirmation,
health bars, hit flashes, and death feedback remain in use.

## Movement and combat corrections

- A manual move is path-validated before interrupting a valid move or attack.
  Rejected destinations leave the current action intact and show the existing
  command error. Repeated nearby move orders reuse the current valid path.
- Cancelling a move no longer leaves its old destination as the defense point;
  autonomous behavior therefore does not silently resume the cancelled order.
- Movement steps stop at steering points rather than passing a nearby funnel
  corner. Existing surface constraints, acceleration, deceleration, collision
  clearance, and frame substeps remain responsible for movement.
- Hero models blend toward their attack facing using the configured turning speed.
  Logical attack facing and damage timing remain independent from visual smoothing.
- Repeating an attack order on the same target preserves its animation, windup,
  cooldown, and path. Existing target acquisition and retry pacing are preserved.

Assassin, Ranger, and Warrior share these changes. Models, attack statistics,
Recast/Detour, ECS ownership, and existing combat mechanics are unchanged.

## Validation and limits

`Hero.Runtime` covers atomic/repeated move orders, cancellation anchors, repeated
attack windups, input gesture conflicts, all hero types, multiple frame rates,
and existing melee/projectile/area behavior. `Game.Runtime` checks preview
retention over UI, construction/upgrades, hero selection/orders, camera gestures,
and UI capture. `Game.Ui` checks selection-first Escape and pause/resume.
Terrain, battle, crowd, rendering, and Editor checks provide broader regression
coverage. Local build/test logs are in `out/phase39/`.

Game and Editor built successfully in Release and Debug. The full Release suite
passed 41/41 (146.26 s), followed by a 7/7 affected-check rerun after the final
visual-turn adjustment. The full Debug suite passed 41/41 (297.77 s). Vulkan
validation was enabled for runtime checks; formatting and diff checks passed.
The authored playthrough retains its expected winning and losing defense plans.

This pass does not alter camera feel, implement crowd avoidance, or replace
navigation. Existing large-battle navigation spikes and physically crowded
passages can still limit responsiveness. Runtime checks exercise real SDL/Vulkan
interactions; an extended manual playthrough was not performed.

