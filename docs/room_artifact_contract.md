# Room Artifact Contract

This project treats gameplay data and visible static geometry as two related
artifacts with different jobs:

```txt
rom:/lvl/<room>.lvl    gameplay artifact: LVL2 collision, entities, metadata
rom:/lvl/<room>.t3dm   render artifact (offline at this stage): static visible
                       room geometry + materials
rom:/lvl/<room>.colmesh collision mesh: BVH over quantized triangles
rom:/lvl/<room>.nav     offline sidecar: TrafficBlock path data (no runtime
                       consumer yet)
```

## LVL2 (current level format)

The shipping gameplay artifact is **LVL2**, serialized by
`tools/lvl_format.py` and consumed by `src/user/gameplay/world/level_loader.cpp`.
The header is 0x44 (68) bytes and carries counts for colliders, faces, vertices,
entities and strings, plus atmosphere fields (skybox/music/ambience/snow) and
byte offsets to each section. Full layout:

```txt
+0x00  magic            char[4]  "LVL2"
+0x04  version          uint32   = 2
+0x08  collider_count   uint32
+0x0C  face_count       uint32
+0x10  vertex_count     uint32
+0x14  entity_count     uint32
+0x18  string_count     uint32
+0x1C  skybox_str_id    uint16
+0x1E  music_str_id     uint16
+0x20  ambience_str_id  uint16
+0x22  snow_amount_q8   uint16
+0x24  snow_dir_x/y/z   int16[3]
+0x2A  reserved         uint16
+0x2C  off_strings      uint32
+0x30  off_colliders    uint32
+0x34  off_faces        uint32
+0x38  off_vertices     uint32
+0x3C  off_entities     uint32
+0x40  off_props_blob   uint32
```

Face flags (uint16): bit 0 = solid, bit 1 = visual_only.

The `.lvl` is the gameplay + visible-geometry source the runtime renders today
(`LvlRoomRenderer`). The `.colmesh` is the collision source, baked from the same
`ParsedMap` in `tools/writers/colmesh_writer.py`.

## T3DM is an offline artifact at this stage

The `.t3dm` is produced and validated by the offline pipeline (GLB intermediary
→ `gltf_to_t3d` → `tools/patch_t3dm_materials.py`) but is **not connected to
the runtime yet**:

- `GameplayScene` still loads `LvlRoomRenderer` from the `.lvl`.
- The renderer cutover to T3DM is explicitly out of scope for the current
  pipeline migration.
- T3DM remains chunk-based (`T3M` magic + version byte + chunk pointer table);
  the structural summary is recorded in the baseline for later parity checks.

## Baseline

The versioned reference for `1-1` lives in `tests/fixtures/baseline/`:

- `baseline.json` — map SHA-256, scale, format versions and artifact counts.
- `1-1/` — decoded summaries of LVL2, colmesh, NAV and the current T3DM, plus
  the manifest and the T3DM bytes.

## Map-pack (multi-room) layer

The whole A-side `1.map` is partitioned into spatial grid chunks; each chunk is
a normal single-room LVL2 + colmesh (this contract applies per chunk). A
map-pack manifest (`<pack>.mappack` binary + `<pack>.mappack.json` host view)
lists rooms (id, lvl/colmesh paths, world AABB, atmosphere), adjacency (±X/±Z
neighbor ids) and per-room spawns. Serialization: `tools/mappack_format.py`;
runtime reader: `mappack_loader.{hpp,cpp}` (`MapSpec`/`MapRoomSpec`,
`kMaxRooms = 64`).

### Grid chunking axis convention (load-bearing)

The grid is 2D in **WORLD XZ** — the same axes the runtime
`Map::ResolveCellByPosition` resolves on:

- world = `transform_point(map) = (map_x·s, map_z·s, −map_y·s)`
- cell `ix = floor(world_x / (chunk_size·scale))`,
  cell `iz = floor(world_z / (chunk_size·scale))` (= depth, −map_y)

The second grid axis is world Z (depth), **NOT** map_z, which is the Quake UP
axis. The bake (`tools/ogmap_lib/brush_grid.py::cell_of`) uses the same formula
with a `scale` parameter so bake and runtime can never disagree on a cell,
including at seams (an earlier map-unit `(x, z)` partition used the up axis and
shipped chunks whose content never matched the runtime's cells — the
fall-through regression fixed by `.plans/interconnected-map-fixup/`).
`world_aabb_for_cell` returns world-space AABBs (depth-indexed) for preload
culling only — active-cell resolution is always the grid index.

### Bake defaults for `1.map`

- `--chunk-size 1200` (map units) → 47 chunks, max 891 faces / 3412 verts.
- Chunk size is in MAP units; world cell size = `chunk_size · scale`.
- Boot uses the manifest `start_spawn` (the `Start`-named PlayerSpawn), not
  the `.lvl`'s last PlayerSpawn.
- Colmesh reuse is opt-in (`--reuse-colmesh`, default off); without it, a
  stale `.colmesh` left on a decoration-only chunk is unlinked so the DFS can
  never ship wrong collision.

