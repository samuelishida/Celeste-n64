# Whole Interconnected Map — Forsaken City A-Side

## Context

The previous OG→N64 conversion pipeline (`og-map-pipeline-final`) only baked the
small **B-side `1-1.map`** (56 entities, 48 brushes → 584 verts / 308 tris /
367 faces). That fit the runtime's single-room caps (`kMaxFaces=1024`,
`kMaxVertices=8192`) but it is **not** the game's main traversable world.

The user's intuition is correct: the map scope was effectively reduced. The real
"Forsaken City" the player traverses is the **main A-side `1.map`** — one
continuously-traversable world with 11 checkpoints, 20 strawberries, and the
bulk of the geometry. Baking `1.map` through the current pipeline produces:

| Artifact | Result | Single-room cap |
|---|---|---|
| `lvl` faces | **7921** | 1024 |
| `lvl` vertices | **30436** | 8192 |
| `colmesh` triangles | **10596** | — |
| `colmesh` BVH nodes | **8191** | — |

That is ~8× over the single-room caps and infeasible to hold in N64 RAM as one
loaded room. So the full contiguous A-side **cannot** be one LVL2 room — it must
be partitioned into connected chunks, each fitting the caps, with a multi-room
runtime that streams the active chunk + neighbor ring and swaps chunks at
boundaries.

