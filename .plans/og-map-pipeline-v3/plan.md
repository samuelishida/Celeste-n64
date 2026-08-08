# Plan: OG Map Pipeline v3 — Library-First, Class-Driven, Artifact-Unified

## Context

`bake_og.py` produces a colmesh whose BVH is in world-space coordinates (the runtime dequantizes BVH AABBs using `quant_scale`/`quant_origin`, so world-space int16 values produce garbage bounding boxes). It also misses UV coordinates (every face samples texel 0,0), skips atmosphere properties, and has no `.t3dm` output. Beyond these bugs, the monolithic-one-tool architecture makes it hard to add a class system or integrate with the asset pipeline.

This plan replaces it with a **library-first, pass-based** architecture where:
- A shared Python library (`ogmap_lib.py`) provides clean types for the parsed scene
- Each artifact (colmesh, lvl, t3dm, manifest) is produced by an independent pass
- Entity classes and material classes are defined in a single data table
- The BVH is built from **quantized** positions (matching the runtime dequantization path)
- UVs, atmosphere, and sprite catalog integration are all handled

## Architectural decisions

- **Decision: Library-first architecture.** `ogmap_lib.py` defines clean Python types (`Map`, `Entity`, `Brush`, `Face`, `MaterialClass`, `EntityClass`) and the shared parse/transform/clip pipeline. Thin baker scripts (`bake_colmesh.py`, `bake_lvl.py`, `bake_t3dm.py`) import from the library. Rationale: separates concerns; each artifact baker is independently testable; the shared library eliminates duplicated polygon clipping / triangulation / coordinate transform code. Alternatives rejected: (a) monolithic script again — proved fragile; (b) keeping bake_og.py architecture with patches — the BVH fix is easy but the design is wrong at the foundation.

- **Decision: Class system as a single data table.** `CLASS_REGISTRY` in `ogmap_lib.py` maps every OG classname to a `ClassDef(material_class, entity_id, render_mode, filter)`. Both the colmesh baker (material flags) and the lvl/t3dm bakers (entity spawns, face flags) read from the same table. Adding a new OG class is one line. Rationale: single source of truth; no scattered dicts across scripts; the mapping IS the documentation of what each OG class means in our engine.

- **Decision: BVH built from quantized int16 positions.** `bake_colmesh.py` quantizes vertices first (int16), then builds the BVH from the quantized positions. The BVH node AABBs are stored as quantized int16 values. The runtime's `DequantAabb` correctly reconstructs world-space AABBs from these. This matches `colmesh_bake.py`'s working approach exactly. Rationale: this is the root cause of collision not working; fix it at the architectural level so it cannot regress.

- **Decision: UV computation using Quake texture axis projection on Quake-space points.** `bake_lvl.py` and `bake_t3dm.py` compute UVs from the face's polygon vertices **in Quake space** (before the game-space transform), then transform the vertex positions to game space for the binary output. This is correct because `compute_uv` projects against Quake texture axes (X/Y/Z-up). Computing UVs from game-space points would project onto wrong axes (Y-up vs Z-up). Rationale: the old working pipeline did UV computation before the transform; we must do the same.

- **Decision: Unified artifact output.** One invocation of the pipeline produces ALL runtime artifacts: `.colmesh` (collision), `.lvl` + `.manifest` (entity spawns + legacy render data), `.t3dm` (visual mesh), and `.nav` (moving platform paths). The Makefile target produces all of them from a single dependency: the source `.map` file. Rationale: the runtime needs all of these; generating them separately invites inconsistency.

- **Decision: Atmosphere properties extracted from worldspawn.** `bake_lvl.py` reads `skybox`, `music`, `ambience`, `snowAmount`, `snowDirection` from the `worldspawn` entity and writes them into the LVL header. The runtime's `level_loader.cpp` reads these and populates `Room::skybox`, `Room::music`, etc. Without them the level has no skybox, music, or snow. Rationale: this is standard Quake map metadata; we were simply not propagating it.

- **Decision: `.t3dm` output uses fan-triangulated faces with per-vertex UVs.** `bake_t3dm.py` generates a `.t3dm` file containing the visible room geometry (solid + visual-only faces) with correct UV coordinates and material references. The runtime's `level_renderer` (or a future `.t3dm` path) loads this for textured rendering. Rationale: `.lvl` faces are suitable for sprite-based rendering but `.t3dm` supports smooth normals and proper texture mapping for higher visual fidelity.

