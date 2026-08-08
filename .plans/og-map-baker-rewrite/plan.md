# Plan: OG Map Baker Rewrite — Direct .map → .colmesh + .lvl

## Context

The current pipeline (`normalize_og_map.py` → `bake_map.py` → `colmesh_bake.py`) works but is a layered band-aid: a normalization pass patches OG entity classes into our `func_wall`+suffix convention, a general-purpose baker processes the normalized map, and a separate tool generates the collision mesh. The chain loses entity semantics, has no visual mesh path, and can't handle moving-platform metadata.

This plan replaces ALL three tools with a single baker (`bake_og.py`) that reads OG .map files directly, understands OG entity classes natively, and produces both collision and visual artifacts in one invocation. No normalization pass. No intermediate format gymnastics. Entity classes are first-class.

**Scope**: 1-1 first, architecture designed for all 12 OG maps. Visual+gameplay parity: both the collision mesh and the visible geometry must match the original.

## Architectural decisions

- **Decision: Single unified baker (`tools/bake_og.py`).** Replaces `normalize_og_map.py`, `bake_map.py`, and `colmesh_bake.py` with one tool that reads OG .map and writes `.lvl` + `.colmesh`. Rationale: eliminates the normalization pass (entity semantics handled natively), reduces 3 tools to 1, produces consistent face IDs between .lvl and .colmesh. The tool is ~400 lines (smaller than the sum of the 3 tools it replaces) because it targets a single job: convert OG Quake maps to our binary artifacts. Alternatives rejected: (a) keeping the normalize+bake chain with fixes — still fragile, still loses semantics; (b) runtime .map loader — too heavy for N64.

- **Decision: OG entity classes are understood natively.** SpikeBlock produces `MAT_SOLID | MAT_DEATH` faces. DeathBlock produces `MAT_SOLID | MAT_DEATH` faces. TrafficBlock produces `MAT_SOLID` faces + optional path metadata. Decoration produces visual-only faces. No `func_wall` translation layer. Each class maps to a `FaceClass` enum with `{solid, death, climbable, visual_only, trigger}`. Rationale: one source of truth; adding a new OG class is adding one enum variant + one mapping entry.

- **Decision: .lvl faces and .colmesh triangles are independently indexed.** The `.lvl` format stores n-gon faces with `vertex_start`/`vertex_count` (no `face_id` field). The `.colmesh` stores triangles each with `face_id == triangle_array_index` (a hard invariant of the colmesh format, not a cross-artifact link). The runtime's `FaceIsDeath`/`FaceIsClimbable` check `coll_mesh->triangles[face_id].material` where `face_id` comes from the `GroundHit`/`WallHit` query results — those `face_id` values are colmesh triangle indices. There is no requirement that .lvl faces and .colmesh triangles share IDs. The baker generates both artifacts from a single shared triangulation pass so the results are geometrically consistent, but their internal numbering is independent.

- **Decision: Face normals computed from brush plane equations, not polygon vertices.** OG brushes are convex hulls defined by plane equations. Each face normal is the plane normal, transformed to game space. This avoids the polygon-winding bugs documented in `og-map-polygon-winding.md`. The existing `bake_map.py` computes normals from clipped polygon vertices, which introduces sign ambiguity for non-axis-aligned faces.

- **Decision: SpikeBlock geometry emitted only for upward-facing faces.** SpikeBlock brushes in OG maps are thin slabs that overlap worldspawn geometry. Emitting all faces would create death surfaces inside solid walls. The baker filters SpikeBlock brush faces to only those whose game-space normal has `y > 0.3` (upward). Rationale: matches OG gameplay — you die landing ON spikes, not brushing against them. The threshold of 0.3 ≈ cos(72.5°) excludes walls and ceilings. This is the ONE case where face filtering is needed because SpikeBlock geometry overlaps worldspawn; all other entity classes emit all faces.

- **Decision: TrafficBlock baked as static solid with optional path metadata sidecar.** TrafficBlock brushes (1×1×1 degenerate in OG) are expanded to their actual traversal volume by reading the Node pathfinding entities and computing the moving platform's swept bounds. The baker emits these as `MAT_SOLID` faces with `owner_id` linking to a `MovingSurface` entry. Path data (Node positions, timing) is written to a `.nav` sidecar file that the runtime can load when moving-platform support is added. Until then, TrafficBlock geometry is static solid — same as current behavior but with correct face positions (not degenerate 1-unit boxes).

- **Decision: .lvl format kept for entity spawns + visual geometry.** The runtime already has a working LevelRenderer that consumes .lvl face data. Rather than inventing a new render format, the baker emits .lvl with correct per-face flags and material assignments. This gives us visual rendering for free. A future increment can add .t3dm output if the sprite-based .lvl rendering becomes insufficient for visual fidelity.