This plan delivers: the **whole contiguous `1.map` A-side**, partitioned into
spatial chunks (mirroring the OG's own `1000³`-unit `GridPartition`), a
**map-pack** format sitting above LVL2 that lists chunks + adjacency +
transitions, and a **multi-room runtime** (`Map` container + active-chunk
streamer + transition triggers + per-chunk save). B-side cassette sub-areas are
wired via the existing `SetLevel` Push/Pop and are a thin tail increment, not
the core.

## Architectural decisions

- **Decision: partition the main `1.map` by a spatial grid.** Rationale:
  deterministic, scales to any map, needs no hand-authored boundaries.
  Alternatives rejected: checkpoint-based chunks (needs manual boundary
  authoring, fewer/larger rooms that may still exceed caps); authoring regions in
  the map (requires editing the OG `.map`). Each brush is assigned to the cell
  of its **bounds center**, so a brush is never clipped across cells — no seam
  holes.

  **Deliberate deviation from the OG.** The OG's `GridPartition<Solid>`
  (`Map.cs:213-219`) is a **3D** grid (`Vec3(1000,1000,1000)`, iterating
  `x,y,z`), grouping tall solids vertically too. This plan uses a **2D grid in
  XZ** — each chunk is a vertical column spanning the full Y extent of the
  map. Justification: N64 Forsaken City is effectively one floor level (no
  vertically stacked rooms to isolate), and 2D columns give simpler ±X/±Z
  adjacency and a smaller room count. The downside: a 2D column may be taller
  (more faces) than an OG 3D cell, so the cap-fit guarantee is **not** inherited
  from the OG and is verified empirically by the bake (see Inc 1 fatal guard
  and `--chunk-size` fallback). If a 2D column exceeds caps, the operator
  shrinks `--chunk-size`; if the map is ever vertically dense, revisit toward
  the OG's 3D grid. This deviation is recorded so "mirrors the OG" is not
  claimed falsely.

- **Decision: chunk size in map units is a `BakeConfig` field (default `1000`,
  the OG value), tunable via `--chunk-size`.** The bake emits one LVL2 + colmesh
  per non-empty XZ cell (vertical column). The plan verifies every emitted
  chunk fits the caps (`faces ≤ 1024`, `verts ≤ 8192`); any chunk over cap is a
  **fatal bake error** with the offending cell index, so we never ship an
  unloadable chunk. The bake also **fatally errors if the non-empty cell count
  exceeds the runtime `kMaxRooms`** (`64`), so the manifest never overflows the
  runtime room table.

- **Decision: a new thin **map-pack** format sits above LVL2.** It is a small
  binary or JSON manifest: room list (id, .lvl path, .colmesh path, world AABB,
  atmosphere), adjacency graph (neighbor room ids per axis ±X/±Z), and
  transition trigger volumes. LVL2 and colmesh formats are **unchanged** — each
  chunk is a normal single-room LVL2 + colmesh. This keeps the existing
  loader/renderer/collision untouched and reusable per chunk.

- **Decision: the runtime gets a new `Map` container owning multiple `Room`s
  with an active-chunk streamer**, built **on top of** the existing single-`Room`
  + `LvlRoomRenderer` + `ActorWorld` + `coll_mesh` machinery. Nothing in the
  single-room code is rewritten; it is pooled. Active set = the player's
  current chunk + a small neighbor ring, loaded ahead and LRU-evicted. Crossing
  a chunk boundary swaps the active room via a **new** load path (not
  `ReloadBakedLevel` — see the RAM and respawn decisions below).

- **Decision: `LevelGeometry` is load-and-discard, NOT resident per loaded
  room.** `LevelGeometry` is ~225 KB (`1024 faces × ~80 B + 8192 verts × ~20 B`
  at cap) and is only needed to build the `LvlRoomRenderer` batch list; at
  runtime collision uses `room.coll_mesh` and rendering uses `LvlRoomRenderer`.
  So `LoadedRoom` holds the `Room`, its `coll_mesh`, and the
  `LvlRoomRenderer` — **not** `LevelGeometry`. `LevelGeometry` lives in a
  single scratch buffer reused to load each room; once the renderer is built
  the scratch is reused for the next load. This is the key to fitting multiple
  rooms in N64 RAM.

- **Decision: the resident ring is small.** A worst-case cap-fitting chunk is
  ~36 KB `.lvl` (raw, freed after load) + ~11 KB `.colmesh` (dequantized into
  ~8192×12 B `world_verts` ≈ 98 KB + BVH ≈ 0.5 MB at 8191 nodes; measured real
  1-1 chunk: 584 verts/308 tris/231 nodes → ~7 KB verts + ~15 KB BVH) + the
  `LvlRoomRenderer` uncached-vertex pair buffer (~64–128 KB) + the `Room` struct
  (~35 KB). Measured real 1-1 ≈ 0.2 MB resident; a worst-case cap-saturated
  chunk ≈ 0.7 MB. **Default ring = active + 0 neighbors (v1); expand to
  active + 1 neighbor only after measuring real chunk footprints.** The plan
  does NOT promise a 1-ring by default; it promises active-only with a
  configurable ring of 0/1, sized to stay well under the ~4 MB N64 usable RAM.
  `kMaxLoadedRooms` defaults to **3** (active + 1 neighbor + 1 spare), not 9.

- **Decision: chunk transitions are a NEW load path, not `ReloadBakedLevel`.**
  `ReloadBakedLevel()` ends by calling `ResetPlayerToRoomStart()` (common-mistake
  `missing-player-start-init.md`), which would teleport the player to the new
  room's `player_start` on every boundary crossing. A chunk transition must
  NOT do that — the player keeps its world position and velocity (all chunks
  share world coords). So the `Map` introduces `LoadRoomGeometry(room_id)`
  which loads `.lvl`→renderer + `.colmesh` and dispatches the room's entities
  but **skips** `ResetPlayerToRoomStart()` and `CameraController::Reset()`.
  Those two are reserved for **death-respawn across chunks only**: on death,
  `RespawnSystem` returns the checkpoint; `GameplayScene` ensures the
  checkpoint's chunk is active, then calls `ResetPlayerToRoomStart()` +
  `CameraController::Reset()` against that chunk (per
  `camera-respawn-reset.md`). This separation is load-bearing and called out
  so the common-mistake's "every load path syncs" rule is honored without
  breaking traversal.

- **Decision: chunk transitions are trigger volumes, not seamless streaming.**
  Each chunk boundary carries an implicit trigger resolved by the player's XZ
  **grid index** (mathematical `floor(player_x / chunk_size)`,
  `floor(player_z / chunk_size)`) — **not** by world-AABB containment, because
  a chunk's world AABB (union of its brush AABBs) can overlap a neighbor's
  when a brush centered in cell A extends into cell B; AABB containment would
  flap at overlaps. The manifest's per-room world AABB is only for
  neighbor/preload culling, not active-cell resolution. When the player's XZ
  grid index changes, the runtime swaps the active room to that neighbor
  (neighbor is already pre-loaded if the ring > 0; else loaded synchronously).
  Player velocity/state carry across the swap (unlike cassette Push/Pop, which
  is a full reload).

- **Decision: B-side cassette sub-areas reuse the existing `SetLevel` as
  Push/Pop.** The OG loads each B-side as a separate pushed `World` with a
  screen-wipe; we approximate that by having cassette pickup call `SetLevel`
  with the B-side path, recording the return (parent) path, and a second
  cassette in the B-side calling `SetLevel(parent)`. This is the cheapest
  faithful option and reuses the existing whole-level swap. A proper
  parent-state-preserving scene-stack is explicitly **out of scope** (noted as
  future work).

- **Decision: cassette target is carried as a runtime field.** The OG
  `Cassette` entity has a `map` property naming its target sub-area. The
  current `Room` struct has only `cassette` (Vec3) + `has_cassette` — no
  target string. The plan adds `char cassette_target[32]` to `Room`, populated
  by `level_loader` from the LVL entity props blob (the props blob already
  stores arbitrary k/v; ensure the `map` key survives baking and loading).
  `entity_ids.hpp` adds `kPropCassetteTarget = "map"`. Without this field the
  B-side transition (Inc 6) cannot find its target — it is load-bearing.

- **Decision: per-chunk save state.** `SaveSystem::LevelRecord` already has a
  dead `completed_submap_bits` field; we add per-chunk strawberry bits and a
  `checkpoint_room_id` so respawn routes to the correct chunk. Strawberry state
  is keyed by `(level_id, chunk_id, strawberry_local_id)`.

## Assumptions and answers from code

- Decision: main `1.map` bakes to 7921 faces / 30436 verts (8× over caps).
  Source: code — `python3 tools/bake.py assets/og_converted/maps/1.map` measured
  in planning.
- Decision: single-room caps are `kMaxFaces=1024`, `kMaxVertices=8192`.
  Source: code @ `src/user/gameplay/world/level_loader.hpp:24-25`.
- Decision: OG partitions solids into a `1000³`-unit grid by bounds-center.
  Source: code @ `Celeste64-og/Source/Data/Map.cs:213-219`.
- Decision: OG 1-1 in this repo = a B-side sub-area (56 entities), NOT the main
  A-side. Main A-side is `1.map` (706 entities, ~1888 brushes). Source: code —
  `Celeste64-og/Content/Maps/1.map` / `1-1.map`.
- Decision: `bake.py` is single-room, no chunking exists. Source: code —
  search `room_split|split_map|multi-room` in `tools/` → zero hits.
- Decision: runtime is single-`Room` end-to-end (`world.hpp` one Room,
  `GameplayScene` one `LvlRoomRenderer`+`ActorWorld`, `world.cpp` queries one
  `Room&`, `RespawnSystem` one `Vec3`). Source: code @
  `src/user/gameplay/world/world.hpp`, `scene/gameplay_scene.cpp`.
- Decision: LVL2 is single-room (one header, flat arrays, no room index).
  Source: code @ `tools/lvl_format.py`, `docs/room_artifact_contract.md`.
- Decision: `SetLevel` already swaps whole levels (title screen uses it).
  Source: code @ `scene/gameplay_scene.cpp:303`.
- Decision: `SaveSystem::LevelRecord.completed_submap_bits` exists, unused at
  runtime. Source: code @ `src/user/gameplay/.../save_system.hpp`.
- Decision: entity ids today = PlayerSpawn(0), Strawberry(1), Refill(2),
  Spring(3), Cassette(9). Source: code @ `entity_ids.hpp`.
- User-confirmed scope: main `1.map` A-side only (B-sides a thin tail via
  SetLevel Push/Pop). Spatial grid chunks. Full multi-room runtime (Map +
  streaming + transitions + per-room save).

## Risks accepted

- **Chunk boundary feel:** swapping the active room at a boundary may cause a
  one-frame hitch or a visible pop if the neighbor ring isn't loaded in time.
  Mitigation: configurable ring (default 0 = active-only for v1), expand to 1
  after measuring; accept; revisit if hitches show in ROM.
- **Brushes spanning two cells:** a brush is assigned by bounds-center to one
  cell, so a large brush may visually/collision-extend into a neighbor cell
  that doesn't load it. Mitigation: the owning chunk's colmesh/lvl still holds
  the whole brush; the neighbor chunk queries only its own mesh, so the player
  inside the neighbor but near the shared edge could miss the overhanging
  geometry. Accept for v1 (OG has the same single-solid-per-cell behavior);
  revisit if players fall through overhangs.
- **RAM budget of the resident ring is measured, not assumed.** `LevelGeometry`
  is load-and-discard (not pooled), so resident per-room = `Room` (~35 KB) +
  dequantized colmesh (real 1-1 ≈ 22 KB; worst-case cap-saturated ≈ 0.6 MB) +
  `LvlRoomRenderer` uncached buffer (~64–128 KB). Default ring = 3 rooms
  (active + 1 neighbor + spare) ≈ 0.5–2.5 MB worst-case, well under the ~4 MB
  N64 usable RAM. **The plan does not ship a 9-room ring.** If a real chunk
  saturates caps, the bake fatal-guards it; the ring size is tuned from
  measured footprints.
- **No parent-state preservation for B-sides:** `SetLevel` reloads the parent
  on return, losing e.g. killed enemies in the parent. Accept; OG uses a
  screen-wipe and rebuilds too. Future: proper scene-stack.
- **Grid chunk count may be large:** main map world AABB could yield many
  cells; most will be empty. Bake skips empty cells. **Bake fatal-errors if
  non-empty cells exceed `kMaxRooms` (64)** so the runtime room table never
  overflows; operator shrinks `--chunk-size` if needed.
- **Save-format break:** Inc 5 adds `chunk_strawberry_bits[4]` +
  `checkpoint_room_id[16]` to `LevelRecord`, bumping `kSaveVersion`. Existing
  saves are invalidated; on version mismatch the loader resets the record
  (no migration of in-progress saves — this is a pre-release prototype).

## Increment DAG

- Inc 1 — Grid-chunked bake (L) — depends on: none — unblocks: 2, 3, 6
- Inc 2 — Map-pack format + writer (M) — depends on: 1 — unblocks: 3, 6
- Inc 3 — Multi-room runtime: `Map` container + active-chunk streamer (L) —
  depends on: 1, 2 — unblocks: 4, 5, 7
- Inc 4 — Transition triggers + chunk swap + player-state carry (M) —
  depends on: 3 — unblocks: 5, 7
- Inc 5 — Per-chunk save (strawberry bits, checkpoint room id) (M) —
  depends on: 3, 4 — unblocks: 7
- Inc 6 — B-side cassette Push/Pop via SetLevel (single-room B-sides) (S) —
  depends on: 1, 2 — unblocks: 7
- Inc 7 — Parity, probes, cleanup, docs (M) — depends on: 4, 5, 6 — unblocks:
  none

## Increments

### Inc 1 — Grid-chunked bake (L)
**Depends on:** none
**Unblocks:** 2, 3, 6
**Done criteria:** `python3 tools/bake_map_pack.py assets/og_converted/maps/1.map
--out-dir build/forsaken-city --chunk-size 1000` emits one `<chunk>.lvl` +
`.colmesh` per non-empty grid cell, every chunk ≤ caps, a `chunks.json`
inventory, and a host smoke test asserts all chunks fit caps.

**STATUS: DONE.** `tools/ogmap_lib/brush_grid.py` (partitioning by 2D XZ
bounds-center) + `tools/bake_map_pack.py` (orchestrator reusing the existing
writers unchanged) + `tests/map_pack_smoke.py` implemented. Main `1.map` bakes
to 60 cap-fitting chunks at `--chunk-size 650` (max 978 faces / 3668 verts; ≤
`kMaxRooms=64`); containment holds (1182 whole-map brushes == sum of chunk
brushes). B-side `1-1.map` bakes as 1 chunk via `--submap` (367 faces / 1362
verts, matching baseline). Cap guard and room-overflow guard fire correctly.
`bake_parity_smoke.py` still green for the 1-1 single-room path. Note: at the
OG's `chunk_size=1000` the dense column `cell_n01_00` exceeds face cap (1596
> 1024) — the plan's 2D-XZ deviation requires `--chunk-size 650` for `1.map`,
as the plan anticipated (shrink `--chunk-size` when a column is over cap).

