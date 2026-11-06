# Kenney Nature Kit (2.1) — GLB assets for Grimlock

This folder includes all 329 GLB models from the supplied Kenney Nature Kit archive.
The assets are licensed under CC0; see `LICENSE.txt`. Attribution is optional.

The original GLBs are already installed here. CMake stages this directory with the
other game assets. The Editor's **Add Model** picker discovers it recursively;
use **Filter models** with `kenney_nature/tree`, `ground`, `cliff`, `rock`, or
`plant` to narrow the list. No separate asset registry or conversion is required.

**Example assets:**

- Ground and paths: `ground_grass.glb`, `ground_pathStraight.glb`, `ground_pathBend.glb`
- Elevated terrain: `cliff_block_rock.glb`, `cliff_blockSlope_rock.glb`, `cliff_steps_rock.glb`, `platform_grass.glb`
- Decoration: `tree_default.glb`, `rock_largeA.glb`, `plant_bush.glb`
- Bridges: `bridge_wood.glb`, `bridge_stone.glb`

The GLB files contain meshes/material definitions but do not automatically create gameplay navigation or build zones. Configure terrain vs. decoration collision, bake the NavMesh using walkable surfaces, and define build zones separately. Do not convert every visual prop into a navigation obstacle.

The handcrafted [Forest Defense level](../../../../docs/forest_defense.md) uses
these assets through ordinary relative scene references. It has imported terrain,
two ramps, five construction areas, five waves, and the existing Iron Stag boss.

The kit uses one-unit ground tiles and an exported Y offset of -0.05. Forest
Defense uses two-metre tiles and compensates that offset in scene transforms;
model files, materials and geometry remain unchanged. The importer handles the
kit's unused identity parent wrapper without converting the GLBs.