- **Decision: Rejected — per-face texture suffix encoding.** The current approach of appending `_death` to texture names to signal material flags is rejected. Instead, face material is determined by entity class (SpikeBlock = death, worldspawn = solid, etc.) and stored directly as a `face_class` enum. The `.lvl` manifest lists the actual texture names (no suffixes), and the `.colmesh` material flags encode the class-derived behavior. This means `floor_dirty_concrete` stays `floor_dirty_concrete` — no `_death` suffix anywhere.

## Assumptions and answers from code

- **Answered from code: .lvl binary format (version 2).** Source: `tools/lvl_format.py:1-120` — magic `"LVL2"`, header with vertex/face/collider/entity counts, string table for materials, binary sections. Face flags: `0x01` = solid, `0x02` = visual_only. The new baker writes this format directly.

- **Answered from code: .colmesh binary format.** Source: `docs/colmesh_format.md`, `src/user/gameplay/physics/coll_mesh.hpp:15-52` — magic `"CMSH"`, big-endian, header (72 bytes), quantized int16 vertices, uint16-indexed triangles with material/face_id fields, BVH tree (depth-first skip-pointer scheme), surface links for moving platforms.

- **Answered from code: Material flag bit definitions.** Source: `src/user/gameplay/physics/coll_mesh.hpp:57-61` — `MAT_SOLID=0x0001`, `MAT_ONEWAY=0x0002`, `MAT_DEATH=0x0004`, `MAT_CLIMBABLE=0x0008`, `MAT_ICE=0x0010`. The new baker sets these directly based on entity class, not texture suffix.

- **Answered from code: Coordinate transform.** Source: `tools/bake_map.py:418-427` — Quake `(x, y, z)` with Z-up → game `(x * scale, z * scale, -y * scale)`. Scale is `WORLD_SCALE = 0.2` by default; 1-1 uses `0.15`. Normal transform: `(nx, nz, -ny)` — rotation only, scale-free. The new baker uses the same transforms.

- **Answered from code: Entity ID mapping.** Source: `tools/entity_ids.py` — PlayerSpawn=0, Strawberry=1, Refill=2, Spring=3, Cassette=9. These IDs must stay consistent because `level_loader.cpp` and `entity_dispatch.cpp` hardcode them.

- **Answered from code: Runtime collision query flow.** Source: `coll_mesh.cpp` — `SweepSphereMesh` skips non-`MAT_SOLID` triangles; `RaycastMesh` hits everything. `FaceIsDeath`/`FaceIsClimbable` check material flags on `coll_mesh->triangles[face_id].material`. The baker must set `MAT_SOLID` on all collision-relevant faces (including death surfaces) so sphere sweeps detect them.

- **Answered from code: LevelRenderer consumes .lvl face data.** Source: `level_renderer.cpp` — reads `LevelGeometry` (vertices + faces from .lvl), groups by material_id, draws with sprite textures. The new baker's .lvl output must include face geometry with correct material indices for rendering to work.

- **Answer from code: 1-1 uses `WORLD_SCALE = 0.15`.** Source: `tools/bake_map.py` line at bottom calls `bake_map(..., world_scale=0.15)` for 1-1. Confirmed by file inspection — the first-room uses default 0.20. The baker must accept a scale parameter.

- **Assumption: Polygon clipping can be imported from `bake_map.py`.** The new baker imports `clip_polygon_by_plane()`, `compute_face_polygon()`, `sort_vertices_ccw()`, and `dedupe_polygon_vertices()` from `tools/bake_map.py`. These are the ~200 lines of well-tested brush→polygon code. The baker does NOT import from `colmesh_bake.py` (that tool reads `.lvl` as input and its triangulation is coupled to the `LvlFile` object).

- **Decision: The new baker implements its own fan triangulation locally.** `colmesh_bake.py`'s `triangulate_lvl()` is tightly coupled to the `.lvl` `LvlFile` structure (`.faces`, `.vertices`, `.strings`) and cannot accept raw polygon vertex lists. The new baker implements `fan_triangulate(verts: list[Vec3]) -> list[tuple[int,int,int]]` directly — a ~30 line function. The `build_bvh()` and `write_colmesh()` functions in `colmesh_bake.py` are also coupled to the `LvlFile` flow, so the new baker reimplements BVH construction and binary colmesh writing locally (~100 lines). This avoids depending on `colmesh_bake.py` at all, making the new baker fully self-contained. The baked `.colmesh` format is identical — the runtime doesn't know the difference.

- **Assumption: The .lvl format's face flags are sufficient for visual-only vs solid distinction.** `0x01` = solid (appears in both .lvl render data and .colmesh), `0x02` = visual-only (only in .lvl, not in .colmesh). This is already what the current LevelRenderer uses.

- **Note: Vertex budget discrepancy between docs and runtime.** `docs/first-room-brief.md` states `.lvl` vertex budget ≤ 4096. The runtime `LevelGeometry::kMaxVertices` is 8192. The baker uses the runtime limit (8192) as the authoritative constraint. The stale doc should be updated separately.