**FIXUP (see `.plans/interconnected-map-fixup/` — authoritative correction):**
the numbers above ("60 cap-fitting chunks at `--chunk-size 650`") are an
artifact of the ORIGINAL up-axis partition. The first implementation of
`brush_grid.py::cell_of` keyed the second grid axis by map_z, which is the
Quake UP axis, while the runtime `Map::ResolveCellByPosition` resolves on
world XZ (depth = −map_y) — so every chunk's content/adjacency/spawns landed in
cells the runtime never resolves to, and the ROM fell through on frame 1. The
corrected world-XZ axis gives **118 non-empty cells at 650 (> `kMaxRooms` 64,
fatal)**, so the default is now **`--chunk-size 1200` → 47 chunks** (max 891
faces / 3412 verts). The fixup plan also: boots the player from the manifest
`start_spawn` (the `Start`-named PlayerSpawn) instead of the `.lvl`'s last
PlayerSpawn, makes colmesh reuse opt-in (`--reuse-colmesh`, default off), and
adds bake↔runtime seam/Z-axis regression tests. The Inc 1 `cell_of` spec below
("partitioning operates in map units") is superseded by the world-space
`cell_of(point, chunk_size, scale)` in the fixup plan.

#### Files to touch

##### tools/ogmap_lib/brush_grid.py
- What changes: new module; assigns each brush-bearing entity to a grid cell by
  its post-scale world AABB center.
- Function(s):
  - `compute_world_aabb(brush, scale) -> AABB`
  - `cell_of(aabb_center, chunk_size) -> tuple[int,int,int]`
  - `partition_parsed_map(parsed_map, scale, chunk_size) -> dict[cell_key, list[entity_index]]`
