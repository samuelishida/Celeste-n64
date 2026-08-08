# Plan: T3DM Room Renderer — Smooth Terrain from Brush Geometry

## Context

The current `LevelRenderer` draws each brush face as a flat polygon with a sprite texture. Even with correct UVs, scale, and atmosphere, the result looks nothing like the original Celeste 64. The OG game renders `.map` brushes through a smooth-shaded terrain pipeline with proper material textures. Our flat-polygon approach is the fundamental visual gap.

This plan replaces the flat-face `LevelRenderer` with a `.t3dm`-based room renderer. A new `bake_glb.py` generates a `.glb` (glTF 2.0 binary) from brush geometry with smooth normals and proper UVs. The existing `gltf_to_t3d` tool converts `.glb` → `.t3dm` (available at `/tmp/n64-bootstrap/opt/libdragon/bin/gltf_to_t3d`). A new `T3dmRoomRenderer` loads and draws the room model with proper texturing via `t3d_model_draw_custom()`.

The `.lvl` and `.colmesh` artifacts are kept for collision and entity spawn data — only the visual path changes.

## Architectural decisions

- **Decision: `.glb` intermediate + `gltf_to_t3d` conversion.** `bake_glb.py` generates a glTF 2.0 binary file from the parsed brush geometry. Then the Makefile runs `gltf_to_t3d` to produce the `.t3dm`. Rationale: `gltf_to_t3d` already handles vertex packing (16.0 fixed-point), normal compression, material chunk generation, and the complex N64 asset_t format. We don't need to reimplement those. All we write is the `.glb` geometry — the rest is handled by the toolchain. Alternatives rejected: (a) writing `.t3dm` directly from Python — the format is too complex (24-bit pointers, strip conversion, RSP vertex cache); (b) using the existing `bake_t3dm.py` minimal shell — produces no visible geometry.

- **Decision: Smooth normals computed from shared vertices.** For each face, we compute a flat face normal. When multiple faces share a vertex (within a position tolerance), the vertex normal is the average of all incident face normals. This produces smooth shading across connected brush faces. Rationale: this is what the OG Foster engine does; flat normals per-face would look faceted (same as current LevelRenderer).

- **Decision: Single `.t3dm` with multiple embedded material chunks.** The `.glb` exporter groups triangles by texture name into mesh primitives within a single glTF file. `gltf_to_t3d` converts this to a single `.t3dm` with multiple `T3DObject` chunks, each referencing a `T3DMaterial` chunk. The runtime uses `t3d_model_draw_custom()` which handles material switching internally. Rationale: a single file is simpler to manage, loads faster (one `asset_load` call), and matches how `.t3dm` is designed to work.

- **Decision: `t3d_model_draw_custom()` for runtime drawing.** The new `T3dmRoomRenderer` uses tiny3d's `t3d_model_draw_custom()` with a `drawConf` that provides a texture callback. The callback maps material names to `.sprite` files loaded via `sprite_load()`. This handles the N64 RDP state setup (combiner, tile descriptors) internally. Rationale: the simplest API that handles materials, textures, and BVH culling without manual vertex management.

- **Decision: `.lvl` kept for collision and entities, `.t3dm` for visuals only.** The existing `LoadLevel()` + `LoadCollMesh()` path is untouched. Only `LevelRenderer::Init()` + `LevelRenderer::Draw()` is replaced. The `GameplayScene` changes are minimal: swap one renderer init/draw call for another.

- **Decision: Smooth normals computed in Quake space, normalized, then transformed to game space.** Vertex normals are computed as averages of incident face normals in Quake space (Z-up). Normals are re-normalized after averaging. The game-space transform is `(nx, nz, -ny)` — rotation only, uniform scale so the inverse-transpose is the same as the forward transform (no need for matrix inversion). `gltf_to_t3d --base-scale` applies an additional 64× scale to vertex positions for 16.0 fixed-point range, but does not modify normals. Normals remain unit-length in game space after the rotation-only transform.