## Risks accepted

- **Risk: Moving platform geometry may be incorrect.** TrafficBlock brushes in OG are 1×1×1 degenerate boxes — the visual size comes from Node path data. The baker must expand these to their swept traversal volume. If Node data is sparse or incorrect, the expanded bounds may be wrong. Mitigation: start with TrafficBlock as a 1×1×1 box (preserves current behavior), add path expansion in a follow-up increment.

- **Risk: Face normal computation from plane equations may differ from polygon-based normals.** The OG .map format stores face planes as `(p1, p2, p3, texture, u, v, rot, scaleX, scaleY)`. The plane normal is `cross(p2-p1, p3-p1)`. If these three points are collinear or nearly collinear, the normal is degenerate. Mitigation: validate normal length > 1e-6; if degenerate, compute from the brush's other faces via plane intersection.

- **Risk: The new baker may produce different triangle counts than the current pipeline.** Polygon clipping generates fewer or more vertices depending on brush complexity and face count. For standard rectangular brushes (worldspawn, SpikeBlock), the result should be identical. For complex brushes, differences are acceptable as long as collision is correct.

- **Risk: .lvl face geometry must match .colmesh triangle geometry for face_id consistency.** If the visual mesh (.lvl faces) is triangulated differently than the collision mesh (.colmesh triangles), face_id won't match and FaceIsDeath/FaceIsClimbable will check the wrong triangle. Mitigation: the baker triangulates once (using colmesh_bake's fan triangulation) and assigns face_ids from that triangulation; the same face_ids are written to both .lvl and .colmesh.

- **Risk: The visual .lvl path uses per-face sprite textures — visual fidelity may not match OG.** The current LevelRenderer draws each face with its assigned sprite texture at the face's vertex positions. This produces faceted, unlit, flat-shaded faces. For a "faithful" visual recreation, this may look crude compared to OG's smooth terrain. Mitigation: accept this as good enough for milestone; .t3dm-based rendering with smooth normals is a separate plan.

## Increment DAG

- Inc 1 — OG .map parser (S) — depends on: none — unblocks: 2, 4
- Inc 2 — Direct colmesh baker (M) — depends on: 1 — unblocks: 3
- Inc 3 — LVL output (faces + entities) (M) — depends on: 2 — unblocks: 5
- Inc 4 — Navigation sidecar & TrafficBlock (M) — depends on: 1 — unblocks: none
- Inc 5 — Pipeline integration & ROM build (S) — depends on: 2, 3 — unblocks: 6
- Inc 6 — Validation suite (M) — depends on: 5 — unblocks: none

Inc 2 and Inc 4 can run in parallel after Inc 1. Inc 3 depends on Inc 2 because polygon clipping and triangulation infrastructure established in Inc 2 is reused by Inc 3's LVL face output.

## Increments

### Inc 1 — OG .map parser (S)
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** `bake_og.py` parses `assets/og_converted/maps/1-1.map` and prints entity class counts, brush counts, texture names, and coordinate ranges.

#### Files to touch

##### tools/bake_og.py (NEW)
- What changes: New standalone tool. Parses OG Quake .map format into a structured representation. No baking yet — just parse + report.
- Function(s):
  - `parse_map(path) -> MapData` — reads .map file, returns list of entities each with classname, origin, properties dict, brushes list. Each brush = list of faces; each face = 3 point coords + texture name + uv/shift/rot/scale.
  - `report(map_data) -> str` — prints entity class counts, world coordinate ranges, unique texture names, brush counts per entity.
  - `main()` — CLI: `python3 tools/bake_og.py --report <in.map>` (or positional args)
- Data shapes:
  ```python
  MapData = list[Entity]
  Entity = {classname, origin, properties, brushes}
  Brush = list[Face]
  Face = {p1, p2, p3, texture, u, v, rot, scale_x, scale_y}
  ```
- Integration points: Imports nothing from existing tools. Standalone parser. Direct file I/O.
- Error paths: Malformed .map (missing braces, unclosed entities) → print error with line number and exit(1). Empty brushes (0 faces) → warn and skip. Textures with non-ASCII names → warn and use as-is.

#### Edge cases
- .map files may use tabs or spaces for indentation — parser treats all whitespace equivalently
- Texture names may contain path separators or special characters — store as raw string
- Entities with no brushes (point entities like PlayerSpawn) — store with empty brush list
- Entities with no classname — assign classname `"unknown"` and warn

#### Verification
- Run: `python3 tools/bake_og.py --report assets/og_converted/maps/1-1.map`
- Check: output shows `worldspawn: 13 brushes`, `SpikeBlock: 6 brushes`, `TrafficBlock: 5 brushes`, `DeathBlock: 1 brush`, `Decoration: 23 brushes`
- Check: coordinate ranges printed match known values (X: -960..384, Y: -128..4544, Z: 240..576 in Quake coords)
- Check: unique textures include `rock_1`, `snow_1`, `rock_2`, `metal_floor_1`, `floor_dirty_concrete`, `TB_empty`

