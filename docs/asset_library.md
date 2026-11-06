# Asset library

The shipped `game/assets` library contains self-contained GLB models and animations,
JSON configuration/scenes/levels, and required license notices. Shader sources remain
under `engine/shaders`; the cleanup does not change shader, font or audio behavior.
There are no external model textures or binary sidecars in the shipped library.

## Organization

| Directory | Contents |
| --- | --- |
| environment/nature | Terrain, foliage, rocks, tile paths, snow variants and campsite props |
| environment/castle | Castle walls, gates, bridges, flags and architectural props |
| environment/dungeon | Dungeon rooms, floors, furniture, traps and props |
| environment/graveyard | Graves, crypts, fences, lights and graveyard props |
| environment/pirate | Ships, docks, sails, nautical structures and props |
| environment/primitives | Shared PBR/hierarchy demonstration fixtures |
| towers/structures | Buildable tower sections, construction variants and cube proxy |
| towers/weapons | Turrets, siege weapons, cannons, swords, axes and shields |
| towers/projectiles | Ammunition, arrows and beam models |
| enemies/animals | All existing animal enemy models |
| enemies/orcs | Orc character models |
| enemies/undead | Ghost, skeleton, vampire and zombie characters |
| enemies/other | Preserved UFO enemies |
| heroes/knight, ranger, mage | Playable animated heroes, compatible base models and class props |
| heroes/other | Other characters, generic props, mannequins and shared medium/large rig animations |
| licenses | Original pack licenses and retained provenance notices |

Subdirectories identify source families only after classification by purpose. For
example, castle siege engines are weapons, graveyard undead are enemies, pirate
tower parts are tower structures, and tower-defense terrain is nature. Animals,
UFOs and shared diagnostic primitives have explicit supplementary folders rather
than being mislabeled as orcs or undead.

## Phase 44 migration

The original 800 GLBs and 34 models available only as glTF/OBJ now resolve to **832
unique GLBs**. Thirty-one glTF props embed their buffers and images; cube, cover
ridge and witch were converted through the existing OBJ loader, preserving its
runtime geometry, mesh parts, normals, UVs and rendered material behavior. The
original importer did not load the witch's MTL, so conversion preserves its existing
untextured appearance. All existing GLBs retain their original data unless an
external dependency needed embedding.

Two byte-identical General/MovementBasic animation duplicates are consolidated.
Other variants remain separate. Medium and large rig animations, mannequins,
unanimated base characters and useful models not yet used in gameplay are retained.
The playable heroes and their hand attachments remain unchanged. Their preparation
script now consumes only retained GLB bases, animation libraries and weapon props;
see [hero integration](hero_types.md).

Removed from the old library: 515 FBX, 463 OBJ, 460 MTL, 31 glTF, 31 bin, 510 PNG
files (pack previews, redundant atlases and embedded dependencies), 21 shortcuts,
five HTML documents, three pack README files and the obsolete pack inventory JSON.
The three OBJ-only models and 31 glTF models were converted before their source files
were removed. Eight original licenses were relocated verbatim, with a provenance
notice preserving the original Grimlock/user-supplied asset terms.

[asset_migration.json](asset_migration.json) records every original model path and
its retained destination, including the two exact duplicates. Active C++, JSON,
Editor references, tests and current guides use the new paths. Historical archive
notes intentionally retain their original paths as historical records.

## Loading and verification

Game loads configured heroes/enemies and active-scene resources, not the full
library. Editor lists GLB paths recursively without loading their geometry until
needed. CMake synchronizes runtime assets and prunes deleted staged files, so old
pack sources and thumbnails cannot remain in an existing build's asset browser.

`Assets.Library` imports all 832 GLBs, samples every animation, checks embedded
buffers/textures, verifies all migration destinations, and validates every authored
JSON model reference. The OBJ converter
also checks mesh/triangle counts and bounds after conversion. To run the library
check separately:

```powershell
ctest --test-dir out/build/debug -C Debug -R '^Assets.Library$' --output-on-failure
```

The reusable importer still supports external OBJ/glTF inputs and tests generate
such fixtures under ignored build output; those are not shipped assets. Build and
runtime results for this migration are recorded in `out/phase44_*.log`. The known
Phase 42 Cleave-dependent `Level.Playable` small-tower strategy failure remains
separate from asset compatibility.

Phase 44 verification: Debug Game and Editor builds succeed. Across the full
44-check suite and focused reruns, 43 checks pass; only the pre-existing
Cleave-dependent Level.Playable strategy fails. The final manifest-aware
Assets.Library check passes for all 832 models and every migration destination.
Editor and scene-runtime reruns pass, and clang-format/diff checks are clean.
No extended manual visual playthrough was performed. The model library decreased
from approximately 267 MiB of original pack content to 187 MiB of organized GLBs.
