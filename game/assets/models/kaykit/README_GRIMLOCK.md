# Grimlock — Complete KayKit Adventurers + Character Animations

Included original source packs (all supplied files retained):

- KayKit Adventurers 2.0 FREE: 6 characters, props, textures, original starter animations, glTF/GLB, FBX, OBJ and supporting files.
- KayKit Character Animations 1.1: all Rig_Medium and Rig_Large animation sets (GLB and FBX), mannequins, textures and supporting files.

Both original `License.txt` files are retained in their respective folders. Both are CC0.

## Folder layout

- `game/assets/kaykit/adventurers/Characters/gltf/*.glb` — all six original character models
- `game/assets/kaykit/adventurers/Assets/gltf/*` — all glTF props with their `.bin` buffers (do not split `.gltf` and `.bin` files)
- `game/assets/kaykit/adventurers/Animations/gltf/Rig_Medium/*.glb` — basic animations included with Adventurers
- `game/assets/kaykit/animations/Animations/gltf/Rig_Medium/*.glb` — expanded medium-rig animations
- `game/assets/kaykit/animations/Animations/gltf/Rig_Large/*.glb` — expanded large-rig animations
- `game/assets/kaykit/animation_catalog.json` — inventory of every GLB animation clip

All other source formats, textures, preview images and auxiliary files are preserved in these same folders.

## Initial three heroes (other characters remain available)

- Melee: `Knight.glb`, with `Melee_1H_Attack_Slice_Horizontal` from `Rig_Medium_CombatMelee.glb`
- Ranged: `Ranger.glb`, with `Ranged_Bow_Release` from `Rig_Medium_CombatRanged.glb`
- Area attack: `Mage.glb`, with `Ranged_Magic_Shoot` from `Rig_Medium_CombatRanged.glb`

Preserved for future heroes: `Barbarian.glb`, `Rogue.glb`, `Rogue_Hooded.glb`.

## Important: animation integration in Grimlock

**The six character `.glb` files have rigs/skins but NO embedded animation clips.**
The animation clips are contained in SEPARATE GLBs within `animations/Animations/gltf/Rig_Medium/`.
Importing a character GLB alone will not provide its walk/attack/death actions.

For the engine, either:
1. Implement compatible shared-rig animation loading/retargeting across the matching KayKit medium-rig skeletons, verifying joint names/hierarchy/rest transforms; or
2. Use Blender to attach chosen actions to each character and export new fully animated GLBs.

Do not assume animations transfer by index without checking joints and rest poses.
No GLBs were changed or re-exported in this package. The source files are intact.

Original content credits: Kay Lousberg (https://kaylousberg.com/). Attribution optional under the included CC0 licenses.