### Inc 2 — Direct colmesh baker (M)
**Depends on:** Inc 1
**Unblocks:** none (parallel with Inc 3)
**Done criteria:** Running `bake_og.py` on 1-1.map produces a valid `.colmesh` file. The colmesh has MAT_SOLID triangles for worldspawn, MAT_SOLID|MAT_DEATH for SpikeBlock/DeathBlock, and no triangles for Decoration. Face IDs are sequential and consistent.

#### Files to touch

##### tools/bake_og.py (UPDATE)
- What changes: Add `FaceClass` enum, entity-class → face-class mapping, brush face → triangle generation, colmesh output. Imports polygon clipping from `bake_map.py`; implements fan triangulation, BVH construction, and colmesh writing locally.
- Function(s):
  - `FaceClass` enum: `SOLID=0, DEATH=1, CLIMBABLE=2, VISUAL_ONLY=3, TRIGGER=4`
  - `CLASS_FACE_MAP: dict[str, FaceClass]` — maps OG classname → face class. For DeathBlock specifically, the filter is applied (see `is_upward_face`). The complete mapping, designed for all 12 OG maps:
    ```python
    {
        "worldspawn":              FaceClass.SOLID,
        "SpikeBlock":              FaceClass.DEATH,
        "DeathBlock":              FaceClass.DEATH,
        "TrafficBlock":            FaceClass.SOLID,
        "FallingBlock":            FaceClass.SOLID,
        "FloatyBlock":             FaceClass.SOLID,
        "GateBlock":               FaceClass.SOLID,
        "MovingBlock":             FaceClass.SOLID,
        "CassetteBlock":           FaceClass.SOLID,
        "BreakBlock":              FaceClass.SOLID,
        "DoubleDashPuzzleBlock":   FaceClass.SOLID,
        "Decoration":              FaceClass.VISUAL_ONLY,
        "FloatingDecoration":      FaceClass.VISUAL_ONLY,
    }
    ```
  - `SKIPPED_CLASSES: dict[str, str]` — classes explicitly skipped with reasons (Node, func_group, StaticProp, Coin, Feather, SignPost, IntroCar, Granny, Theo, Badeline, Chimney, FixedCamera, EndingArea). Unknown classes trigger a warning and are skipped.
  - `face_class_to_material(fc: FaceClass) -> int` — returns `MAT_SOLID | MAT_DEATH`, `MAT_SOLID`, `MAT_SOLID | MAT_CLIMBABLE`, `0`, etc.
  - `is_upward_face(face: Face) -> bool` — compute game-space normal from face plane points, return `normal.y > 0.3`. Used only for SpikeBlock AND DeathBlock face filtering (these entity classes overlap worldspawn geometry; emitting all faces would create death volumes inside walls). All other entity classes emit all faces unfiltered.
  - `brush_faces_to_triangles(brush, face_class, world_scale) -> list[Triangle]` — for each face in brush: compute plane normal, skip if face_class==DEATH and not upward, clip polygon using `compute_face_polygon()` imported from `bake_map.py`, fan-triangulate locally, emit triangles with `material = face_class_to_material(face_class)`.
  - `fan_triangulate(verts: list[Vec3]) -> list[tuple[int,int,int]]` — fan from verts[0]: `(0, 1, 2), (0, 2, 3), ...` for n vertices. Local implementation (~10 lines).
  - `quantize_positions(positions: list[Vec3]) -> tuple[list[int16_triple], float, Vec3]` — compute quant scale/origin from AABB, quantize all positions. Local implementation (~25 lines).
  - `build_bvh(triangles, vertices) -> list[BvhNode]` — top-down median-split BVH, depth ≤ 30, max 4 tris per leaf. Local implementation (~80 lines).
  - `write_colmesh(path, header, vertices, triangles, bvh_nodes, surface_links)` — write big-endian binary per `colmesh_format.md`. Local implementation (~40 lines).
  - `main()` — CLI: `python3 tools/bake_og.py <in.map> --colmesh <out.colmesh> [--scale 0.15]`
- Data shapes:
  ```python
  Triangle = namedtuple('Triangle', 'i0 i1 i2 material face_id')
  ```
- Integration points: Imports `compute_face_polygon`, `sort_vertices_ccw`, `dedupe_polygon_vertices` from `tools/bake_map.py` (the existing well-tested ~200 lines of polygon clipping). Does NOT import from `colmesh_bake.py` or `normalize_og_map.py`.
- Error paths: Brush with all faces filtered out → warn `"entity class=X had no surviving faces"` and skip. Polygon clipping produces < 3 vertices → skip face. Face normal is degenerate (length < 1e-6) → warn and skip face. Colmesh budget exceeded (>256KB) → error and abort.