- **Decision: LevelRenderer retained as visual fallback.** After the `.t3dm` cutover, `LevelRenderer` is kept in the codebase and used as a fallback if `t3d_model_load()` fails. The `GameplayScene` attempts `.t3dm` loading first; on failure it falls back to the existing `.lvl`-based `LevelRenderer`. This guarantees the game is never left without visuals. Once the `.t3dm` path is proven stable, `LevelRenderer` can be deprecated in a follow-up. Rationale: no-regret migration; the new path is the primary, the old path is the safety net.

## Assumptions and answers from code

- **Answer from code: `gltf_to_t3d` is available.** Source: `/tmp/n64-bootstrap/opt/libdragon/bin/gltf_to_t3d` — confirmed present. Usage: `gltf_to_t3d input.glb output.t3dm --base-scale=64 --ignore-transforms --verbose`.

- **Answer from code: `t3d_model_draw_custom()` API.** Source: `tiny3d-main/src/t3d/t3dmodel.h:240-265` — `T3DModelDrawConf` struct with `tileCb` (callback for textures by material name) and `userData`. The callback receives `T3DMaterialTexture* tex`, `rdpq_tile_t tile`, and material name.

- **Answer from code: Current runtime model loading pattern.** Source: `src/user/gameplay/render/model.cpp:16-34` — `t3d_model_load(path)` loads `.t3dm` → `T3DModel*`, then optionally `rspq_block_begin()` + `t3d_model_draw()` + `rspq_block_end()` for display-list recording. We'll use `t3d_model_draw_custom()` directly (no display list needed for a single room model).

- **Answer from code: T3DVertPacked format.** Source: `tiny3d-main/src/t3d/t3dmodel.h:68` — 32 bytes: `posA[3]` int16, `normA` uint16 packed, `posB[3]` int16, `normB` uint16 packed, `rgbaA` uint32, `rgbaB` uint32, `stA[2]` int16, `stB[2]` int16. Positions are 16.0 fixed-point, UVs are 10.5 fixed-point.

- **Answer from code: glTF 2.0 binary format.** JSON header + binary buffer. `gltf_to_t3d` reads standard glTF 2.0 with positions, normals, UVs, indices, and material groups. We generate this from brush geometry.

- **Answer from code: `.sprite` files for textures.** Source: `filesystem/tex/` directory — `rock_1.sprite`, `snow_1.sprite`, etc. These are N64 sprite files generated by `mksprite` from PNG textures. The `gltf_to_t3d --ignore-materials` flag skips material generation so textures are loaded at runtime by our callback.

- **Answer from code: Material mapping.** The glTF material name is the texture base name (e.g., `"rock_1"`). Our runtime callback appends `".sprite"` and loads via `sprite_load("rom:/tex/rock_1.sprite")`.

## Risks accepted

- **Risk: `gltf_to_t3d` may not handle large meshes well.** The 1-1 room has ~1362 vertices and ~628 faces (1884 triangle indices). A glTF with 6 material groups and 1362 vertices is well within typical limits. Mitigation: test the full pipeline on 1-1 first; if `gltf_to_t3d` crashes, split into sub-models.

- **Risk: Smooth normals at brush seams may produce artifacts.** Where brushes from different entities meet (e.g., worldspawn wall meets SpikeBlock slab), shared vertices may not be exactly coincident. The vertex-dedup threshold (0.01 game units at scale 0.2) should catch most cases but may miss some. Mitigation: test visually; if artifacts appear, increase the threshold or compute normals per-entity.

- **Risk: DFS size increase.** The `.t3dm` room model will be ~200-500KB (1362 vertices × 32 bytes = 44KB for vertices, plus indices, materials, BHV). This is within the ROM budget. Mitigation: verify final `.t3dm` size; if too large, consider simplification or compression.

- **Risk: `gltf_to_t3d --ignore-materials` requires runtime texture loading.** Without baked material chunks, we need to load sprite textures in our draw callback. If a sprite file is missing, the face renders untextured. Mitigation: verify all 6 manifest textures have `.sprite` files (confirmed by `--check-sprites` flag on `bake_lvl.py`).

## Increment DAG