- **Decision: Rejected — keeping `bake_og.py` with patches.** The BVH fix is a one-line change (pass quantized positions to `build_bvh`). But the architecture has deeper problems: no class system, no asset integration, UVs missing, atmosphere missing, monolithic structure. Patching it would address the immediate symptom without solving the structural issues. A clean rewrite is warranted.

## Assumptions and answers from code

- **Assumption: The runtime's `DequantAabb` uses `quant_scale` and `quant_origin` from the colmesh header.** Source: `src/user/gameplay/physics/coll_mesh.hpp:147-155` — confirmed. BVH node AABBs must be in the same quantized coordinate space as vertices.

- **Assumption: `colmesh_bake.py` builds BVH from quantized positions.** Source: `tools/colmesh_bake.py:152-237` — its `build_bvh` receives `qverts` (quantized int16 positions) and reads `qverts[tri[0]]` for AABB computation. Confirmed — this is the reference implementation.

- **Assumption: `bake_map.py`'s `compute_uv` works but needs the world scale propagated.** Source: `tools/bake_map.py:343-415` — the function has a `scale=0.2` parameter that is **not used** for the critical `tex_per_unit = 0.003125` constant (which equals 1/320). At `scale=0.15` the tiling will differ from `scale=0.2`. The library's `compute_uv` wrapper must set `tex_per_unit = 1.0 / (320.0 * scale / 0.2)` or equivalently `1.0 / (1600.0 * scale)` to produce consistent UVs across scale values. The library function signature is `compute_uv(point: Vec3, face_def: FaceDef, world_scale: float) -> Tuple[float,float]`.

- **Assumption: `.t3dm` format is a tiny3d model with vertex positions, UVs, and material groups.** Source: `src/user/gameplay/render/level_renderer.cpp` loads `.t3dm` via `t3d_model_load()`. The format is defined in tiny3d's `src/t3dmodel.c:200-400`. Layout: header (magic `"T3DM"`, version uint16, mesh_count uint16, flags uint16), per-mesh: vertex_count uint32, index_count uint32, material_name char[64], then `T3DVertPacked` array (32 bytes each: float pos[3], float norm[3], float uv[2], pad[2]), then uint16 index array. Big-endian. Our baker will produce this format. Reference: `tiny3d-main/src/t3dmodel.c` line 200–400.

- **Assumption: The LVL2 header atmosphere fields are at the documented offsets.** Source: `tools/lvl_format.py:20-45` — `skybox_str_id` at +0x1C, `music_str_id` at +0x1E, `ambience_str_id` at +0x20, `snow_amount_q8` at +0x22, `snow_dir` at +0x24. Confirmed.

- **Answer from code: Entity IDs for spawnable classes.** Source: `tools/entity_ids.py` — PlayerSpawn=0, Strawberry=1, Refill=2, Spring=3, Cassette=9. These are the only entity classes the runtime currently spawns.

- **Answer from code: Material flag bit definitions.** Source: `src/user/gameplay/physics/coll_mesh.hpp:57-61` — `MAT_SOLID=0x0001`, `MAT_ONEWAY=0x0002`, `MAT_DEATH=0x0004`, `MAT_CLIMBABLE=0x0008`, `MAT_ICE=0x0010`.

- **Answer from code: Polygon clipping is correct in `bake_map.py`.** Source: `tools/bake_map.py:211-340` — `clip_polygon_by_plane`, `compute_face_polygon`, `sort_vertices_ccw`, `dedupe_polygon_vertices` are working and tested. The library imports these directly.

- **Answer from code: `sort_vertices_ccw` must receive Quake-space (untransformed) normal.** Source: `tools/bake_map.py:528` — called with `face_def["normal"]` (Quake-space), not `transform_normal(face_def["normal"])`. The reversal in `sort_vertices_ccw` is designed for Quake-space normals.

## Class system design

### MaterialClass enum
```python
class MaterialClass(IntEnum):
    SOLID = 0       # MAT_SOLID (0x0001)
    DEATH = 1       # MAT_SOLID | MAT_DEATH (0x0005)
    CLIMBABLE = 2   # MAT_SOLID | MAT_CLIMBABLE (0x0009)
    VISUAL_ONLY = 4 # 0 — not in colmesh
    TRIGGER = 5     # 0 — non-solid trigger volume
    ONEWAY = 6      # MAT_SOLID | MAT_ONEWAY (0x0003)
    # ICE (3) reserved for future; runtime doesn't check MAT_ICE yet
```