#### Edge cases
- Brushes with non-planar faces (OG Quake convention: first 3 points define plane, subsequent points must be coplanar) — use only p1,p2,p3 for normal; clip against all brush planes to get actual polygon vertices
- Texture names that colmesh doesn't care about — colmesh only stores material flags (uint16), not texture references
- Worldspawn brushes that form the outer shell — these are large and produce many triangles; BVH handles them efficiently

#### Verification
- Run: `python3 tools/bake_og.py assets/og_converted/maps/1-1.map --colmesh /tmp/1-1.colmesh --scale 0.15`
- Check: `python3 -c "import struct; d=open('/tmp/1-1.colmesh','rb').read(); assert d[:4]==b'CMSH'; print('OK')"` — magic valid
- Check: at least one triangle has `(material & 0x0004) != 0` (MAT_DEATH)
- Check: at least one triangle has `(material & 0x0001) != 0` (MAT_SOLID)
- Check: zero triangles have both MAT_DEATH and MAT_CLIMBABLE (mutually exclusive in this map)
- Check: triangle_count == face_count (each face produces ≥2 triangles via fan; total should be ≥ 200 for 1-1)

### Inc 3 — LVL output with face geometry and entities (M)
**Depends on:** Inc 2
**Unblocks:** 5
**Done criteria:** Running `bake_og.py` on 1-1.map produces a `.lvl` file that LevelRenderer can load. Faces have correct material assignments. Entity spawns (PlayerSpawn, Strawberry, Cassette) are present. The .lvl solid face count matches .colmesh triangle count (each n-gon → ≥2 triangles via fan).

#### Files to touch

##### tools/bake_og.py (UPDATE)
- What changes: Add `write_lvl()` function. For each brush face: compute clipped polygon vertices, assign material_id (from texture name → manifest index), write face with `flags = 0x01` (solid) or `0x02` (visual_only). Entity spawns: look up entity_id from classname, write to entity table.
- Function(s):
  - `build_material_manifest(entities) -> list[str]` — collect unique texture names across all brush faces, return ordered list. Deduplicate; preserve first-encountered order (deterministic).
  - `write_lvl(entities, world_scale, manifest, out_path)` — for each brush-bearing entity:
    - Compute game-space vertices for each face polygon
    - Deduplicate vertices (positional equality within 1e-5 tolerance)
    - Write vertex table (float x,y,z per vertex)
    - Write face table (vertex_count, vertex_start, material_id, flags per face)
    - Write collider table (one AABB collider per brush, for legacy compat — can be empty since colmesh handles collision)
    - Write entity table (entity_id, position for point entities)
    - Write string table (manifest)
  - `main()` — CLI: `python3 tools/bake_og.py <in.map> --lvl <out.lvl> [--manifest <out.manifest>] [--scale 0.15]`
- Data shapes: Binary LVL2 format per `lvl_format.py` spec. Big-endian, sections: header, vertices, faces, entities, colliders, strings.
- Integration points: Produces files that `level_loader.cpp` and `level_renderer.cpp` consume. Does NOT import from `bake_map.py`.
- Error paths: Vertex count exceeds 8192 → error and abort (runtime has `kMaxVertices=8192`). Face count exceeds 1024 → error and abort. Entity count exceeds 64 → error and abort. Face polygon clipping produces < 3 vertices → skip face and warn.