- Inc 1 — GLB baker: brush geometry → .glb with smooth normals (M) — depends on: none — unblocks: 2
- Inc 2 — Pipeline: .glb → .t3dm conversion + Makefile (S) — depends on: 1 — unblocks: 3
- Inc 3 — T3dmRoomRenderer: runtime loading + drawing (M) — depends on: 2 — unblocks: 4
- Inc 4 — Swap renderer in GameplayScene (S) — depends on: 3 — unblocks: 5
- Inc 5 — Validation: compare visuals + smoke test (M) — depends on: 4 — unblocks: none

All sequential — each increment depends on the previous.

## Increments

### Inc 1 — GLB baker: brush geometry → .glb with smooth normals (M)
**Depends on:** none
**Unblocks:** 2
**Done criteria:** `bake_glb.py` produces a `.glb` file from 1-1.map that loads correctly in Blender and shows smooth-shaded terrain with correct UVs and material groups.

#### Files to touch

##### tools/bake_glb.py (NEW)
- What changes: New script. Generates glTF 2.0 binary (.glb) from parsed brush geometry using `ogmap_lib.py`.
- Function(s):
  - `collect_geometry(parsed_map, scale) -> list[GlbMesh]` — iterates all `RenderMode.STATIC_MESH` brush entities. For each brush face: clip polygon, sort CCW (Quake-space normal), deduplicate, transform to game space. Fan-triangulate. Group triangles by texture name (material). Returns list of `GlbMesh` objects each with `{name, positions[], normals[], uvs[], indices[]}`.
  - `compute_smooth_normals(meshes, weld_epsilon)` — weld vertices by position (within epsilon), compute vertex normals as the average of all incident face normals in that mesh. Normalize. Store per-vertex normals.
  - `write_glb(path, meshes)` — write glTF 2.0 binary: JSON header + binary buffer. Buffer layout: per-mesh interleaved vertex data (pos float ×3, normal float ×3, uv float ×2 = 32 bytes per vertex), then index data (uint16 per triangle). JSON header references buffer views. Material names stored in `meshes[].material.name`.
  - `main()` — CLI: `python3 tools/bake_glb.py <in.map> --out <out.glb> --scale 0.2`
- Data shapes:
  ```python
  GlbMesh = {name, positions: list[Vec3], normals: list[Vec3], uvs: list[tuple], indices: list[int]}
  ```
- Integration points: Imports `ogmap_lib` for `parse_map`, `classify_entity`, `transform_point`, `compute_face_polygon`, `sort_vertices_ccw`, `dedupe_polygon_vertices`, `fan_triangulate`, `compute_uv`. Uses Python `struct` and `json` for binary writing.
- Error paths: Zero meshes → error, no output. Vertex count > 65535 → warn (uint16 index limit). Material name with special chars → sanitize.

#### Edge cases
- Brushes with co-planar faces produce shared vertices that should be welded for smooth normals
- Decoration faces use `snow_1` or `rock_2` textures — included in the mesh with proper material groups
- Death surfaces (SpikeBlock upward faces) use `floor_dirty_concrete` — also included as textured geometry
- TrafficBlock geometry uses `metal_floor_1` — included

#### Verification
- Run: `python3 tools/bake_glb.py assets/og_converted/maps/1-1.map --out /tmp/1-1-room.glb --scale 0.2`
- Open `/tmp/1-1-room.glb` in Blender: verify smooth shading, correct UVs, 6 material groups
- Check file size: ~200-500KB for 1362 vertices × 32 bytes = ~44KB + indices + JSON overhead

### Inc 2 — Pipeline: .glb → .t3dm conversion + Makefile (S)
**Depends on:** Inc 1
**Unblocks:** 3
**Done criteria:** `./compile-rom.sh` produces `filesystem/lvl/1-1.t3dm` that `t3d_model_load()` can successfully load (non-zero vertex/index count, readable objects).

#### Files to touch