### EntityClass enum
```python
class EntityClass(IntEnum):
    NONE = -1           # brush-only, no spawn
    PLAYER_SPAWN = 0    # maps to entity_id 0
    STRAWBERRY = 1      # maps to entity_id 1
    REFILL = 2          # maps to entity_id 2
    SPRING = 3          # maps to entity_id 3
    CASSETTE = 9        # maps to entity_id 9
    TRAFFIC_BLOCK = 100 # moving platform (needs .nav)
```

### RenderMode enum
```python
class RenderMode(IntEnum):
    NONE = 0           # invisible (triggers, spawn points)
    STATIC_MESH = 1    # included in .t3dm + .lvl
    ACTOR_MODEL = 2    # separate .t3dm, not baked into room mesh
```

### FaceFilter enum
```python
class FaceFilter(IntEnum):
    NONE = 0           # emit all faces
    UPWARD_ONLY = 1    # only faces with game-space normal y > 0.3
```

### CLASS_REGISTRY
```python
ClassDef = namedtuple('ClassDef', 'material_class entity_class render_mode face_filter')

CLASS_REGISTRY: dict[str, ClassDef] = {
    "worldspawn":              ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "PlayerSpawn":             ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.PLAYER_SPAWN,   RenderMode.NONE,        FaceFilter.NONE),
    "Strawberry":              ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.STRAWBERRY,     RenderMode.NONE,        FaceFilter.NONE),
    "Refill":                  ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.REFILL,         RenderMode.NONE,        FaceFilter.NONE),
    "Spring":                  ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.SPRING,         RenderMode.NONE,        FaceFilter.NONE),
    "Cassette":                ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.CASSETTE,       RenderMode.NONE,        FaceFilter.NONE),
    "SpikeBlock":              ClassDef(MaterialClass.DEATH,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.UPWARD_ONLY),
    "DeathBlock":              ClassDef(MaterialClass.DEATH,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.UPWARD_ONLY),
    "Decoration":              ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "FloatingDecoration":      ClassDef(MaterialClass.VISUAL_ONLY, EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "TrafficBlock":            ClassDef(MaterialClass.SOLID,       EntityClass.TRAFFIC_BLOCK,  RenderMode.STATIC_MESH, FaceFilter.NONE),
    "FallingBlock":            ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "FloatyBlock":             ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "GateBlock":               ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "MovingBlock":             ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "CassetteBlock":           ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "BreakBlock":              ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
    "DoubleDashPuzzleBlock":   ClassDef(MaterialClass.SOLID,       EntityClass.NONE,           RenderMode.STATIC_MESH, FaceFilter.NONE),
}

SKIPPED_CLASSES: dict[str, str] = {
    "Node":         "pathfinding node, no geometry",
    "func_group":   "TrenchBroom layer container, no geometry",
    "StaticProp":   "visual-only point entity, no collision",
    "Coin":         "needs coin runtime (future)",
    "Feather":      "needs feather runtime (future)",
    "SignPost":     "needs sign/dialog runtime (future)",
    "IntroCar":     "cutscene entity, no gameplay collision",
    "Granny":       "NPC, no collision geometry",
    "Theo":         "NPC, no collision geometry",
    "Badeline":     "NPC, no collision geometry",
    "Chimney":      "needs chimney runtime (future)",
    "FixedCamera":  "camera hint, no geometry",
    "EndingArea":   "trigger volume, no geometry",
}
```

## Risks accepted

- **Risk: `.t3dm` binary format may need careful byte alignment.** The tiny3d `.t3dm` format has specific packing requirements (T3DVertPacked is 32 bytes). Our baker must match this exactly or the model won't load. Mitigation: test with tiny3d's `t3d_model_load()` in the smoke test; compare binary output against known-good `.t3dm` files.

- **Risk: The class system mapping may not cover all entities in all OG maps.** The `CLASS_REGISTRY` covers the 19 brush-bearing classes found in 1-1, but future maps may introduce new classes. Mitigation: `SKIPPED_CLASSES` documents 13 additional classes with reasons; unknown classes trigger a loud warning and are skipped.

- **Risk: UV computation depends on scale factor.** `compute_uv` from `bake_map.py` uses a hardcoded `tex_per_unit = 0.003125` (1/320). This works for the default WORLD_SCALE=0.2 but may need adjustment for 0.15. Mitigation: test visually on 1-1 (which uses 0.15) and compare with the old pipeline's output.