#### Edge cases
- Same texture name used by both solid and visual-only faces — material_id is the same; the face's `flags` field distinguishes them
- Entity without origin (shouldn't happen in valid .map) — skip with warning
- Worldspawn's `_tb_textures` and `_tb_def` keys — ignored (TrenchBroom metadata, not runtime data)
- Cassette entity — point entity, entity_id=9 per `entity_ids.py`; level_loader reads `room.cassette` from entity table

#### Verification
- Run: `python3 tools/bake_og.py assets/og_converted/maps/1-1.map --lvl /tmp/1-1.lvl --manifest /tmp/1-1.manifest --scale 0.15`
- Check: `/tmp/1-1.manifest` contains `rock_1`, `snow_1`, `rock_2`, `metal_floor_1`, `floor_dirty_concrete`, `TB_empty` (no `_death` suffixes!)
- Check: `/tmp/1-1.lvl` magic is `b"LVL2"`
- Check: entity_count ≥ 3 (PlayerSpawn, Strawberry, Cassette)
- Check: `python3 tools/level_bake_report.py /tmp/1-1.lvl` reports `duplicate_vertex_faces=0` and `reversed_winding_faces=0`

### Inc 4 — Navigation sidecar & TrafficBlock path data (M)
**Depends on:** Inc 1
**Unblocks:** none
**Done criteria:** Running `bake_og.py` on 1-1.map produces a `.nav` sidecar file. The .nav contains TrafficBlock platform definitions with path waypoints. TrafficBlock brushes are expanded from 1×1×1 degenerate to their swept path bounds and emitted as solid faces in .colmesh/.lvl.

#### Files to touch

##### tools/bake_og.py (UPDATE)
- What changes: Add `extract_traffic_paths()`, `expand_traffic_brush()`, `write_nav()`. TrafficBlock brush expansion hooks into the existing colmesh/LVL emission pipeline from Inc 2/3.
- Function(s):
  - `extract_traffic_paths(entities: list[Entity]) -> dict[int, TrafficPath]` — for each TrafficBlock entity: find its linked Node entities via `target`/`targetname` property matching, extract `origin` positions in travel order (sorted by distance from start to finish), transform to game space. Returns `{traffic_entity_index: TrafficPath}`.
  - `TrafficPath` data shape: `(entity_index: int, waypoints: list[Vec3], travel_time: float)`. Travel time defaults to 2.0 seconds (OG convention) if no `speed`/`wait` key present.
  - `expand_traffic_brush(brush: Brush, path: TrafficPath) -> Brush` — if brush is degenerate (all face pairs within 1 Quake unit in each axis), compute the swept AABB of all path waypoints and create a replacement brush covering that volume with the original brush's texture. Return expanded brush (or original if already non-degenerate).
  - `write_nav(paths: list[TrafficPath], world_scale: float, out_path: str)` — write binary `.nav` file:
    - Header: `uint16 platform_count`
    - Per platform: `uint16 entity_index` (links back to TrafficBlock entity), `uint16 waypoint_count`, `float travel_time`, then `float x,y,z` per waypoint (game-space). Little-endian.
  - `main()` — CLI: `python3 tools/bake_og.py <in.map> --nav <out.nav> [--scale 0.15]`
- Data shapes: `.nav` binary, ~300 bytes for 5 platforms with 3 waypoints each.
- Integration points: The expanded TrafficBlock brushes are fed into the same colmesh/LVL emission pipeline as Inc 2/3. The `.nav` is not loaded by runtime yet — it exists as a future-ready artifact.
- Error paths: TrafficBlock with no linked Nodes → warn `"TrafficBlock N has no path nodes, keeping degenerate brush"` and emit 1×1×1 as-is. Node with no `origin` key → skip node. Multiple Nodes with same `targetname` → sort by distance from first to last waypoint using nearest-neighbor ordering. No Node entities in map → all TrafficBlocks stay degenerate, logged as aggregate warning.

#### Edge cases
- TrafficBlock with non-degenerate brushes (unlikely in OG but possible in custom maps) — don't expand; use as-is but still extract path data
- No Node entities in the map — all TrafficBlocks stay degenerate (1×1×1), warning logged; `.nav` file still written (with platform_count = number of TrafficBlocks, waypoint_count = 0 for each)
- Multiple TrafficBlocks sharing Nodes — each TrafficBlock gets its own path copy; Nodes are not consumed
- TrafficBlock `target` property references a Node `targetname` — OG convention; simple string match

#### Verification
- Run: `python3 tools/bake_og.py assets/og_converted/maps/1-1.map --nav /tmp/1-1.nav --scale 0.15`
- Check: `/tmp/1-1.nav` exists and is non-zero
- Check: `python3 -c "import struct; d=open('/tmp/1-1.nav','rb').read(); pc=struct.unpack_from('<H',d,0)[0]; print(f'platforms={pc}')"` — platforms=5
- Check: each platform has ≥ 2 waypoints (verify by parsing the binary)
- Run colmesh bake with expanded TrafficBlock: traffic faces appear with `MAT_SOLID` only (no death)
- Check: TrafficBlock face positions in colmesh span realistic platform volumes (not 0.15-unit cubes)

### Inc 5 — Pipeline integration & ROM build (S)
**Depends on:** Inc 2, Inc 3
**Unblocks:** 6
**Done criteria:** `./compile-rom.sh` builds a ROM that loads 1-1 through the new baker. Player can move, jump, dash on the baked geometry. Death surfaces kill on contact. Visual geometry is rendered.

#### Files to touch

##### Makefile
- What changes: Replace the 1-1 bake rules with new `bake_og.py` invocation. Remove `normalize_og_map.py` dependency. Keep the `.colmesh` pattern rule (it still auto-generates from `.lvl` unless we decide otherwise — but we don't need it since bake_og.py produces .colmesh directly).
- Integration points:
  ```makefile
  # Old: normalize → bake_map → colmesh_bake (3 steps)
  # New: single bake_og.py invocation (1 step)
  filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest filesystem/lvl/1-1.colmesh: \
      assets/og_converted/maps/1-1.map tools/bake_og.py | filesystem/lvl
      python3 tools/bake_og.py $< \
          --lvl filesystem/lvl/1-1.lvl \
          --manifest filesystem/lvl/1-1.manifest \
          --colmesh filesystem/lvl/1-1.colmesh \
          --scale 0.15
  ```
  Remove the `build/1-1-norm.map` intermediate target. Remove any references to `tools/normalize_og_map.py` from the Makefile.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: No changes expected. `kBakedLevelPath` stays `"rom:/lvl/1-1.lvl"`. The new .lvl and .colmesh have the same filenames — the runtime doesn't know the baker changed.

##### src/user/gameplay/world/level_loader.cpp
- What changes: No changes expected. The .lvl format is unchanged — `LoadLevel` reads the same binary structure. The .colmesh format is unchanged — `LoadCollMesh` reads the same binary structure.

#### Edge cases
- The first build after the tool switch will regenerate all 1-1 artifacts — normal, expected
- If the new .lvl has different face_count than the old, LevelRenderer batch limits (kMaxBatches=1024) could overflow — verify face_count < 1024

#### Verification
- Run: `make clean && ./compile-rom.sh`
- Check: build output shows bake_og.py running (not normalize_og_map.py, not bake_map.py, not colmesh_bake.py)
- Check: `make -n filesystem/lvl/1-1.lvl` shows exactly one Python invocation
- Check: `ls -la filesystem/lvl/1-1.colmesh` — file exists and is > 0 bytes
- Check: ROM boots in emulator, player spawns on 1-1 geometry
- Check: player walks on solid surfaces, dies on spike surfaces
- Check: visual geometry renders correctly (walls, floor, decorations visible)

### Inc 6 — Validation suite (M)
**Depends on:** Inc 5
**Unblocks:** none
**Done criteria:** Automated tests verify the new baker produces correct colmesh material flags, correct .lvl entity spawns, and correct face geometry. A comparison script verifies the new baker's output against known-good references.

#### Files to touch

##### tests/bake_og_smoke.py (NEW)
- What changes: New smoke test for the unified baker. Concrete assertions against 1-1.map output.
- Function(s):
  - `test_parse_1_1()` — `parse_map("1-1.map")` → assert entity_count == 56, assert `worldspawn` brushes == 13, `SpikeBlock` == 7 (6 main + 1 ent 53), `TrafficBlock` == 5, `DeathBlock` == 1, `Decoration` == 23
  - `test_colmesh_material_flags()` — bake colmesh, parse binary:
    - Assert: at least 1 triangle with `(material & 0x0004) != 0` (MAT_DEATH set)
    - Assert: at least 10 triangles with `(material & 0x0001) != 0` (MAT_SOLID set)
    - Assert: zero triangles with material == 0 (no trigger-only triangles in 1-1)
    - Assert: all triangles with MAT_DEATH also have MAT_SOLID (death surfaces must be solid)
  - `test_colmesh_no_decoration_triangles()` — verify all triangles with material != 0 have `(material & MAT_SOLID)`. Decoration faces (visual-only) must NOT appear in colmesh.
  - `test_lvl_entity_spawns()` — bake .lvl, parse binary:
    - Assert: entity with entity_id=0 exists and has classname `PlayerSpawn`
    - Assert: entity with entity_id=1 exists (Strawberry)
    - Assert: entity with entity_id=9 exists (Cassette)
    - Assert: entity_count == 3 (no extraneous entity spawns — Refill, Spring not in 1-1)
  - `test_no_death_suffix_in_manifest()` — bake manifest, assert line list contains `floor_dirty_concrete` (not `floor_dirty_concrete_death`), contains `TB_empty` (not `TB_empty_death`), contains `metal_floor_1`, `rock_1`, `snow_1`, `rock_2`
  - `test_lvl_face_budgets()` — bake .lvl, assert face_count ≤ 1024, assert vertex_count ≤ 4096
  - `test_colmesh_budget()` — bake colmesh, assert file size ≤ 262144 (256 KB)
  - `test_nav_output()` — if .nav generated: assert platform_count == 5, each platform has waypoint_count ≥ 2, all waypoints are finite floats
- Integration points: Shells out to `bake_og.py` as subprocess. Parses .colmesh binary (struct.unpack big-endian, header + triangle records) and .lvl binary (LVL2 format).
- Error paths: bake_og.py crash → test fails with stderr output. Missing output file → test fails with clear message. Binary parse error → test fails with hex dump context.

##### tests/colmesh_smoke.py (UPDATE)
- What changes: Update existing colmesh tests to work with new baker output. The test currently expects material flags from suffix-based mapping — update to check for the same flags from class-based mapping.
- Function(s): Existing `test_colmesh_has_death_triangles()` and `test_colmesh_triangle_count()` — update expected material values if format changed.

##### tests/level_bake_report_smoke.py (UPDATE)
- What changes: Update to run against new baker's .lvl output. Check for `duplicate_vertex_faces=0`, `first_fan_degenerate_faces=0`, `reversed_winding_faces=0`.

#### Edge cases
- The old `tests/fixtures/1-1.manifest` is obsolete (it contains `_death` suffixes) — replace with new expected manifest
- Old colmesh smoke tests may reference `floor_dirty_concrete_death` — update to use clean texture names

#### Verification
- Run: `python3 tests/bake_og_smoke.py` — all tests pass
- Run: `python3 tests/colmesh_smoke.py` — all tests pass (with updated expectations)
- Run: `python3 tests/level_bake_report_smoke.py` — all tests pass

## Cross-cutting verification

After all increments:
1. **ROM boots and plays**: Player spawns, moves, jumps, dashes on 1-1 geometry
2. **Death surfaces kill**: Touching SpikeBlock/DeathBlock geometry triggers respawn
3. **Collision is correct**: No falling through floors, no walking through walls
4. **Visuals render**: Walls, floors, and decorations visible with correct textures
5. **Entity spawns work**: Strawberry is collectible, Cassette is present
6. **No normalizer**: `normalize_og_map.py` is not invoked during build
7. **No suffix hacks**: Manifest contains clean texture names (no `_death` suffixes)
8. **Scale is correct**: 1-1 uses 0.15, not 0.20

## Standards / common-mistakes referenced

- `.agents/map-creation.md` — applies to: .lvl format output, material manifest contract
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: face polygon vertex ordering, deduplication
- `docs/room_artifact_contract.md` — applies to: artifact separation (.lvl vs .colmesh vs .t3dm)
- `docs/colmesh_format.md` — applies to: binary format, BVH structure, material flags
- `docs/first-room-brief.md` — applies to: material suffix contract (superseded by class-based system), budget limits

## Open questions (CONSIDER from review)

- **1×1×1 TrafficBlock collision artifact risk.** TrafficBlock brushes are 1×1×1 Quake units (0.15 game units at scale 0.15). If baked as static solid without path expansion, players could fall into gaps between these tiny boxes and worldspawn geometry. Verify no traversal-blocking gaps exist after Inc 5 ROM build — if gaps exist, TrafficBlock expansion (Inc 4) must be completed before shipping 1-1.

- **FaceClass enum doesn't include ONEWAY or ICE.** The material flag system supports `MAT_ONEWAY` and `MAT_ICE` but no OG entity class maps to them. If future maps introduce one-way platforms or ice surfaces, the `FaceClass` enum needs extension. This is fine for 1-1 scope but worth noting for the "all 12 maps" design goal.

- **.lvl face flags limited to solid/visual_only — no death/ice/climb distinction for the renderer.** The `.lvl` format only has `0x01` (solid) and `0x02` (visual_only) face flags. The renderer can't distinguish death surfaces from normal surfaces visually (e.g., to tint spikes red). This is a pre-existing limitation; the `.colmesh` carries the full material flags for gameplay. If visual feedback for death surfaces is desired, it would need a renderer change or `.t3dm` path.

- **.lvl collider table is vestigial.** The runtime `level_loader.cpp` reads `collider_count` from LVL header but skips loading collider data — static collision is exclusively via `.colmesh`. The plan's `write_lvl()` in Inc 3 says "one AABB collider per brush, for legacy compat." Consider emitting zero colliders (`collider_count=0`) and removing the vestigial code — the runtime doesn't use them. If legacy compatibility matters, verify by checking the runtime path: `level_loader.cpp` line handling collider table.

- **baker ~400 line estimate doesn't account for polygon clipping import.** With clipping imported from `bake_map.py` (~200 lines reused) and new code for fan triangulation (~10), quantization (~25), BVH (~80), colmesh writing (~40), LVL writing (~60), entity parsing is ~50 (from Inc 1), the total is ~265 new lines + ~200 imported. Realistic total: ~450-500 lines. The 400-line estimate was slightly optimistic; 500 is more realistic.

- **Material catalog sprite availability.** The plan stores OG texture names directly in the manifest (e.g., `floor_dirty_concrete`, `TB_empty`). The runtime's `MaterialCatalog` loads sprites by material name: it needs `filesystem/tex/floor_dirty_concrete.sprite` and `filesystem/tex/TB_empty.sprite` to exist. These sprites must be present in the DFS for rendering to work. Verify asset availability during Inc 5 ROM build — if sprites are missing, the renderer will skip faces with null materials (graceful degradation, not a crash).

## Out of scope

- .t3dm visual mesh generation (the .lvl face path is sufficient for now)
- Runtime moving-platform support (TrafficBlock stays static solid)
- Runtime one-way platform support (MAT_ONEWAY defined but unchecked)
- Runtime ice physics (MAT_ICE defined but unchecked)
- Maps beyond 1-1 (2-1 through 1-10, etc.) — architecture designed for them, but only 1-1 implemented
- NPC entities (Granny, Theo, Badeline)
- Coin, Feather, SignPost entities
- StaticProp .glb model instancing
- Removing the old tools (normalize_og_map.py, bake_map.py, colmesh_bake.py) — keep for reference until new tool is proven