Regenerate with `python3 tools/bake_baseline.py`. The baseline is frozen
reference data; parity tests compare against it rather than capturing
expectations at runtime.

## Brush-class policy

Every brush-bearing source class must declare both axes before it may emit data:

```txt
render_mode: static_mesh | actor_model | none | unsupported
collision_mode: solid | actor_owned | trigger | none | unsupported
```

The first-room audit is expected to resolve the classes currently present in the
OG map, including at least `worldspawn`, `Decoration`, `SpikeBlock`,
`TrafficBlock`, and `DeathBlock`. No class may silently become visible or solid
just because it happens to carry brushes.

## Material policy

The legacy LVL1 render path may continue using manifest-based validation while it
exists. The active `.t3dm` render path must validate its own material references
before ROM bundling so TMEM safety follows the artifact that actually renders.

## Canonical world IR and map-pack v2 (interconnected rebuild)

The interconnected rebuild (`tools/ogworld/`) introduces one canonical world
representation from which all artifacts are emitted:

```txt
.map ──→ canonical world IR ──→ global CMSH (one, world-space)
                          └──→ per-visual-cell LVL2 rooms
                          └──→ map-pack v2 manifest
```

### Canonical IR (`tools/ogworld/`)

- `model.py` — immutable records: `SourceBrush`, `SourceFace`, `WorldPolygon`,
  `CollisionTriangle`, `SpawnRecord`, `ChunkInput`, `WorldBuild`. Every
  polygon/triangle/spawn carries source identity (entity/brush/face index) and
  explicit policy.
- `class_policy.py` — full-world class policy table separating `collision_mode`
  from `render_mode`. Dynamic classes (`TrafficBlock`, `FallingBlock`,
  `MovingBlock`, `FloatyBlock`, `GateBlock`, `CassetteBlock`, `BreakBlock`,
  `DoubleDashPuzzleBlock`) are labeled **static-proxy** for this
  collision-first milestone. `DeathBlock` is an invisible kill volume:
  collision-only, not rendered (so it is not duplicated into dozens of visual
  cells).
- `parse.py` — adapts the proven `ogmap_lib.parse_map` into the IR, preserving
  spawn names and entity properties.
- `geometry.py` — computes brush polygons once, orients from the transformed
  face normal, dedupes, fans, and attaches source/material identity.
- `collision.py` — builds one global `CollisionScene` (all policy-approved
  static surfaces exactly once).
- `chunking.py` — partitions visual geometry by world-XZ grid; a brush appears
  in every cell whose column intersects its AABB (seam coverage), clipped to
  each cell's column to bound per-cell face/vertex counts. Point entities are
  assigned to exactly one cell.

### Overlap ownership

- **Global collision** is never partitioned: every policy-approved solid
  surface appears exactly once in the single global CMSH.
- **Visual geometry** may be duplicated across cells (a brush crossing a seam
  belongs to both sides, clipped per column). The report records each source
  brush → cell count and the largest duplication multiplier.
- **Point entities / spawns** occur in exactly one cell (their origin cell).

### Map-pack v2 (`tools/mappack_format.py`)

The v2 manifest (`MPP2` magic, version 2) carries the metadata v1 loses:

- source-map SHA-256, pipeline version, scale, chunk size
- one global collision path/hash/counts (crc32 + size + vertex/triangle/BVH
  counts)
- per-visual-room: id, lvl path, **render origin**, world AABB, lvl crc32/size,
  neighbor ids, and a spawn table
- a fixed spawn table with named Start/anchor/actor records (kind, source id,
  room id, world position, name, classname)

`Start` is the initial/default checkpoint. Other named `PlayerSpawn` records
are anchors, not automatically activated checkpoints; a future trigger/save
record must select one before save state may use it.

### Source hashes (`tools/artifact_hash.py` + `artifact_hash.{hpp,cpp}`)

A deterministic CRC32 over each artifact and the source map. The writer records
hash + byte size; the N64 loader uses the same implementation to reject
stale/truncated DFS files. Human-readable reports may additionally include
SHA-256, but the runtime contract does not depend on a host-only hash.

### World-space collision vs chunk-local rendering

Collision and player state remain in **shared world coordinates**. Each visual
chunk manifest stores a **render origin**; the renderer subtracts it before
fixed-point packing, and the scene renders the player, camera, and actors in
the same local frame. This avoids the `kPosScale=32` int16 overflow on the full
map's absolute coordinates while preserving world-space movement and chunk
resolution.

### Bake defaults for `1.map` (interconnected)

- `--chunk-size 1200` → 45 visual cells (with overlap partition + DeathBlock
  not rendered), max 920 faces / 3527 verts, all ≤ caps.
- Global collision: ~20,868 verts, 10,618 tris, 8,191 BVH nodes, ~383 KB on
  disk.
- All 45 rooms reachable from `cell_00_00`; spawn records: 1 Start, 10 anchors,
  20 strawberries, 10 cassettes, 8 refills, 6 springs.

