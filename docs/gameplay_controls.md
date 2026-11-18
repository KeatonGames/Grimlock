# Gameplay controls

Game and world-map cameras use **WASD** to move relative to their orientation,
**right mouse drag** to rotate, and the **wheel** to zoom. Keyboard movement eases
in and out using frame time; mouse rotation uses accumulated pixel displacement.
Pitch, zoom and authored map boundaries constrain the view. Middle-drag and short
right-click orders are no longer gameplay bindings. Editor cameras retain their
independent right-pan / middle-orbit controls.

## Strategy and direct hero control

Strategy is the default. WASD moves the camera, heroes keep their existing AI,
and left-clicks retain tower, construction and selection interactions. Heroes are
assigned by each level's `available_heroes` roster; empty rosters hide hero cards.
This update does not change the existing tower-only Greenhaven roster.

During an active wave, press **1**, **2** or **3**, or click a living hero's card,
to directly control that hero. Press its number again or **SPACE** to return to
strategy. Numbers do nothing during preparation. Slot assignments use hero type
order and do not change when a hero dies.

In direct control, WASD moves the hero relative to the camera. Diagonals are
normalized, navigation and crowd contacts constrain movement, and existing
acceleration, turning, terrain grounding and animations remain active. The camera
smoothly follows X/Z while preserving rotation and zoom. WASD never moves the
camera and hero simultaneously. Rotation alone does not rotate the hero.

Combat remains automatic in both modes. Direct heroes acquire targets within
normal range and keep existing cooldowns, projectiles, melee and area attacks.
AI chasing and return-to-defense movement are suppressed only for the controlled
hero. Other heroes retain their AI. Releasing control restores autonomous defense.

Wave completion, hero death, pause, restart and leaving gameplay clear direct
control. Strategy resumes near the current camera position. Losing keyboard
focus stops movement; text input suppresses shortcuts and keyboard navigation.
Escape retains the existing placement/selection/pause behavior.

Normal left-click hero orders still work outside direct mode, including during
preparation. Valid orders keep their terrain-following routes and destination
markers. Direct mode ignores those movement orders, preventing competing inputs.

## Hero cards and presentation

Up to three compact square cards appear on the right, below wave controls and
above the tower-inspection area. They show the actual model, a stable number,
health, a gold active border, and a muted dead/unavailable state. Unassigned slots
are hidden. Cards never appear in menus or on the world map.

The portraits are animated idle atlases rendered from the existing hero models
through Engine's Vulkan renderer. Each 1024-square atlas holds sixteen transparent
256-square views. Game displays one cached texture region per card at eight frames
per second; it neither reloads models nor renders additional 3D scenes per frame.
Regenerate them after changing hero assets:

```powershell
cmake --build --preset debug --target MenuCapture --parallel 4
.\out\build\debug\Debug\MenuCapture.exe "$PWD\game\assets" --hero-portraits
```

Currency, wave controls, construction, upgrades and existing combat UI remain.
Camera and hero controls do not spend gold or write authored level data.

## Validation

`Camera.Navigation` covers both camera profiles, WASD, right-drag rotation,
keyboard suppression and bounds. `Hero.Runtime` covers direct movement against
walls, movement release, automatic combat, switching, preparation gating, death
and restart. `Game.Ui` exercises number/SPACE input and hero-card visibility;
world-map and Editor checks guard their separate input contexts.
Debug verification passed for Game and Editor builds and nine focused checks:
Hero.Runtime, Game.Runtime, Game.Ui, Game.LevelMenu, Camera.Navigation, Input.State,
Camera.Runtime, Editor.Runtime and Game.Stress. The final hero-enabled HUD capture
ran with Khronos core/synchronization validation and no errors or hazards. The
capture uses a temporary hero-enabled level copy under out; authored level rosters
remain unchanged. Logs and the capture use out/controls_* and out/hero_controls*.
Release was not rebuilt for this update.
