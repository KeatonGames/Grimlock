# Interactive kingdom world map

Main Menu **PLAY** opens `assets/scenes/world_map.json`, a rendered Engine scene.
No LevelRuntime, hero or enemy is created until the selected location's triangular
launch button is pressed. Clicking a location only selects it and smoothly focuses
the camera. Clicking empty terrain clears selection. BACK or Escape returns to the
main menu and releases the scene. Launch uses a short fade and the existing level
loader, with normal validation and error handling.

Map landmarks use the role `grimlock.map_location:<level UUID>`. Their scene name,
model and world transform provide the label, visual landmark and location. The UUID
is resolved against the existing playable-level catalog; completion comes from
PlayerProgress. Missing/unknown IDs remain visible but unavailable. Use
`grimlock.map_location:locked:<UUID>` to keep an authored location locked. There
are no invented playable levels or separate progression saves.

Open the scene in GrimlockEditor to edit terrain and decorations. Select a map
landmark to edit its Map level ID and locked state in the Inspector, or duplicate
it to add a location for a real level. The map currently contains Greenhaven and
scenic ruins, forests, mountain formations, a lake and connecting roads. Only the
implemented Greenhaven location can launch.

Regenerate the initial scene explicitly with `tools/author_world_map.ps1` (overwrites
map edits). The map camera supports bounded WASD movement, right-drag rotation, wheel zoom and smooth selection focus;
there are no hero controls on this screen.

Validation uses `Game.LevelMenu` for the new flow, locked markers, Back navigation,
explicit launch and existing options/discovery behavior.

Verified Debug Game and Editor builds. Game.LevelMenu, Game.Ui, Game.Runtime and Editor.Runtime passed after updating the obsolete card-selection UI checks. A Khronos validation capture of the world-map scene reported no validation errors. Captures and logs use out/world_map_*.

Level definitions may specify `required_level`, a stable prerequisite ID. World-map launch requires that chapter's persisted completion record. Locked registered locations remain selectable with disabled Play. Gravewatch Hollow requires Greenhaven Valley; its marker uses the ordinary `grimlock.map_location:<id>` role. The level editor preserves and exposes this prerequisite.