##### Makefile
- What changes: Replace `bake_t3dm.py` with `bake_glb.py` + `gltf_to_t3d` in the 1-1 recipe. Also add glTF tool as prerequisite.
- Integration points:
  ```makefile
  GLTF_TO_T3D := /tmp/n64-bootstrap/opt/libdragon/bin/gltf_to_t3d

  filesystem/lvl/1-1.t3dm: \
      assets/og_converted/maps/1-1.map \
      tools/ogmap_lib.py \
      tools/bake_glb.py | filesystem/lvl
      python3 tools/bake_glb.py $< --out build/1-1-room.glb --scale 0.2
      $(GLTF_TO_T3D) build/1-1-room.glb $@ --base-scale=64 --ignore-materials --verbose
  ```
  Remove `tools/bake_t3dm.py` from the recipe (it's superseded).

##### tools/bake_t3dm.py
- What changes: Delete (replaced by bake_glb.py + gltf_to_t3d).

#### Edge cases
- `gltf_to_t3d` may fail if the `.glb` has invalid material references — `--ignore-materials` avoids this, but means textures must be bound at runtime via our draw callback
- The `--base-scale=64` flag controls vertex coordinate scaling within the 16.0 fixed-point range. At game scale 0.2, the map X range is [-282, 326]. With base-scale=64, the T3D integer range is approximately [-18048, 20864], well within [-32768, 32767]. Verify the actual range after conversion.
- **Material validation**: After `.t3dm` conversion, verify all material names referenced in the `.t3dm` have corresponding `.sprite` files in `filesystem/tex/`. This can be done with a simple Python script or added to the Makefile recipe.

#### Verification
- Run: `make clean && ./compile-rom.sh`
- Check: build output shows `bake_glb.py` running then `gltf_to_t3d` converting
- Check: `python3 -c "import struct; d=open('filesystem/lvl/1-1.t3dm','rb').read(); assert d[:3]==b'T3M'; print('OK')"`
- Check: `filesystem/lvl/1-1.t3dm` size is ~100-300KB (non-trivial)

### Inc 3 — T3dmRoomRenderer: runtime loading + drawing (M)
**Depends on:** Inc 2
**Unblocks:** 4
**Done criteria:** A new `T3dmRoomRenderer` class loads the `.t3dm` room model and draws it with correct textures, visible in the emulator.

#### Files to touch

##### src/user/gameplay/render/t3dm_room_renderer.hpp (NEW)
- What changes: Header for the new renderer.
- Class: `T3dmRoomRenderer`
  - `bool Load(const char* path)` — loads `.t3dm` via `t3d_model_load()`, sets up texture callback
  - `void Draw(const T3DMat4FP* matrix)` — draws the room model via `t3d_model_draw_custom()`
  - `void Free()` — frees the model via `t3d_model_free()`
  - Private: `T3DModel* model_`, `T3DModelDrawConf drawConf_`, `std::map<std::string, sprite_t*> sprites_`

##### src/user/gameplay/render/t3dm_room_renderer.cpp (NEW)
- What changes: Implementation. Key function: `Load(path)`:
  - Calls `t3d_model_load(path)` — loads the `.t3dm` binary
  - Sets up `drawConf_.tileCb` as a lambda/static function that receives a material name (e.g., `"rock_1"`) and loads `sprite_load("rom:/tex/rock_1.sprite")` into tile 0
  - Caches loaded sprites in a `std::map`
  - `drawConf_.userData = this`
- `Draw(matrix)`:
  - `t3d_matrix_push(matrix)`
  - `t3d_model_draw_custom(model_, drawConf_)`
  - `t3d_matrix_pop(1)`
- Integration points: Used by `GameplayScene` instead of `LevelRenderer`.

#### Edge cases
- Multiple materials sharing the same sprite — deduplicate in the sprite cache
- Model load failure (corrupt file) — fallback to empty render (no crash)
- Empty model (0 objects) — draw nothing, don't crash

#### Verification
- Compile and run ROM: `./compile-rom.sh`
- Verify ROM boots and the room model renders (even if untextured due to `--ignore-materials`)
- Check console output for any load errors

### Inc 4 — Swap renderer in GameplayScene (S)
**Depends on:** Inc 3
**Unblocks:** 5
**Done criteria:** The game renders the room using `T3dmRoomRenderer` instead of `LevelRenderer`. Player can still move, collide, and interact with entities.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: Replace `LevelRenderer` usage with `T3dmRoomRenderer`.
- In `Impl`: replace `LevelRenderer level_renderer` with `T3dmRoomRenderer room_renderer`
- In `ReloadBakedLevel()`: replace `level_renderer.Init(level_geometry)` with `room_renderer.Load(lvl_path_t3dm)` where `lvl_path_t3dm` is derived from `lvl_path` by swapping `.lvl` → `.t3dm`
- In `Render()`: replace `level_renderer.Draw(catalog)` with `room_renderer.Draw(identity_fp)`
- In `Shutdown()`: replace `level_renderer.Free()` with `room_renderer.Free()`
- Remove `MaterialCatalog` dependency (no longer needed for rendering)
- Integration points: `LevelRenderer` and `MaterialCatalog` are no longer used for room rendering. Keep them in the codebase for reference; they compile out naturally when the include is removed.

#### Edge cases
- The identity matrix used for room rendering must be allocated (`malloc_uncached`). Keep the existing `impl_->identity_fp` or create a new one in the renderer.
- `lvl_path_t3dm` derivation: replace `.lvl` with `.t3dm` in the path string.

#### Verification
- Run: `./compile-rom.sh` — compiles without errors
- ROM test: room geometry renders via .t3dm (may be untextured with `--ignore-materials`)
- ROM test: player can walk on collision geometry (from .colmesh, unchanged)
- ROM test: strawberry, cassette, player models still render

### Inc 5 — Validation: compare visuals + smoke test (M)
**Depends on:** Inc 4
**Unblocks:** none
**Done criteria:** The .t3dm-rendered room looks visually correct compared to the OG Celeste 64 1-1 map. Smooth shading is visible. Textures are applied correctly.

#### Files to touch

##### tests/bake_glb_smoke.py (NEW)
- What changes: New test for the glb baker + conversion pipeline.
- Function(s):
  - `test_glb_valid()` — run bake_glb.py, verify output is valid glTF 2.0 (JSON header + BIN chunk)
  - `test_glb_vertex_count()` — verify vertex count matches expected (~1362)
  - `test_glb_material_groups()` — verify 6 material groups (one per texture)
  - `test_glb_uvs_nonzero()` — verify UVs are populated
  - `test_t3dm_from_glb()` — run gltf_to_t3d on the .glb, verify .t3dm is non-zero and loads

##### Makefile
- What changes: Add `filesystem/lvl/1-1.t3dm` to DFS (already done from v3 pipeline). The `.t3dm` is now a full room model, not a minimal shell.

#### Verification
- Run: `python3 tests/bake_glb_smoke.py` — all pass
- Open `.glb` in Blender: terrain looks smooth, textures mapped correctly
- Emulator: level renders with visible textures (after adding `sprite_load` in tile callback)

## Cross-cutting verification

After all increments:
1. ROM boots with .t3dm room rendering (no flat-face LevelRenderer)
2. Terrain appears smooth-shaded (not faceted)
3. Textures are applied per-material (rock, snow, metal, etc.)
4. Collision still works (from .colmesh, unchanged)
5. Entity spawns still work (from .lvl, unchanged)
6. DFS includes full .t3dm room model
7. ROM size stays within budget

## Standards / common-mistakes referenced

- `.agents/map-creation.md` — applies to: artifact pipeline, DFS configuration
- `docs/room_artifact_contract.md` — applies to: .lvl for gameplay, .t3dm for visuals
- `docs/first-room-brief.md` — applies to: DFS budget limits

## Open questions (CONSIDER from review)

(To be filled by self-review pass)

## Out of scope

- StaticProp model instancing (trees, bushes, etc.) — separate plan needed
- Per-material texture generation from OG source PNGs
- Moving platform rendering via .t3dm
- BVH culling for the room model (tiny3d's `t3d_model_draw_custom` may handle this internally)
- Maps beyond 1-1
