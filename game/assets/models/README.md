# Sample models

`hill_terrain.glb` is the handcrafted lower meadow, ramp, hill platform and isolated
overlook used by Hill Watch and Cover Watch. `cover_rock.obj` adds Cover Watch's
solid mountain ridge. Scene roles classify the terrain and combat blockers; see
[terrain authoring](../../../docs/terrain.md) and [combat visibility](../../../docs/combat.md).

`cube.obj`, `hierarchy.glb`, and `pbr-comparison.glb` are original geometric assets created for
Grimlock and dedicated to the public domain under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). No attribution is required.

`hierarchy.glb` contains a tower and smaller annex:

- Two glTF meshes, with two indexed body primitives and one non-indexed roof primitive.
- Interleaved positions, normals, and UVs; 8-bit and 16-bit indices.
- A rotated root, a transform-only group, translated/scaled children, and an explicit roof matrix.
- Body/roof meshes referenced by both towers without duplicate mesh definitions.
- Two scenes: scene 1 is the default. Scene 0's distant tower must not appear.

The hierarchy fixture remains available with `Grimlock assets/models/hierarchy.glb`.
The imported fixtures are unchanged. `pbr-comparison.glb` exports the previous
Game-generated 24-by-48 sphere (same positions/normals/UVs/tangents/indices) and its
six dielectric/metallic roughness materials. Six objects in `assets/scenes/demo.json`
share its single primitive and select different material overrides; no procedural
generation code remains in Game.

`assassin.glb`, `dogcat.glb`, `witch.obj`, `blackrat.glb`, `bunny.glb`, and `bunny2.glb`
are user-supplied assets, unchanged by this
implementation. Their licenses are not established here; the CC0 dedication above
does not apply to them. Do not redistribute them without checking their source terms.

The demonstration preserves the current character/cat/bunny placement and
adds two blackrat instances sharing one GPU model, alongside six PBR spheres.
The OBJ cube remains available through the model argument; its unused default
upload was removed during the Engine/Game refactor. The character has four
base-color JPEGs, four metallic-roughness JPEGs, and four normal-map PNGs; all are
shaded. The cat has a gray dielectric material and no textures. Neither GLB has
AO/emissive textures. Both GLBs contain skins but no animation clips. Their joint-node
local transforms are identity while inverse-bind matrices are not; the importer
reports this and uses undeformed bind-pose previews rather than collapsed default
skins. Re-export with complete joint transforms for an intended skeletal pose.
See the main README for counts, supported material features, and limitations.

`blackrat.glb` contains three primitives, three materials, six embedded PNGs,
one skin with 235 joints, and 13 clips. Clearcoat (eyes), specular/IOR (masked fur),
and UV1 textures are now rendered. There are no compressed textures, morph targets,
or punctual lights in this asset. Its first clip `_Static Pose` is intentionally
static; the second demonstration instance plays `idle_A1` independently. Neither
bunny file has skins or clips. Synthetic checks exercise morph targets, eight
influences, cubic interpolation, and animated imported lights absent from these assets.
