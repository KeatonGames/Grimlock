# Build zones and free placement

In `Grimlock --editor`, use **Navigation > Build Zones** to create, select, rename,
delete, or edit a rectangular zone. Position and width/depth use world units;
rotation is yaw in degrees. New zones start at the camera target's X/Z and elevation
zero; adjust Y for elevated terrain. Zones also appear as model-less objects in the
existing hierarchy. The regular Inspector can edit their transforms. Keep zones
upright, with positive dimensions and no shear. The dedicated controls edit root
zones; parented zones can use the regular Inspector.

**Show Build Zones** toggles subtle viewport boundaries. Boundaries are always shown
while placing a tower, even with that toggle off. These are Editor overlays; normal
Game rendering does not display them. They neither block navigation nor participate
in model picking. Select a zone from its menu or the hierarchy.

Use **Navigation > Towers > Tower Type**, set **Tower Rotation (degrees)** if needed,
then **Place Tower**. Move freely over terrain and left-click to place. Escape or
**Cancel Placement** cancels. The translucent rotated box represents the physical
footprint: green passes preliminary checks, red explains a rejection. There is no
snapping. Required route checks run only on click. A green preview can still be
rejected if it seals a required route. Camera controls remain available.

Construction is allowed during preparation. Battle, victory and defeat disable
placement in both the UI and the navigation placement API. Outside level mode,
debug scenes act as preparation sandboxes. First Watch and the navigation debug
scene include authored zones. Other scenes need zones created before building;
an empty zone list deliberately grants no construction space. Existing towers are
not deleted when a zone changes or is removed; the new boundaries govern future
placements.

## Data and responsibilities

`BuildZone` is a Game data structure. `PlacementSystem` reads world transforms and
tests all four physical footprint corners against each rotated rectangle. A tower
must fit entirely inside one zone at the correct elevation; adjacent zones do not
combine to permit overhang. This uses no grid coordinates. Terrain support, walls,
props, other towers, and animals are still checked by TowerSystem. Existing
conservative world bounds remain the obstacle representation for collision and
Recast baking. Editing only zones does not rebuild navigation.

Zones use the existing scene JSON: a model-less object with role
`grimlock.build_zone`, name, position, yaw quaternion, and transform scale
`[width/2, 1, depth/2]`. They save and load with the scene referenced by a level's
JSON. No Engine-specific gameplay schema or second save file is needed. Restarting
a level restores its authored zones and towers.

`assets/config/towers.json` defines stable type IDs, display names, model paths,
physical `half_extent`, independent `visual_scale`, and tint. Basic and Wide
placeholder towers share the existing cube model. Changing type affects future
spawns. Placed towers save type through the opaque scene role
`grimlock.tower:<type>`, plus their normal transform/model. Legacy `grimlock.tower`
objects retain the Basic type. Loading resolves physical dimensions from the
catalog, independent of saved visual scale. Keep type IDs stable; unknown types
fail visibly rather than receiving a different footprint.

NavigationSession combines the placement rules with the existing NavMesh candidate
validation. It retains all future-wave spawn/profile routes, rejects route-closing
towers, installs a successful candidate once, and invalidates enemy paths through
the existing mesh revisions. Removing towers restores navigation. Pure visual
scale changes do not alter collision bounds or trigger navigation bakes.

Editor only projects generic rotated boxes and quads and hosts Game's controls.
No new renderer feature, navigation algorithm, economy, or combat behavior is added.
This checkout does not contain tower attacks or economy; those systems are not
introduced by this phase.

## Checks

`Towers.BuildZones` covers full-footprint containment, rotated zones/towers, exact
edges, continuous coordinates, elevation, disabled/deleted/invalid zones, independent
zones, visual scale independence, tower definitions, and scene round trips.
`Enemies.Runtime` exercises ImGui zone creation/deletion, tower type selection,
immediate previews, mixed types with shared assets, saved physical bounds, visual-only
edits without rebaking, navigation restoration and preparation-only placement.
Existing tower route, crowd, wave, editor and renderer checks remain applicable.
