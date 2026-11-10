# Unique tower construction zones

Each enabled build zone supports one tower. The existing tower shop, prices, upgrades,
and HUD remain unchanged. Select a tower in the shop, then click an available zone.
The preview snaps to its construction point and facing. Green passes preliminary
checks; red indicates an occupied zone, invalid footprint, terrain/collision problem,
or a route rejection. Full route validation runs on confirmation. Gold is charged
only after successful construction. Every available construction point uses the
same compact 2-by-2-metre orange `construction-zone.glb` frame.
Its 11-centimetre border matches the hero selection marker's visual weight.
It closes the character-selection corners into a low, bevelled ground frame using
the same kit material, rather than selection-b's upright ribbon. Regenerate it with
`tools/prepare_build_zone.ps1`. The frame
disappears when a tower occupies the zone and returns when that tower is destroyed.
There are no orientation triangles or variable-size rectangle overlays. Authored
validation boundaries and construction points remain intact.

## Editor controls

Use **Navigation > Build Zones** to create, select, rename, move, rotate, resize,
or delete zones. **Construction Offset (metres)** sets the tower's foot position
relative to the zone's rotated frame; **Construction Facing (degrees)** sets its
additional yaw. Offsets do not scale with zone dimensions when using these controls.
The default construction point is the rectangle's center, facing along its yaw.
Set the zone's Y to the supporting terrain elevation. Elevated points use the same
terrain support, collision, and Recast route checks as ground-level points.

**Show Build Zones** displays the same uniform model frames in the Editor.
Selected-zone controls show occupancy and report legacy conflicts. Frames
also appear while placing towers. **Navigation > Towers > Tower Type > Place Tower**
uses the same single-tower rules, with free authoring rather than gameplay purchases.
Escape cancels placement. Camera and tower selection/upgrade controls are preserved.
Zones remain model-less objects in the regular scene hierarchy; root zones support
the dedicated controls. Keep transforms upright, positive, and free of shear.

## Persistence and occupancy

The existing `grimlock.build_zone` scene object stores rectangle position, yaw,
and `[width/2, 1, depth/2]` scale. Existing scene/level definitions need no destructive
migration: zones without explicit construction points default to their centers.
Editing a zone or constructing its first tower adds model-less
`grimlock.construction_anchor` and `grimlock.construction_point` children. The anchor
cancels rectangle scale before the point applies facing, preserving tower materials,
visual scale, and physical dimensions without shear. The regular scene serializer
saves these transforms and hierarchy; Engine needs no gameplay-specific schema.

New towers are children of their construction point. This persists their zone
ownership through saving/loading and editor moves. Deleting a zone uses the existing
scene hierarchy's cascading deletion and removes its owned towers. Legacy towers
retain their existing transforms and hierarchy. Their foot position assigns them to
the first matching zone in scene-ID order at the same elevation. In the legacy debug
scene, both plaza towers may therefore occupy the central zone: neither is deleted
or repositioned, and the Editor reports the conflict. Remove or relocate extra legacy
towers manually. The zone remains blocked until its last occupant is removed.

Runtime occupancy is rebuilt from live, generation-safe tower ECS entities. Tower
death/removal releases occupancy on navigation synchronization; recycled entity
slots, restarting, and changing scenes cannot retain old claims. All new construction
must use the point/facing and fit entirely in one zone. Adjacent zones do not combine
to permit footprint overhang. Collision remains independent of visual scale. Existing
economy, upgrades, combat, route preservation, and lazy navigation-profile builds are
retained; empty construction markers load no model assets.

## Validation

`Towers.BuildZones` checks fixed points, offsets/facing persistence, rotated footprints,
elevation, occupancy, legacy multi-tower warnings, and disabled/deleted zones.
`Towers.UniqueZones` exercises actual construction, duplicate rejection without gold
loss, ECS ownership, scene save/load, destruction, generation reuse, restart, and
level switching. `Terrain.Runtime` covers elevated construction and navigation
restoration. Existing Game/Editor runtime, UI, economy, combat, and stress checks
remain applicable.

First Watch's existing rear rectangle was narrowed and a separate rear approach
zone added, retaining both previously useful firing points while giving each its
own occupancy. The original zone IDs remain. Hill Watch and Cover Watch retain
their rectangles and use the previous hill defense location as an explicit point.
