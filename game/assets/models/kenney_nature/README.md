# Kenney Nature Kit (2.1) — GLB assets for Grimlock

This folder includes all 329 GLB models from the supplied Kenney Nature Kit archive.
The assets are licensed under CC0; see `LICENSE.txt`. Attribution is optional.

**Usage:** Extract the ZIP at the root of the Grimlock repository. The models will be placed under `game/assets/models/kenney_nature/`.

**Example assets:**
- Ground and paths: `ground_grass.glb`, `ground_pathStraight.glb`, `ground_pathBend.glb`
- Elevated terrain: `cliff_block_rock.glb`, `cliff_blockSlope_rock.glb`, `cliff_steps_rock.glb`, `platform_grass.glb`
- Decoration: `tree_default.glb`, `rock_largeA.glb`, `plant_bush.glb`
- Bridges: `bridge_wood.glb`, `bridge_stone.glb`

The GLB files contain meshes/material definitions but do not automatically create gameplay navigation or build zones. Configure terrain vs. decoration collision, bake the NavMesh using walkable surfaces, and define build zones separately. Do not convert every visual prop into a navigation obstacle.

If the project's current asset folder differs, move this directory accordingly and update asset references in the level data.