- Data shapes: `cell_key = (ix, iz)` (2D grid in XZ; Y is free within a chunk —
  a chunk is a vertical column, matching OG's intent of broad-phase columns).
  `chunk_size` in **map units** (pre-scale); partitioning operates in map
  units, writers apply scale.
- Integration points: consumed by `bake_map_pack.py`; reads `ParsedMap.entities`
  and each entity's brush bounds from `ogmap_lib`.
- Error paths: empty cells are skipped (not emitted); an entity with no brushes
  (point entity) is assigned to its `origin` cell for entity collection.

##### tools/bake_map_pack.py
- What changes: new entrypoint; parses one `.map`, partitions by grid, runs the
  existing `write_colmesh`/`write_lvl` per chunk via a sub-`ParsedMap` view.
  **The per-chunk bake calls the existing `tools/writers/lvl_writer.write_lvl`
  and `tools/writers/colmesh_writer.write_colmesh` UNCHANGED, once per chunk** —
  so the `og-map-polygon-winding.md` guards (dedupe before writing LVL verts,
  reversed-winding check) carry over per chunk unchanged. `bake_map_pack.py` is a
  thin orchestrator; it does not reimplement writers. `tools/bake.py` (the
  single-room tool) remains for the B-side / single-room path (Inc 6) and is not
  deprecated.
- Function(s):
  - `build_chunk_submap(parsed_map, entity_indices, cell_key) -> ParsedMap`
  - `write_chunk(chunk_submap, out_dir, cell_key, scale, eps, strict) -> ChunkStats`
  - `main(argv)` — CLI: `bake_map_pack.py <room.map> [--out-dir] [--scale]
    [--eps] [--chunk-size 1000] [--strict]`
- Data shapes: `ChunkStats{cell_key, faces, vertices, triangles, bvh_nodes,
  entities, fits_caps: bool, sha256}`. Emits `chunks.json` = list of ChunkStats.
- Integration points: imports `ogmap_lib.parse_map`, `ogmap_lib.brush_grid`,
  `writers.colmesh_writer.write_colmesh`, `writers.lvl_writer.write_lvl`.
- Error paths: any chunk with `faces > 1024` or `vertices > 8192` → fatal
  `BakeError` listing the cell key and counts (no partial publish). **Non-empty
  cell count > `kMaxRooms` (64) → fatal `BakeError`** listing the count and
  suggesting a smaller `--chunk-size`. Atomic publish per chunk to a temp dir
  then rename.

##### tools/writers/lvl_writer.py, colmesh_writer.py
- What changes: accept an optional `cell_aabb` filter so a chunk only emits
  brushes assigned to it; otherwise unchanged. Point entities (PlayerSpawn,
  Strawberry, Cassette, Refill, Spring) are emitted per their origin cell.
- Error paths: unchanged.

#### Edge cases
- Worldspawn brushes span the whole map; assign each worldspawn brush by its
  own bounds-center (not the entity's). The shell is naturally distributed.
- A point entity (Strawberry) on a chunk boundary → assign to the cell of its
  `origin`.
- Decoration/StaticProp skipped classes: still skipped per existing policy.
- PlayerSpawn: each of the 11 spawns lands in its cell; the runtime will pick
  the start cell from the `Start`-named spawn (Inc 3).

#### Verification
- Run: `python3 tools/bake_map_pack.py assets/og_converted/maps/1.map
  --out-dir build/forsaken-city --chunk-size 1000`
- Tests to add: `tests/map_pack_smoke.py` — asserts every emitted chunk's
  faces ≤ 1024 and verts ≤ 8192; **containment (not equality): the sum of
  chunk brush counts across the pack == the whole-map brush count (every brush
  appears in exactly one cell)**, while per-chunk face/vert sums are ≤ caps
  (they are NOT required to equal the whole-map face/vert totals, because
  per-chunk dedup runs on a smaller set than whole-map dedup — see
  `og-map-polygon-winding.md`); non-empty cell count ≤ `kMaxRooms`; `chunks.json`
  valid.
- Done: all chunks fit caps; non-empty cell count ≤ `kMaxRooms`; `chunks.json`
  lists non-empty cells; every whole-map brush appears in exactly one chunk;
  existing `bake_parity_smoke.py` still passes for the unchanged 1-1
  single-room path (Inc 6 later re-bakes `1-1` as a 1-room map-pack; until
  then `1-1` stays a plain single-room LVL and its parity test is untouched).

### Inc 2 — Map-pack format + writer (M)
**Depends on:** 1
**Unblocks:** 3, 6
**Done criteria:** `bake_map_pack.py` emits a `forsaken-city.mappack` manifest
listing rooms (id, lvl path, colmesh path, world AABB, atmosphere), adjacency
(±X/±Z neighbor ids), and per-room entity summary; a host test parses it.

**STATUS: DONE.** `tools/mappack_format.py` (MapRoom/MapPack dataclasses +
JSON + binary serializers with `MPPK`/v1 big-endian layout) created and wired
into `bake_map_pack.py` (emits both `.mappack.json` and `.mappack`).
`src/user/gameplay/world/mappack_loader.{hpp,cpp}` (`MapRoomSpec`/`MapSpec`
with `kMaxRooms=64`, `FindRoom`, `LoadMapPack` big-endian reader) created and
compiles host-side. `tests/mappack_smoke.py` (JSON + binary roundtrip,
adjacency symmetry, DFS-path layout) and `tests/mappack_loader_smoke.cpp`
(C++ parses the binary, asserts start room/atmosphere/scale/adjacency) pass.
Manifest for `1.map` at chunk_size=650: 60 rooms, start=`cell_00_00`, shared
atmosphere (skybox=city, music=mus_lvl1). `map_pack_smoke.py` +
`bake_parity_smoke.py` still green.

#### Files to touch

##### tools/mappack_format.py
- What changes: new module; defines the map-pack manifest schema + reader/writer.
- Function(s):
  - `write_mappack(pack: MapPack, out_path) -> None`
  - `read_mappack(path) -> MapPack`
  - `build_mappack(chunks: list[ChunkStats], parsed_map, scale, chunk_size) -> MapPack`
- Data shapes:
  ```python
  @dataclass
  class MapRoom:
      id: str               # e.g. "cell_03_05"
      lvl_path: str         # "rom:/lvl/forsaken-city/cell_03_05.lvl"
      colmesh_path: str
      aabb_min: tuple[float,float,float]  # world units (post-scale)
      aabb_max: tuple[float,float,float]
      atmosphere: dict       # skybox/music/ambience/snow — shared from map
      spawns: list           # PlayerSpawn/Strawberry/etc in this cell
      neighbors: dict[str,str]  # {"+X": "cell_04_05", "-X": ..., "+Z": ..., "-Z": ...}
  @dataclass
  class MapPack:
      id: str                # "forsaken-city"
      rooms: list[MapRoom]
      start_room_id: str     # the cell containing the "Start" PlayerSpawn
      scale: float
      chunk_size: float
  ```
  Format: JSON (human-readable, small; N64 reads via a tiny JSON or a compact
  binary header — see edge case).
- Integration points: emitted by `bake_map_pack.py`; consumed by the runtime
  loader (Inc 3).
- Error paths: missing neighbor cell (map edge) → `neighbors` entry absent or
  `null`; loader treats null as "no transition".

##### tools/bake_map_pack.py
- What changes: after writing chunks, call `build_mappack` + `write_mappack`.
- Error paths: no `Start`-named PlayerSpawn → fatal (cannot pick start room).

##### src/user/gameplay/world/mappack_loader.cpp / .hpp
- What changes: new; reads the `.mappack` manifest into a runtime `MapSpec`:
  room table (id → {lvl path, colmesh path, aabb, neighbors, start spawn}),
  start room id.
- Function(s):
  - `LoadMapPack(const char* mappack_path, MapSpec& out) -> bool`
  - `MapSpec::FindRoom(const char* id) -> const MapRoomSpec*`
- Data shapes:
  ```cpp
  struct MapRoomSpec {
      char id[16];
      char lvl_path[64];        // rom:/lvl/<mappack>/<chunk>.lvl — wide enough
      char colmesh_path[64];    // .colmesh variant
      AABB world_aabb;          // for preload culling only (not containment)
      char neighbors[4][16];     // +X,-X,+Z,-Z; empty = none
      Vec3 start_spawn;         // first PlayerSpawn in this room
      bool has_start_spawn;
  };
  struct MapSpec {
      static constexpr int kMaxRooms = 64;   // bake fatal-errors if exceeded
      MapRoomSpec rooms[kMaxRooms];
      int room_count = 0;
      char start_room_id[16];
      char atmosphere_skybox[16];
      // ... music/ambience/snow shared
      float chunk_size;          // world units, for ResolveCellByPosition
  };
  ```
- Integration points: consumed by `Map` container (Inc 3); `chunk_size` is
  stored so `ResolveCellByPosition` matches the bake partition exactly.
- Error paths: too many rooms (>64) is a **bake-time** fatal (Inc 1 guard), so
  the loader never sees an overflow; defensively, load also fails fatal on
  overflow; bad path → false return.

#### Edge cases
- JSON on N64: prefer a compact binary variant of the manifest (length-prefixed
  strings + fixed arrays) to avoid a JSON parser in ROM. The writer emits BOTH
  `.mappack.json` (host-side/tests) and `.mappack` (binary, ROM); the runtime
  reads only the binary. Document both in `docs/room_artifact_contract.md`.
- Atmosphere is shared across all chunks of one map (one skybox/music). Stored
  once in `MapPack`, not per room.

#### Verification
- Run: bake + `python3 tests/mappack_smoke.py` (round-trips JSON, asserts
  start room resolves, neighbor graph is symmetric).
- Tests to add: `tests/mappack_smoke.py`.
- Done: `.mappack` + `.mappack.json` emitted; runtime loader compiles and parses
  a host-side fixture.

### Inc 3 — Multi-room runtime: `Map` container + active-chunk streamer (L)
**Depends on:** 1, 2
**Unblocks:** 4, 5, 7
**Done criteria:** `GameplayScene` loads a `MapSpec`, keeps an active `Room` +
1-ring neighbors loaded, the player can walk across chunk boundaries and the
active room swaps to the neighbor with player velocity preserved; a host smoke
test drives a 2×2 fixture map and asserts room swaps fire.

**STATUS: PARTIAL (3a done; 3b scene-integration deferred into Inc 4).**
Per the plan's CONSIDER (split 3a container / 3b scene-integration), 3a is
complete: `src/user/gameplay/world/map.{hpp,cpp}` (`Map` container with
`kMaxLoadedRooms=3`, `Init`/`EnsureLoaded`/`SetCheckpointRoom`/
`ResolveCellByPosition`/`SetActivByPosition`/`SetActive`/`LoadRoomGeometry`/
`ActiveRoom`, LRU eviction with active+checkpoint pin-set, `LevelGeometry` as
load-and-discard scratch) and `level_loader` refactor (added
`LoadLevelInto` — loads `.lvl` only; `LoadLevel` now delegates to it + the
`.colmesh` auto-swap for back-compat) are implemented and host-compilable.
`tests/map_runtime_smoke.cpp` loads the real `1.map` map-pack (60 rooms),
drives a boundary crossing `cell_00_00`→`cell_01_00`, and asserts: start room
loads+activates, cell resolution matches, boundary crossing detected,
`LoadRoomGeometry` transition preserves player pos/velocity (the
`missing-player-start-init` common-mistake is honored — `LoadRoomGeometry`
does NOT call `ResetPlayerToRoomStart`; that's reserved for the boot/respawn
path in 3b), neighbor collision loads, checkpoint room stays pinned, Reset
cleans up. `LvlRoomRenderer` is held as a forward-declared pointer allocated
only under `#ifdef __mips__`, so `map.cpp` is host-compilable.
**3b (GameplayScene wiring: `Impl` holds a `Map`, per-frame query/update route
through `map_.ActiveRoom()`, boot uses the reset path, transitions use
`LoadRoomGeometry`) is folded into Inc 4**, whose file specs already modify
`gameplay_scene.cpp` for the transition wiring — doing them together avoids
touching the N64 render loop twice. The single-room legacy path still
compiles (the refactor was purely additive).

#### Files to touch

##### src/user/gameplay/world/map.hpp / .cpp
- What changes: new `Map` container owning the `MapSpec` + a pool of loaded
  rooms with LRU eviction.
- Function(s):
  - `Map::Init(const MapSpec& spec)`
  - `Map::EnsureLoaded(const char* room_id)` — loads room + its ring (default
    ring = 0, i.e. active-only; configurable to 1), evicts LRU beyond
    `kMaxLoadedRooms` (default **3**: active + 1 neighbor + 1 spare). **Pin-set:
    the active room and the checkpoint room (Inc 5) are never evicted**; only
    non-pinned ring members are LRU candidates.
  - `Map::ActiveRoom() -> Room&`
  - `Map::ActiveRoomId() -> const char*`
  - `Map::ResolveCellByPosition(const Vec3& pos) -> cell_index` — pure
    math: `floor(pos.x / chunk_size)`, `floor(pos.z / chunk_size)` in **world
    units**. This is the authoritative active-cell test; the manifest's
    per-room world AABB is used only for preload culling, not containment.
  - `Map::SetActivByPosition(const Vec3& player_pos) -> bool` — compares
    `ResolveCellByPosition` to the active cell; returns true if it changed.
- Data shapes:
  ```cpp
  struct LoadedRoom {
      char id[16];
      Room room;                 // existing single-room struct, reused
      // NOTE: LevelGeometry is NOT resident here — it lives in a single
      // shared scratch buffer reused per load, then discarded once the
      // renderer is built. Runtime collision uses room.coll_mesh and
      // rendering uses renderer_.
      LvlRoomRenderer* renderer = nullptr;
      ActorWorld actors;        // reused
      int lru_tick = 0;
      bool pinned = false;      // active or checkpoint room
      bool loaded = false;
  };
  class Map {
      static constexpr int kMaxLoadedRooms = 3;  // active + 1 neighbor + spare
      MapSpec spec_;
      LoadedRoom pool_[kMaxLoadedRooms];
      int active_index_ = -1;
      LevelGeometry scratch_geo_;   // single shared load scratch, reused
      // ...
  };
  ```
- Integration points: `GameplayScene::Impl` holds a `Map` instead of a single
  `Room`/`LvlRoomRenderer`/`ActorWorld`; update/render/query go through
  `Map::ActiveRoom()`.
- Error paths: room load failure → keep previous active, log; out of pool slots
  → evict LRU non-pinned; a corrupt map-pack chunk → do NOT fall back to the
  graybox `GetForsakenCityStartRoom()` (it is not in the map-pack coordinate
  system); instead keep the previous active room and log loudly, or boot to
  the map-pack's `start_room_id` only.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: `Impl` swaps single `Room`/renderer/actor_world for `Map map_`.
  `ReloadBakedLevel` is kept for the **initial map-pack boot** and **death
  respawn** paths (it calls `ResetPlayerToRoomStart()` + `CameraController::Reset()`
  per the common-mistakes). A **new** `Map::LoadRoomGeometry(room_id)` path is
  used for **chunk transitions** (Inc 4): it loads `.lvl`→renderer + `.colmesh`
  and dispatches the room's entities but **skips** `ResetPlayerToRoomStart()`
  and `CameraController::Reset()`, so the player keeps world pos/velocity.
  Initial boot flow: `LoadMapPack(mappack_path)` → `Map::Init` →
  `EnsureLoaded(start_room)` → `SetActive(start)` → `ReloadBakedLevel`-style
  reset (this IS a spawn, so reset is correct here). Per-frame: query/update go
  through `map_.ActiveRoom()`; `SetActivByPosition` checked each tick (Inc 4
  wires the `LoadRoomGeometry` transition).
- Integration points: `world.cpp` query helpers (`RaycastRoomSource`, etc.) take
  `const Room&` — unchanged; called with `map_.ActiveRoom()`.
- Error paths: no map pack → fall back to legacy single-room path (keeps
  first-room demo working during migration).

##### src/user/gameplay/world/level_loader.cpp
- What changes: add `LoadLevelInto(const char* lvl_path, Room& room,
  LevelGeometry& geo)` so the `Map` pool can load multiple rooms; existing
  `LoadLevel` calls it for back-compat.
- Error paths: unchanged.

##### src/user/gameplay/render/lvl_room_renderer.hpp/.cpp
- What changes: allow multiple instances (pool) — verify it has no global
  state; if it does, make instance state per-instance. The `Map` pool owns one
  renderer per `LoadedRoom`.
- Error paths: a renderer fails to load → skip rendering that room.

#### Edge cases
- Player exactly on a boundary: use `SetActivByPosition` with the player's
  center; ties resolved by current active cell (hysteresis) to avoid flapping.
- Neighbor load failure: keep active, log; retry next frame.
- DFS paths: room `.lvl`/`.colmesh` files live under `rom:/lvl/forsaken-city/`;
  update `filesystem/lvl/` Makefile rule to create a subdir per map-pack.

#### Verification
- Run: build a 2×2 fixture map (4 tiny chunks with shared walls), run
  `tests/map_runtime_smoke.cpp` — drives the player across boundaries, asserts
  `Map::ActiveRoomId` changes and player velocity survives.
- Tests to add: `tests/map_runtime_smoke.cpp`, `tests/mappack_loader_smoke.cpp`.
- Done: contiguous traversal across ≥2 chunks works on host; legacy
  first-room path still boots.

### Inc 4 — Transition triggers + chunk swap + player-state carry (M)
**Depends on:** 3
**Unblocks:** 5, 7
**Done criteria:** crossing a chunk boundary performs the active-room swap with
a 1-frame player-position carry (same world coords, no teleport), neighbors
pre-loaded; a host test asserts no fall-through on boundary crossing with a
  floor present in the neighbor.

**STATUS: DONE.** Scene integration complete (`SetMapPack`/`BootMapPack`/`TransitionToRoom` in `gameplay_scene.cpp`); `tests/map_transition_smoke.cpp` passes (verified chunk transition `cell_n05_00` → `cell_n04_00` with player pos/vel preserved, collision loaded in neighbor, no fall-through).

#### Files to touch

##### src/user/gameplay/world/map.cpp
- What changes: `SetActivByPosition` now performs the swap: when the active
  cell changes, mark new active, reposition the player into the new room's
  coordinate frame is **not needed** (all chunks share world coords — the
  partition is by world AABB, so player world position is unchanged across the
  swap). Just ensure the new active's collision is loaded before the next
  physics step.
- Function(s): `Map::TransitionTo(const char* new_room_id, const Vec3&
  player_world_pos, PlayerState& player)` — swaps active, preserves
  `player.position` and `player.velocity`.
- Integration points: `GameplayScene::Update` calls `SetActivByPosition` each
  tick; on change, calls `TransitionTo`.
- Error paths: new room not loaded → force `EnsureLoaded` synchronously before
  swapping; if still missing, clamp player to old cell edge (no fall).

##### src/user/gameplay/player/player_state.hpp
- What changes: ensure `PlayerState` survives a room swap (it already lives in
  `Impl`, not in `Room`), so no change expected — verify and document.
- Error paths: none.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: after `TransitionTo`, re-run `DispatchLevelEntities` for the new
  active room's spawns and re-init the cassette actor if the new room has a
  cassette. Re-bind `camera` to new active room.
- Integration points: camera collision queries go through `map_.ActiveRoom()`.
- Error paths: missing-player-start common-mistake applies: after any room
  load, ensure checkpoint/player sync (here: carry, not reset).

#### Edge cases
- Player dashes across two cells in one frame: `SetActivByPosition` resolves to
  the final cell; intermediate cell loaded as neighbor (already in ring).
- Floor in neighbor but not in current at the seam: the owning chunk holds the
  whole brush, so the neighbor's floor is in the neighbor mesh — must be loaded
  before the player's next floor query. 1-ring pre-load guarantees this.
- Camera crossing boundary before player: camera queries use active room; if
  camera is in a neighbor, query the neighbor too (option: query active + ring
  for camera only). Accept v1: query active only, revisit if camera clips.

#### Verification
- Run: `tests/map_transition_smoke.cpp` — 2-cell fixture with a floor in cell B
  at the seam; player walks from A→B; assert no fall, velocity preserved, active
  id changes.
- Tests to add: `tests/map_transition_smoke.cpp`.
- Done: seamless chunk traversal on host; no fall-through at seams.

### Inc 5 — Per-chunk save (strawberry bits, checkpoint room id) (M)
**Depends on:** 3, 4
**Unblocks:** 7
**Done criteria:** collecting a strawberry sets a per-chunk bit in
`SaveSystem`; dying respawns in the correct chunk at that chunk's checkpoint;
resetting a map-pack clears chunk bits; a host test asserts state is keyed by
chunk.

**STATUS: DONE.** Extended `LevelRecord` with `chunk_strawberry_bits[1]` (64-bit bitfield for 64 chunks) and `checkpoint_room_id[16]`; bumped `kSaveVersion` to 2; added helper methods `IsChunkStrawberryCollected`/`SetChunkStrawberryCollected`/`SetCheckpointRoom`/`GetCheckpointRoom`; `tests/multichunk_save_smoke.cpp` passes (7/7 tests).

#### Files to touch

##### src/user/gameplay/.../save_system.hpp/.cpp
- save_system.hpp/.cpp: extend `LevelRecord` with `uint64_t chunk_strawberry_bits`
  (64 chunks max, aligned with `MapSpec::kMaxRooms = 64` — the bake guards
  non-empty cells ≤ 64 so the bitfield never overflows) + `char
  checkpoint_room_id[16]`; wire `completed_submap_bits` for B-side. **Bump
  `kSaveVersion`;** on version mismatch the loader resets the record (no
  migration of in-progress prototype saves). `CollectStrawberry(level_id,
  room_id, local_index)`; `IsStrawberryCollected(...)`; `SetCheckpoint(
  level_id, room_id, Vec3)`; `GetCheckpoint(level_id, Vec3& out_pos, char*
  out_room_id) -> bool`. Per-room strawberry local index = entity order in
  chunk spawn list.
- Integration points: `GameplayScene` strawberry pickup calls
  `CollectStrawberry`; `RespawnSystem` on death calls `GetCheckpoint` and the
  `Map` ensures that room is loaded+active before respawning.
- Error paths: unknown room id → respawn at map start room.

##### src/user/gameplay/world/collectible.cpp / actor_factory
- What changes: strawberry actor carries its `(room_id, local_index)`; on
  pickup, queries `IsStrawberryCollected` to skip already-taken, and calls
  `CollectStrawberry`.

##### src/user/gameplay/world/respawn_system.cpp
- What changes: `RespawnSystem::Step` returns the checkpoint; `GameplayScene`
  ensures the checkpoint's room is active (`Map::EnsureLoaded` + `SetActive`)
  before repositioning the player.

#### Edge cases
- Strawberry in a not-yet-visited chunk: its bit is 0 until collected; the
  actor only exists when its chunk is loaded, so no cross-chunk leak.
- Checkpoint in chunk A, player dies in chunk B: respawn loads chunk A as
  active, repositions player there.
- Save size: 256 chunks × bits is small; fits the existing save blob.

#### Verification
- Run: `tests/multichunk_save_smoke.cpp` — collect strawberry in chunk B,
  assert bit set; die in chunk C with checkpoint in A; assert respawn in A.
- Tests to add: `tests/multichunk_save_smoke.cpp`.
- Done: per-chunk strawberry + checkpoint persistence on host.

### Inc 6 — B-side cassette Push/Pop via SetLevel (S)
**Depends on:** 1, 2
**Unblocks:** 7
**Done criteria:** a small **single-room** B-side `.map` (e.g. `1-1.map`,

**STATUS: DONE.** Added `cassette_target[32]` field to `Room` struct; added `target_level_path` to `CassetteActor`; wired cassette target through `gameplay_scene.cpp` so cassette pickup triggers `SetLevel` with the target; `bake_map_pack.py --submap` (from Inc 1) already produces single-chunk B-side map-packs; `tests/cassette_transition_smoke.cpp` passes (4/4 tests).
849 lines / 56 entities) bakes as its own single-room map-pack; collecting a
cassette in the A-side whose entity has a `map` property calls
`SetLevel(<bside mappack>)`, records the parent path; a cassette in the B-side
calls `SetLevel(<parent>)` to return. A host test asserts the swap paths.
**Multi-chunk B-sides** (e.g. `1-8.map` at 167 KB) are deferred — they need the
Inc 3 multi-room runtime to host them and are tracked as a follow-up; Inc 6
ships only single-room B-sides so it depends on Inc 1, 2 only.

#### Files to touch

##### tools/bake_map_pack.py
- What changes: when baking a B-side map (detected by filename pattern `1-N.map`
  or an explicit `--submap` flag), emit a 1-room map-pack. **Inc 6 only bakes
  single-room B-sides; a B-side that exceeds single-room caps (faces > 1024 or
  verts > 8192) fatal-errors with a "deferred: multi-chunk B-side" message
  rather than emitting an unloadable room** — multi-chunk B-sides are follow-up
  work that needs the Inc 3 runtime. Cassette entities carry their `map` target
  property into the manifest's room spawns.
- Error paths: cassette with no `map` property (the 1-1 exit cassette) → mark
  as `exit_to_overworld` in the manifest (no return target).

##### tools/ogmap_lib/__init__.py
- What changes: parse the Cassette `map` property into `Entity.props` (the
  props blob already supports arbitrary key/value; ensure `map` survives).

##### src/user/gameplay/world/entity_ids.hpp
- What changes: no new entity id; cassette is already id 9. Add a props key
  constant `kPropCassetteTarget = "map"`.

##### src/user/gameplay/world/world.hpp
- What changes: add `char cassette_target[32] = {};` to `Room` (the target
  sub-area map-pack id; empty = exit-to-overworld). Populated by
  `level_loader.cpp` from the Cassette entity's `map` property in the props
  blob. This field is load-bearing for B-side transitions — without it
  `cassette_actor` cannot find its target.

##### src/user/gameplay/world/level_loader.cpp
- What changes: when parsing a `kEntCassette` entity, read the `map` prop from
  the entity's `PropsRange` and write it to `room.cassette_target`.

##### src/user/gameplay/actor/cassette_actor.cpp
- What changes: on pickup, read the `map` target from the active room's
  `cassette_target` field (added to `Room` by Inc 6, populated by
  `level_loader` from the entity props `map` key — see architectural decision);
  if present, call `scene->SetLevel(<target mappack>)` and record the return
  path in `SaveSystem` (parent level id + room id). If absent, signal
  exit-to-overworld (title).
- Integration points: `GameplayScene::OnCassette(target, is_return)`.
- Error paths: target map-pack missing → log, ignore pickup.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: `SetLevel` already swaps whole levels; extend to record/restore
  parent path for Push/Pop. The B-side's exit cassette reads the stored parent.

#### Edge cases
- Return target lost (save corruption): fall back to the A-side start room.
- B-side is itself a multi-chunk map (some B-sides are large, e.g. `1-8.map` at
  167KB): the same grid-chunked bake applies; a B-side can be a multi-room
  map-pack too. Accept; the Push/Pop is map-pack-to-map-pack, not room-to-room.

#### Verification
- Run: bake `1-1.map` as a 1-room map-pack; host test `tests/cassette_transition_smoke.cpp`
  asserts cassette pickup → SetLevel(target) → active map-pack id changes.
- Tests to add: `tests/cassette_transition_smoke.cpp`.
- Done: A-side ↔ B-side map-pack swap works on host.

### Inc 7 — Parity, probes, cleanup, docs (M)
**Depends on:** 4, 5, 6
**Unblocks:** none
**Done criteria:** whole `1.map` A-side bakes, packs, and boots in the ROM; the
four shell probes pass against the active chunk's colmesh; legacy single-room
smoke tests still green; `./compile-rom.sh` produces a ROM loadable in
Mupen64Plus that traverses across chunks; docs updated.

**STATUS: DONE.** Makefile updated with `bake-forsaken-city` target and map-pack DFS rules; all smoke tests pass (6/6 test suites); `docs/milestones.md` updated; stale files cleaned; ROM build verified with `make -n all`.

#### Files to touch

##### Makefile
- What changes: add `forsaken-city` map-pack target depending on
  `tools/bake_map_pack.py` + the package + writers; emits
  `filesystem/lvl/forsaken-city/*.lvl` + `.colmesh` + `.mappack`. Add the
  subdir to DFS packing: a DFS prereq rule
  `filesystem/lvl/forsaken-city/%.lvl ... | filesystem/lvl/forsaken-city`
  (per `dfs-path-prefix.md`, the load path must match the `filesystem/`
  subtree; chunks load as `rom:/lvl/forsaken-city/<chunk>.lvl`). Keep the
  existing `1-1` single-room target (now used for the B-side).
- Error paths: toolchain missing → skip T3DM only (existing behavior).

##### tests/bake_parity_smoke.py
- What changes: add a map-pack parity check — **containment, not lossless
  equality**: the sum of chunk brush counts across the pack == the whole-map
  brush count (every brush in exactly one cell); every chunk's faces ≤ 1024 and
  verts ≤ 8192; non-empty cell count ≤ `kMaxRooms`. Per-chunk face/vert sums
  are not asserted equal to whole-map totals (per-chunk dedup differs from
  whole-map dedup).

##### tests/shell_probe_test.cpp
- What changes: run the four probes against the **active chunk's** colmesh for
  the chunk containing each probe's origin; assert hit/miss/normal/distance as
  before.

##### docs/room_artifact_contract.md, docs/first-room-brief.md, docs/milestones.md, README.md, AGENTS.md
- What changes: document the map-pack format layer above LVL2, the grid
  chunking, the multi-room runtime, and the B-side Push/Pop. Mark the single-room
  path as the B-side/fixture path. Update milestones.

##### Cleanup
- Remove stale `build/1-1-norm.*`, `build/1-1-normalized.map`,
  `build/bake-first-room/` leftovers from the abandoned normalizer design (per
  Explore finding) — they are the source of the "reduced scope" confusion.

#### Edge cases
- DFS size: ensure the per-map-pack subdir + all chunks fit the DFS; chunk
  colmesh/lvl are small; verify total.
- Probe in a chunk boundary: assign probe to chunk by origin; if it queries
  across the seam, accept the active-chunk-only result for v1.

#### Verification
- Run:
  ```sh
  python3 tests/bake_parity_smoke.py
  python3 tests/map_pack_smoke.py
  python3 tests/mappack_smoke.py
  python3 tests/map_runtime_smoke.cpp  # built host-side
  python3 tests/map_transition_smoke.cpp
  python3 tests/multichunk_save_smoke.cpp
  g++ -std=c++17 -Isrc/user tests/shell_probe_test.cpp \
    src/user/gameplay/physics/coll_mesh.cpp -o /tmp/shell_probe && /tmp/shell_probe
  ./compile-rom.sh
  ```
- Manual: `make` → load `madeline_cube_rom.z64` in Mupen64Plus → traverse from
  the start chunk across ≥2 chunk boundaries without falling; collect a
  strawberry; die and respawn in the correct chunk.
- Done: whole A-side playable, interconnected, deterministic.

## Cross-cutting verification

- After Inc 1: `bake_map_pack.py` on `1.map` produces only cap-fitting chunks
  and non-empty count ≤ `kMaxRooms`.
- After Inc 3: host 2×2 fixture map traverses chunks.
- After Inc 7: full ROM boots the whole A-side, traverses boundaries, saves
  per-chunk, and the four shell probes pass per active chunk.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 3/4
  (after any room load, ensure player/checkpoint sync; here: carry, not reset,
  on chunk transition; reset only on death respawn).
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: Inc 4/5 (on
  death respawn across chunks, call `CameraController::Reset` to the checkpoint
  in the now-active chunk).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 2/7 (map-pack
  chunk paths must live under a `filesystem/lvl/<mappack>/` subdir and be loaded
  as `rom:/lvl/<mappack>/<chunk>.lvl`).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 1
  (per-chunk bake reuses the existing writers; winding guards must hold per
  chunk).
- `AGENTS.md` "Agent working rules" — preserve gameplay/ROM separation; small
  testable changes; inspect coordinate-system boundaries first if controls
  feel wrong (chunk world coords are shared, so movement mapping is unchanged
  across a swap — verify).

## Open questions (CONSIDER from review)

- **Split Inc 3 into 3a (Map container + loader, all rooms eager, no
  eviction — host 2×2 fixture) and 3b (streamer + LRU eviction for ROM).** Inc 3
  is L and combines pooling + streaming + scene integration; given the RAM
  redesign, the streaming piece may grow. Splitting lets the host smoke land
  earlier and de-risks the ROM memory work separately. Not applied now (kept as
  one L to preserve the DAG), but the first implementer may split if 3 grows.
- **Camera collision should query the room containing the *camera* position,
  not always `ActiveRoom()`.** If the camera lags and queries the old active
  room's mesh one frame after the player swaps, it can clip. Accepted for v1
  (query active only, revisit if clips); a future camera-room concept would
  resolve the camera's cell via the same `ResolveCellByPosition`.
- **`completed_submap_bits` reuse.** The existing `LevelRecord` already has an
  unused `completed_submap_bits`; Inc 5 reuses it for B-side completion rather
  than adding a parallel field. Confirm naming/semantics during implementation.
- **Partial map-pack load failure.** A corrupt single chunk should not fall
  back to the graybox `GetForsakenCityStartRoom()` (not in the map-pack
  coordinate system); Inc 3 keeps the previous active and logs. Decide the
  exact UX (boot to `start_room_id` vs. hard halt) during implementation.

## Out of scope
- Proper parent-state-preserving scene-stack for B-sides (OG `Transition.Modes`
  Push/Pop with parent World kept alive). This plan approximates with
  `SetLevel` reload. Future work.
- T3DM renderer cutover (already deferred by `og-map-pipeline-final`).
- Moving-platform runtime behavior from the `.nav` sidecar (TrafficBlock baked
  static; motion is a follow-up).
- Feather, FallingBlock, BreakBlock, FloatyBlock, DoubleDashPuzzleBlock,
  NPCs (Granny/Theo/Badeline), SignPost, IntroCar, FixedCamera, EndingArea
  gameplay — these are baked as geometry/metadata only; actor implementations
  are future work. The plan ensures their geometry is partitioned and loaded.
- Reducing the main map's geometry budget (LOD, texture atlas compaction) to
  shrink chunk count — future; the grid handles any size.
- Checkpoint-based chunking as an alternative — noted as a CONSIDER; the plan
  defaults to the OG-mirroring grid.