- **Risk: The library architecture may seem like overengineering.** Three small scripts importing from one library vs one monolithic script. Mitigation: each script is ~100 lines; the library is ~400 lines; total code is similar to bake_og.py (~700 lines) but cleaner and better tested. The separation pays off when debugging (each artifact can be tested independently).

## Increment DAG

- Inc 1 — Library: types + parser + class registry (S) — depends on: none — unblocks: 2, 3, 4, 5
- Inc 2 — Collision baker: colmesh with quantized BVH (M) — depends on: 1 — unblocks: 6
- Inc 3 — LVL baker: faces with UVs + entities + atmosphere + sprite check (M) — depends on: 1 — unblocks: 6
- Inc 4 — T3DM baker: visual mesh with UVs (M) — depends on: 1 — unblocks: 6
- Inc 5 — Nav baker: TrafficBlock path data (S) — depends on: 1 — unblocks: none
- Inc 6 — Pipeline integration: Makefile + ROM build + verification (S) — depends on: 2, 3, 4 — unblocks: 7
- Inc 7 — Validation suite (M) — depends on: 6 — unblocks: none

Inc 2, 3, 4, 5 can all run in parallel after Inc 1.

(Inc 6 — Material catalog: sprite manifest + asset linking — was folded into Inc 3. The sprite check is small enough to live in bake_lvl.py's `--check-sprites` flag.)

## Increments

### Inc 1 — Library: types + parser + class registry (S)
**Depends on:** none
**Unblocks:** 2, 3, 4, 5, 6
**Done criteria:** `ogmap_lib.py` parses `1-1.map`, prints entity class registry lookups, and produces a `ParsedMap` with all entities, brushes, faces, and textures cataloged.

#### Files to touch

##### tools/ogmap_lib.py (NEW)
- What changes: New shared library. Clean Python types for the parsed scene, the class registry, and shared pipeline functions (parse, transform, clip, triangulate).
- Function(s):
  - **Types**: `Vec3`, `FaceDef`, `Brush`, `Entity`, `ParsedMap`
  - **Enums**: `MaterialClass`, `EntityClass`, `RenderMode`, `FaceFilter`
  - **Registry**: `ClassDef`, `CLASS_REGISTRY`, `SKIPPED_CLASSES`
  - `parse_map(path) -> ParsedMap` — Quake .map parser → structured scene
  - `transform_point(p, scale) -> Vec3` — Quake→game coordinate transform
  - `transform_normal(n) -> Vec3` — Quake→game normal transform (rotation only)
  - `classify_entity(ent) -> ClassDef | None` — CLASS_REGISTRY lookup
  - `is_upward_face(face, class_def) -> bool` — filter check
  - `compute_face_polygon(brush_faces, face_idx) -> list[Vec3]` — import from `bake_map.py`
  - `fan_triangulate(verts) -> list[tuple[int,int,int]]` — NEW code in library (~10 lines); not imported (does not exist in bake_map.py)
  - `compute_uv(point: Vec3, face_def: FaceDef, world_scale: float) -> tuple[float,float]` — wrapper that imports `bake_map.py`'s `compute_uv` but sets `tex_per_unit = 1.0 / (1600.0 * world_scale)` for correct UV tiling at any scale
  - `build_material_manifest(parsed_map) -> list[str]` — unique texture names
  - `extract_atmosphere(parsed_map) -> dict` — worldspawn properties
- Data shapes: `ColmeshTriangle = tuple[int,int,int,int,int]` (i0,i1,i2,material_flags,face_id).
- Integration points: Imported by ALL baker scripts. Imports polygon clipping and UV from `bake_map.py`. Does NOT import from `colmesh_bake.py` or `normalize_og_map.py`.
- Error paths: Malformed .map → raise `ParseError` with line number. Unknown class → log warning, skip entity.

##### tools/bake_map.py
- What changes: Verify `compute_uv` is importable. If it's a nested function inside `bake_map()` or references module-level state, extract it. No other changes.
- Function(s): No new functions. Verify signature: `compute_uv(point: Vec3, face_def: FaceDef) -> Tuple[float,float]`.
- Integration points: Imported by `ogmap_lib.py`.

#### Verification
- Run: `python3 -c "from tools.ogmap_lib import *; m = parse_map('assets/og_converted/maps/1-1.map'); print(f'{len(m.entities)} entities, {len(m.textures)} textures')"`
- Check: `56 entities, 6 textures`
- Run: `python3 -c "from tools.ogmap_lib import *; m = parse_map('...'); [print(f'{e.classname} -> {classify_entity(e)}') for e in m.entities[:10]]"`
- Check: worldspawn → ClassDef(SOLID, NONE, STATIC_MESH, NONE); SpikeBlock → ClassDef(DEATH, NONE, STATIC_MESH, UPWARD_ONLY); etc.

### Inc 2 — Collision baker: colmesh with quantized BVH (M)
**Depends on:** Inc 1
**Unblocks:** 7
**Done criteria:** `bake_colmesh.py` produces a `.colmesh` where BVH node AABBs are in quantized int16 space, the runtime's sphere sweeps detect surfaces, and the player doesn't fall through the world.

#### Files to touch

##### tools/bake_colmesh.py (NEW)
- What changes: New script. Reads `ParsedMap` from library, quantizes vertices first, builds BVH from quantized positions, writes .colmesh.
- Function(s):
  - `build_colmesh_triangles(parsed_map, scale) -> list[Triangle]` — for each brush entity, generate triangles with material flags from class registry. Skip VISUAL_ONLY/TKIGGER entities. Apply face_filter. Use `compute_face_polygon` and `fan_triangulate` from library.
  - `quantize_vertices(world_verts) -> tuple[list[int16_triple], float, Vec3]` — compute AABB, quant_scale, quant_origin, quantize all positions to int16.
  - `build_bvh_quantized(triangles: list[ColmeshTriangle], qverts: list[int16_triple], max_tris=4, max_depth=30) -> tuple[list[BvhNode], list[ColmeshTriangle]]` — **build BVH from quantized int16 positions**, NOT world-space floats. This is the critical fix from `bake_og.py`. Returns both the BVH node list AND a **reordered triangle array** where triangles are in depth-first leaf order. Leaf nodes index into this reordered array with contiguous ranges (`left_or_first` = first triangle, `count_or_zero` = N). All `face_id` fields in the reordered triangles are updated to match their new array index. This matches `colmesh_bake.py`'s approach exactly. Each node stores quantized AABB min/max (int16 values in [-32767, 32767]).
  - `write_colmesh(path, qverts, qaabb, scale, origin, triangles, bvh, slinks)` — big-endian binary writer.
  - `main()` — CLI: `python3 tools/bake_colmesh.py <in.map> --out <out.colmesh> --scale 0.15`
- Integration points: Imports `ogmap_lib`, `struct`. No dependency on `colmesh_bake.py`.

#### Edge cases
- Brush with all faces filtered → skip with warning
- Zero triangles generated → error, no output
- Colmesh > 256KB → warn but still write (budget is advisory)

#### Verification
- Run: `python3 tools/bake_colmesh.py assets/og_converted/maps/1-1.map --out /tmp/test.colmesh --scale 0.15`
- Check: BVH node AABBs are in signed int16 range [-32767, 32767], not world-space floats
- Check: `python3 -c "open('/tmp/test.colmesh','rb').read()[:4]"` → `b'CMSH'`
- Check: triangle count ≥ 200 for 1-1
- ROM test: player stands on geometry, doesn't fall through

### Inc 3 — LVL baker: faces with UVs + entities + atmosphere (M)
**Depends on:** Inc 1
**Unblocks:** 7
**Done criteria:** `bake_lvl.py` produces a `.lvl` file that LevelRenderer loads. Faces have correct UV coordinates (not all zero). Entity spawns (PlayerSpawn, Strawberry, Cassette) are present. Atmosphere properties (skybox, music, snow) are in the LVL header.

#### Files to touch

##### tools/bake_lvl.py (NEW)
- What changes: New script. Reads `ParsedMap`, emits LVL2 binary with per-vertex UVs, entity spawns, and atmosphere data.
- Function(s):
  - `build_lvl_faces(parsed_map, manifest, scale) -> LvlFile` — iterates brush entities. For each face: clip polygon, sort CCW with **Quake-space normal** (NOT game-space), dedupe, **compute UVs on Quake-space points before transform**, then transform to game space for vertex positions. Sets face flags from class registry render_mode.
  - `build_entity_spawns(parsed_map, scale) -> list[LvlEntity]` — for entities with EntityClass != NONE (and EntityClass < 100 to avoid future-only IDs like TrafficBlock), create LvlEntity with entity_id and transformed position.
  - `apply_atmosphere(lvl, parsed_map)` — extract skybox/music/ambience/snow from worldspawn, set on LvlFile.
  - `main()` — CLI: `python3 tools/bake_lvl.py <in.map> --out <out.lvl> --manifest <out.manifest> --scale 0.15 [--check-sprites]`
- Integration points: Imports `ogmap_lib`, `lvl_format`. Uses `compute_uv` from library for UV projection. The `--check-sprites` flag verifies each manifest texture has a corresponding `filesystem/tex/<name>.sprite` file and reports any missing ones.

#### Edge cases
- Same texture used by solid and visual-only faces → same material_id, differentiated by face flags
- Entity without origin → skip with warning
- Worldspawn without atmosphere keys → use defaults (empty strings, zero snow)

#### Verification
- Run: `python3 tools/bake_lvl.py assets/og_converted/maps/1-1.map --out /tmp/test.lvl --manifest /tmp/test.manifest --scale 0.15`
- Check: `duplicate_vertex_faces=0`, `reversed_winding_faces=0`, `first_fan_degenerate_faces=0`
- Check: manifest has clean names (no `_death` suffixes)
- Check: entity_count == 3 (PlayerSpawn, Strawberry, Cassette)
- Check: at least one vertex has non-zero UV (verify via Python inspection)

### Inc 4 — T3DM baker: visual mesh with UVs (M)
**Depends on:** Inc 1
**Unblocks:** 7
**Done criteria:** `bake_t3dm.py` produces a `.t3dm` file loadable by `t3d_model_load()`. The model has correct vertex positions, UVs, and material groupings matching the manifest.

#### Files to touch

##### tools/bake_t3dm.py (NEW)
- What changes: New script. Generates a `.t3dm` binary model from brush geometry with per-vertex UVs.
- Function(s):
  - `build_t3dm_geometry(parsed_map, manifest, scale) -> T3dmData` — collects all STATIC_MESH faces, fans into triangles, computes per-vertex UVs, groups by material_id, builds vertex/normal/index arrays.
  - `write_t3dm(path, data)` — writes the tiny3d `.t3dm` binary format. Uses `struct.pack` for the binary header + vertex chunks + index chunks + material chunks. Format reference: tiny3d's `t3d_model_load()`.
  - `main()` — CLI: `python3 tools/bake_t3dm.py <in.map> --out <out.t3dm> --manifest <in.manifest> --scale 0.15`
- Integration points: Imports `ogmap_lib`. Produces file that `t3d_model_load()` consumes.

#### Edge cases
- `.t3dm` vertex count limit (uint16 index range) — verify 1-1 stays under 65535
- Material count limit — verify 1-1 manifest size is within tiny3d's material slot budget
- No solid faces in map → produce minimal valid .t3dm (empty mesh, zero vertices)

#### Verification
- Run: `python3 tools/bake_t3dm.py assets/og_converted/maps/1-1.map --out /tmp/test.t3dm --manifest /tmp/test.manifest --scale 0.15`
- Check: file is non-zero and starts with expected magic bytes
- Check: `python3 -c "import struct; d=open('/tmp/test.t3dm','rb').read(); print(len(d))"` — reasonable size (~50-200KB)
- ROM test: level renders with textured faces (not flat colors)

### Inc 5 — Nav baker: TrafficBlock path data (S)
**Depends on:** Inc 1
**Unblocks:** none
**Done criteria:** `bake_nav.py` produces a `.nav` file with TrafficBlock → Node path waypoints in game-space coordinates.

#### Files to touch

##### tools/bake_nav.py (NEW)
- What changes: New script. Extracts TrafficBlock path data from parsed map.
- Function(s):
  - `extract_paths(parsed_map, scale) -> dict` — for each TrafficBlock: find linked Node via target/targetname, compute brush center, create waypoint pair (brush_center, node_origin) in game space.
  - `write_nav(paths, out_path)` — binary .nav file: magic `NAV1`, **little-endian** (host-native for the Python baker; the future N64 runtime will need byteswap). Format: uint16 count, per-entry: uint16 entity_index, uint16 waypoint_count, float travel_time, float x/y/z waypoints.
  - `main()` — CLI: `python3 tools/bake_nav.py <in.map> --out <out.nav> --scale 0.15`
- Integration points: Imports `ogmap_lib`. Produces future-ready artifact; not loaded by runtime yet.

#### Verification
- Run: `python3 tools/bake_nav.py assets/og_converted/maps/1-1.map --out /tmp/test.nav --scale 0.15`
- Check: platform_count == 5, each has 2 waypoints, all waypoints are finite floats
- Check: waypoint Z coordinates match expected traversal range (e.g., -93.6 to -410.4)

### Inc 6 — Pipeline integration: Makefile + ROM build (S)
**Depends on:** Inc 2, 3, 4
**Unblocks:** 7
**Done criteria:** `./compile-rom.sh` builds a ROM that loads 1-1 with working collision, textured visuals, correct entity spawns, and atmosphere.

#### Files to touch

##### Makefile
- What changes: Replace the `bake_og.py` rule with the new pipeline. The 1-1 targets depend on the source .map and all baker scripts + library.
- Integration points:
  ```makefile
  filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest filesystem/lvl/1-1.colmesh filesystem/lvl/1-1.t3dm: \
      assets/og_converted/maps/1-1.map \
      tools/ogmap_lib.py \
      tools/bake_colmesh.py \
      tools/bake_lvl.py \
      tools/bake_t3dm.py \
      tools/bake_nav.py \
      tools/bake_map.py \
      tools/lvl_format.py \
      tools/entity_ids.py | filesystem/lvl
      python3 tools/bake_colmesh.py $< --out filesystem/lvl/1-1.colmesh --scale 0.15
      python3 tools/bake_lvl.py $< --out filesystem/lvl/1-1.lvl --manifest filesystem/lvl/1-1.manifest --scale 0.15
      python3 tools/bake_t3dm.py $< --out filesystem/lvl/1-1.t3dm --manifest filesystem/lvl/1-1.manifest --scale 0.15
      python3 tools/bake_nav.py $< --out filesystem/lvl/1-1.nav --scale 0.15
  ```
  Add `filesystem/lvl/1-1.t3dm` to `DFS_MDL_FILES`.

##### Delete
- `tools/bake_og.py` — no longer used; remove from build

#### Verification
- Run: `make clean && ./compile-rom.sh`
- Check: build output shows all 4 baker scripts running
- Check: colmesh, lvl, manifest, t3dm, nav all produced
- ROM test: player spawns on geometry, moves/jumps/dashes, doesn't fall through
- ROM test: surfaces are textured (not flat colors)
- ROM test: touching spikes kills the player

### Inc 7 — Validation suite (M)
**Depends on:** Inc 6
**Unblocks:** none
**Done criteria:** Automated tests verify all artifacts meet spec. Colmesh BVH is quantized. LVL has UVs. T3DM is valid. Nav has correct waypoints.

#### Files to touch

##### tests/bake_pipeline_smoke.py (NEW)
- What changes: New test suite replacing `tests/bake_og_smoke.py`. Tests each baker independently.
- Function(s):
  - `test_library_parse()` — parse 1-1.map via ogmap_lib, assert entity/texture counts
  - `test_class_registry()` — verify all OG classnames in 1-1.map have a ClassDef or a SKIPPED reason
  - `test_colmesh_bvh_quantized()` — bake colmesh, parse binary, verify BVH node AABB values are in [0, 32767] range (quantized), not arbitrary floats
  - `test_colmesh_material_flags()` — verify: at least 10 triangles with MAT_SOLID only (no DEATH), at least 1 with MAT_DEATH, all DEATH triangles also have MAT_SOLID, no triangle with impossible combos (DEATH+CLIMBABLE, CLIMBABLE+DEATH)
  - `test_lvl_has_uvs()` — bake lvl, parse binary, verify at least one vertex has non-zero UV
  - `test_lvl_atmosphere()` — verify skybox/music/ambience/snow fields in LVL header are populated (not all zero)
  - `test_t3dm_valid()` — bake t3dm, verify file starts with expected magic, parse header, check vertex/face counts
  - `test_nav_waypoints()` — verify 5 platforms, 2 waypoints each, finite coordinates
  - `test_manifest_clean()` — verify no `_death` suffixes in manifest
  - `test_budgets()` — verify colmesh ≤ 256KB, lvl faces ≤ 1024, lvl vertices ≤ 8192

##### tests/bake_og_smoke.py
- What changes: Delete (replaced by bake_pipeline_smoke.py).

#### Verification
- Run: `python3 tests/bake_pipeline_smoke.py` — all tests pass
- Run: `python3 tests/level_bake_report_smoke.py` — updated for new pipeline

## Cross-cutting verification

After all increments:
1. ROM boots and plays: player stands on geometry, doesn't fall through
2. Death surfaces kill: touching spikes triggers respawn
3. Visuals are textured: faces show sprite textures, not flat colors
4. Entity spawns work: strawberry is collectible, cassette is present
5. Atmosphere works: skybox renders, music plays, snow falls
6. No `_death` suffixes: manifest contains clean texture names
7. BVH is quantized: colmesh BVH AABBs are in signed int16 range [-32767, 32767]
8. All artifacts produced: .colmesh + .lvl + .manifest + .t3dm + .nav

## Standards / common-mistakes referenced

- `.agents/map-creation.md` — applies to: artifact pipeline, material manifest, brush-class policy
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: face polygon vertex ordering
- `docs/colmesh_format.md` — applies to: binary format, BVH layout, quantization
- `docs/room_artifact_contract.md` — applies to: artifact separation, brush-class policy
- `docs/first-room-brief.md` — applies to: budget limits, material suffix contract (superseded by class system)

## Open questions (CONSIDER from review)

- **`bake_map.py` split-brain risk.** `ogmap_lib.py` imports from `bake_map.py`, but `bake_map.py` also contains its own `STATIC_SHELL_CLASSES` and entity dispatch logic that will conflict with the new class system. Consider eventually extracting only the pure functions (`compute_face_polygon`, `sort_vertices_ccw`, `dedupe_polygon_vertices`, `compute_uv`) into a `map_geometry.py` utility so `bake_map.py` can eventually be deprecated. Not blocking for Milestone 1.

- **Parallel Inc 2/3/4 bottleneck on `compute_uv` extraction.** If the library imports `compute_uv` from `bake_map.py` but the function needs modification (scale propagation, import cleanup), that extraction work blocks all three artifact bakers. Consider an explicit pre-increment (Inc 0.5) that extracts and tests all `bake_map.py` dependencies before the parallel bakers start. The implementer can decide whether this is needed based on the extraction complexity.

- **BVH leaf layout: contiguous ranges vs first-index.** The reference `colmesh_bake.py` uses contiguous leaf ranges (leaf node's `left_or_first` = first triangle, `count_or_zero` = N, all triangles in that leaf are consecutive). The old `bake_og.py` uses `indices[0]` as `left_or_first` (first triangle index in unsorted list). The plan should use the contiguous-range approach because it matches the colmesh format spec's traversal pseudocode and the working `colmesh_bake.py` reference.

- **`TrafficBlock` EntityClass ID 100 exceeds entity_ids.py range.** The runtime's `entity_ids.hpp` only defines IDs 0–9. ID 100 for TrafficBlock won't be recognized. Since `.nav` is future-ready and runtime moving-platform support is out of scope, TrafficBlock should either NOT emit a LVL entity spawn at all (only produce `.nav` data from a separate `bake_nav.py`), or the spawn should use a dummy ID (e.g., -1) that the loader ignores.

- **`compute_uv`'s `tex_per_unit` hardcoded at 0.003125 (1/320).** At `scale=0.15` vs the default `0.2`, UV tiling rates differ. The library's wrapper addresses this by computing `1.0 / (1600.0 * world_scale)`, but this hasn't been visually validated. If textures look over-tiled or under-tiled, try adjusting the formula. The implementer can compare against the old pipeline's UV output for validation.

- **Inc 7 Makefile uses `$<` (first prerequisite) for all bakers.** This works because the source `.map` is first. But the hardcoded `filesystem/lvl/1-1.manifest` argument to `bake_t3dm.py` won't scale to other maps. Acceptable for Milestone 1; refactor when adding map-2.

## Out of scope

- Runtime moving-platform support (TrafficBlock stays static solid; .nav is future-ready)
- Runtime ice physics (MAT_ICE defined but unchecked)
- Runtime one-way platform support (MAT_ONEWAY defined but unchecked)
- .t3dm-based rendering in LevelRenderer (the .lvl face path is still used; .t3dm is generated for future use)
- StaticProp .glb model instancing
- Maps beyond 1-1 (architecture designed for all 12, but only 1-1 implemented)
- Coin/Feather/SignPost runtime support
- NPC entities (Granny, Theo, Badeline)
