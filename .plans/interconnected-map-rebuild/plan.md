# Interconnected OG Map Pipeline Rebuild

## Context

The repository contains several generations of an OG `.map` conversion effort,
but they do not describe one reliable shipping path:

- the early plans targeted the small B-side `1-1.map` as a single room;
- the library-first pipeline consolidated parsing, class policy, LVL2, colmesh,
  and navigation output for that small map;
- `whole-interconnected-map` added a grid bake and a multi-room runtime for the
  main A-side `1.map`;
- `interconnected-map-fixup` corrected the grid's second axis from Quake
  `map_z` (up) to world Z (depth) and made boot/spawn and stale-colmesh behavior
  deterministic;
- `open-world-conversion` then treated the remaining fall-through as a
  per-chunk winding/ABI problem.

The current working tree proves only part of the intended result. The host bake
of `assets/og_converted/maps/1.map` produces 47 visual rooms at
`--chunk-size 1200`, with 1182 source brushes accounted for, every room under
the current LVL caps, and a global collision build of 10,596 triangles / 8,191
BVH nodes that the runtime loader can read. The existing 43 per-room collision
sidecars also pass their host raycast audit, but they are not sufficient to
guarantee continuous collision at room seams. The host `Map`/manifest tests
also pass.

The full ROM path is still structurally broken. `GameplayScene::Update` and
`Render` continue to use the legacy single `Room`, `room_renderer`, and
`ActorWorld` for physics, camera, respawn, actors, and drawing. The new `Map`
object is loaded beside that state and only mirrors selected fields such as
`coll_mesh`. The per-room renderer stored by `Map` is not the renderer drawn by
the scene. Host map tests do not compile or exercise this scene integration.
There are additional full-world hazards: `LvlRoomRenderer` silently caps at
512 batches while chunks may contain up to 1024 faces, absolute world
coordinates overflow its int16 fixed-point packing, and the current map-pack
does not preserve named spawn semantics or a source/artifact
compatibility identity.

This plan rebuilds the conversion boundary and the runtime handoff around one
canonical world representation. It targets the complete Forsaken City A-side
`1.map` as one traversable world: one global static collision mesh, chunked
LVL2 visual/entity rooms, and a manifest that ties them together. Existing
placeholder rendering and actor systems are retained where they already work.
B-side maps, moving-platform behavior, and final art parity remain separate
follow-ups.

## Architectural decisions

- **Decision: keep the existing LVL2 and CMSH v1 binary formats initially.**
  They already load on host and N64, and changing both formats would combine a
  conversion rewrite with an unrelated runtime serialization migration. The
  new map-pack manifest gets an explicit version 2 and carries the metadata the
  current manifest loses. A format change is allowed later only if validation
  proves v1 cannot represent the required data.

- **Decision: direct `.map` → canonical world IR → global collision + visual
  chunk artifacts.** No
  normalization pass, texture suffix rewrite, or separate geometry algorithms
  for LVL and colmesh. The existing parser remains the source parser, but a new
  world IR records source entity/brush/face identity, transformed polygon,
  material flags, collision mode, render mode, and stable spawn identity. LVL,
  colmesh, diagnostics, and the manifest are all emitted from that IR.

- **Decision: emit one global CMSH for static collision.** The complete
  collision triangle set is built once from the world IR and loaded once. The
  current measurement is about 383 KB on disk and roughly 250 KB for its
  pre-dequantized vertices, plus BVH/loader overhead; this exceeds the old
  per-file advisory budget but is still within the N64 memory budget candidate
  and must be measured on hardware. A global mesh removes room-seam ownership,
  empty-cell fall-through, and brush-overhang gaps from the collision path.
  If the hardware memory gate fails, stop before shipping and design a sector
  fallback with an explicit player-radius halo; do not silently restore
  center-owned collision.

- **Decision: partition only visual geometry and room-local entities.** A brush
  may appear in every visual cell whose world XZ column intersects its AABB so
  chunk rendering has no seam holes. Point entities are assigned to exactly one
  cell. Static collision is never duplicated per visual chunk.

- **Decision: active-only visual-room loading for the first reliable traversal.**
  The runtime loads the next visual room into staging, validates it, then
  atomically swaps the renderer and room-local actor/spawn view. The global
  collision mesh remains resident and unchanged across the swap. It does not
  begin with an LRU pool or a neighbor ring. Those optimizations can be added
  after the one-room authoritative path passes on hardware.

- **Decision: one authoritative active-room view plus one global collision
  view.** Gameplay and camera receive the active room for dynamic surfaces,
  room metadata, and actor state, while all static queries use the global mesh
  through the same `WorldCollision` object. The legacy `Room` is not
  field-by-field mirrored. A visual-room transition occurs after a fixed-step
  movement; if it fails, the old visual room remains active while collision
  continues safely in world space.

- **Decision: chunk-local rendering coordinates.** Collision and player state
  remain in shared world coordinates. Each visual chunk manifest stores a
  render origin; the renderer subtracts it before packing vertices, and the
  scene renders the player, camera, and actors in the same local frame. This avoids
  the current `kPosScale=32` int16 overflow on the full map's absolute
  coordinates while preserving world-space movement and chunk resolution.

- **Decision: named spawn/checkpoint data is manifest data, not inferred from
  LVL entity order.** The current loader overwrites `player_start` for every
  PlayerSpawn and therefore cannot reliably identify the OG's named Start and
  anchor records. Map-pack v2 stores stable spawn records (kind, source id,
  room id, world position, and name/flags). `Start` is the initial/default
  checkpoint for this source map; other anchors become checkpoints only when a
  future trigger/save record explicitly selects them. LVL actor entities remain
  available for existing dispatch, but respawn routing uses explicit records.

- **Decision: every source brush class has an explicit policy.** The registry
  must state whether a class contributes static collision, visual geometry,
  actor-owned geometry, trigger data, or is intentionally unsupported. Unknown
  brush classes are reported and fail strict full-world baking; they are never
  silently dropped. Dynamic OG classes may use a documented static collision
  proxy for this collision-first milestone, with their dynamic behavior called
  out in the report.

## Question gate: answers and assumptions

- **Scope from the user:** “full interconnected map” is interpreted as the main
  Forsaken City A-side `1.map`, not the separate `1-1`…`1-10` B-side maps. The
  existing map inventory confirms `1.map` is the 706-entity, 1182-brush world;
  `1-1.map` is a small separate sub-area.
- **Output from the user:** the first success criterion is complete colmesh
  collision and traversable chunk transitions. Existing LVL2/placeholder
  rendering is kept as the compatibility artifact rather than making T3DM art
  a prerequisite.
- **Stack from repository guidance:** retain libdragon + tiny3d and the
  gameplay/ROM separation; do not port the original C# runtime wholesale.
- **Answered from code:** the canonical Quake→world transform is
  `(map_x*s, map_z*s, -map_y*s)` in `tools/ogmap_lib/__init__.py:382-390`.
- **Answered from code:** the current runtime caps are 1024 LVL faces and 8192
  LVL vertices in `src/user/gameplay/world/level_loader.hpp:19-24`.
- **Answered from code:** the current map-pack uses world-XZ resolution, but
  `GameplayScene` still routes its hot path through `impl_->room` at
  `src/user/gameplay/scene/gameplay_scene.cpp:602-685`.
- **Answered from code:** `LvlRoomRenderer` stores only 512 batches at
  `src/user/gameplay/render/lvl_room_renderer.hpp:29`, while the bake permits
  1024 faces.
- **Answered from code:** the host bake and colmesh audit currently pass; this
  is evidence to preserve during the rebuild, not evidence that the ROM path is
  complete.

## Risks accepted

- **The global collision mesh exceeds the old 256 KB advisory file budget.**
  The measured candidate is about 383 KB on disk and under 1 MB with loaded
  vertex/BVH structures. Inc 5 measures a host estimate; Inc 10 measures the
  actual N64 resident footprint and query behavior before acceptance. If it
  fails, the plan stops for a sector design rather than silently shipping an
  over-budget ROM.
- **Synchronous visual-room loads may hitch.** Accept for the first correct ROM path;
  add preloading only after active-only traversal is stable and memory is
  measured.
- **Static proxies are not OG gameplay parity.** The report labels dynamic
  classes and the plan does not claim moving-platform behavior until actors and
  nav data are implemented.
- **Render rebasing can diverge from collision coordinates.** A host transform
  test and a ROM seam traversal verify that the global collision remains
  world-space while only visual submission is rebased.
- **The old plans and generated files are inconsistent historical evidence.**
  The new pipeline uses source hashes, deterministic manifests, clean staging,
  and explicit reports so stale artifacts cannot masquerade as a successful
  bake.
- **Original Celeste64 content rights remain separate from source-code rights.**
  This plan uses the repository's existing reference assets for local
  development and does not define a public redistribution policy.

## Increment DAG

- Inc 1 — Reproduction gate and fixture contract (M) — depends on: none —
  unblocks: 2, 3
- Inc 2 — Canonical OG world IR and class policy (M) — depends on: 1 —
  unblocks: 3, 4
- Inc 3 — Canonical global collision + visual-room geometry builder (L) — depends on: 2 —
  unblocks: 4, 5
- Inc 4 — Deterministic global CMSH/LVL/map-pack v2 writer (L) — depends on: 3 —
  unblocks: 5, 6
- Inc 5 — Whole-map offline validation and budgets (M) — depends on: 4 —
  unblocks: 6, 8
- Inc 6 — Global collision + authoritative visual-room runtime core (L) — depends on: 1, 4, 5 —
  unblocks: 7
- Inc 7 — Global collision/query and scene integration (M) — depends on: 6 —
  unblocks: 8, 9
- Inc 8 — Chunk-local renderer and checked visual integration (M) — depends
  on: 7 — unblocks: 10
- Inc 9 — Actor identity and save/transition integration (M) — depends on: 7,
  8 — unblocks: 10
- Inc 10 — ROM packaging, hardware traversal, and old-pipeline retirement (M)
  — depends on: 5, 8, 9 — unblocks: none

## Increments

### Inc 1 — Reproduction gate and fixture contract (M) — DONE

**Depends on:** none  
**Unblocks:** 2, 3  
**Done criteria:** a deterministic host report and a small 2×2 fixture describe
the target world/chunk contract and reproduce the current scene-integration
gap. The report records that the future v2 boot gate requires a global
collision artifact; the actual boot gate is implemented and tested in Inc 6.

#### Files to touch

##### tests/fixtures/interconnected-2x2.map

- What changes: add four tiny adjacent rooms with a floor crossing each seam,
  one overhanging brush, one named Start anchor, and one actor
  spawn. The fixture is intentionally small enough for host tests and exercises
  both positive and negative world-Z cells.
- Data shapes: ordinary Quake `.map`; source ids are derived from entity and
  brush order and recorded by the report.
- Error paths: malformed fixture geometry must fail the fixture parser test,
  not silently produce an empty chunk.

##### tests/interconnected_map_contract.py

- What changes: run the current parser/bake inventory against `1.map`, record
  entity/brush/class counts and source SHA-256, and assert the fixture's four
  cells, seam coverage requirements, and named spawns. This increment remains
  baseline-only; it does not import the Inc 2 IR or the Inc 6 runtime API.
- Function(s): `main()`, `load_report()`, `assert_fixture_contract()`.
- Integration points: uses the existing parser and current bake inventory only;
  Inc 2 must later make its report match this baseline.
- Error paths: missing source, stale output hash, missing class policy, or
  missing fixture room is a hard failure.

#### Verification

- Run: `python3 tests/interconnected_map_contract.py` and the fixture host
  smoke build.
- Done: the report records `1.map` as 706 entities / 1182 brushes, the fixture
  has four connected cells, and the active-room contract has a regression test.

### Inc 2 — Canonical OG world IR and class policy (M) — DONE

**Depends on:** 1  
**Unblocks:** 3, 4  
**Done criteria:** the full source map can be parsed once into a deterministic
IR in which every emitted polygon, collision triangle, actor spawn, and skipped
class has a source identity and explicit policy.

#### Files to touch

##### tools/ogworld/model.py

- What changes: add immutable records for `SourceBrush`, `SourceFace`,
  `WorldPolygon`, `CollisionTriangle`, `SpawnRecord`, `ChunkInput`, and
  `WorldBuild`. Include source entity index, brush index, face index, class,
  texture, transformed normal, material flags, render/collision modes, and
  stable ids.
- Data shapes: world positions use `(x,y,z)` floats with Y-up; cell keys are
  `(ix, iz)` in world XZ; source coordinates remain available for diagnostics.
  Preserve the writer inputs that are currently recomputed later: UVs,
  texture-manifest/material ordering, serialized entity properties, and the
  exact classname/entity representation used by LVL2.
- Error paths: duplicate stable ids, unsupported brush classes in strict mode,
  invalid normals, and non-finite coordinates are fatal.

##### tools/ogworld/class_policy.py

- What changes: move the full-world class mapping into a table used by all
  writers. Separate `collision_mode` from `render_mode`; explicitly label
  `TrafficBlock`, `FallingBlock`, `MovingBlock`, `FloatyBlock`, and other
  dynamic classes as static-proxy or actor-owned rather than silently treating
  them as worldspawn.
- Function(s): `policy_for(classname)`, `validate_policies(parsed_map,
  strict=True)`, `summarize_policies()`.
- Error paths: unknown brush-bearing class in strict full-world mode fails with
  entity/class/source references.

##### tools/ogworld/parse.py

- What changes: adapt the proven `ogmap_lib.parse_map` output into the IR,
  preserving entity properties such as spawn names and cassette targets. Do not
  normalize entities into fake `func_wall` classes.
- Integration points: existing `tools/ogmap_lib/__init__.py` parser and brush
  geometry functions remain reusable primitives.
- Error paths: invalid brushes are retained in diagnostics and excluded only
  under an explicit policy; strict mode fails the build.

#### Verification

- Run: `python3 tests/interconnected_map_contract.py` and a new IR unit test.
- Done: two independent IR builds from the same source have identical stable
  ids, policy summaries, and serialized report content.

### Inc 3 — Canonical global collision + visual-room geometry builder (L) — DONE

**Depends on:** 2  
**Unblocks:** 4, 5  
**Done criteria:** the global collision IR contains every policy-approved static
surface exactly once, visual room inputs have seam coverage, point entities
occur in exactly one room, and the global memory/triangle budget is estimated.

#### Files to touch

##### tools/ogworld/chunking.py

- What changes: partition visual geometry by world-XZ grid using the canonical
  transform and assign clipped visual polygons to every cell whose column
  intersects its AABB, not only the center cell. Compute only the finite
  candidate cell range. Large `DeathBlock`/trigger volumes remain global
  collision/trigger metadata and are not blindly duplicated into dozens of
  render rooms; visible oversized solids are clipped per cell or fail the
  configured visual-cell budget with a source-specific diagnostic.
  Keep point entities on their origin cell and keep actor records separate from
  duplicated visual geometry. Collision geometry is not partitioned here.
- Function(s): `world_cell(point, chunk_size, scale)`,
  `cells_intersecting_aabb(aabb, chunk_size, scale)`,
  `partition_world(build, chunk_size)`, `build_adjacency(cells)`.
- Error paths: an unclip-able brush spanning more than a configured maximum
  number of visual cells, empty/invalid geometry, or a visual cell over the
  runtime cap produces a diagnostic with source ids and suggested chunk sizes.
  The real `1.map` is preflighted with overlap/clipping before the writer is
  allowed to publish, so the old 47-room center-ownership measurement cannot
  be mistaken for proof that the new partition fits.

##### tools/ogworld/collision.py

- What changes: collect all policy-approved static collision surfaces into one
  global `CollisionScene`, preserving source identity and transformed normals.
  Do not split by visual room or assign collision by brush center.
- Function(s): `build_global_collision(world_ir, scale, eps)`,
  `collision_budget(scene)`, `validate_global_coverage(scene)`.
- Data shapes: one ordered triangle stream plus material/source metadata; the
  stream is later serialized as `forsyken-city.colmesh`.
- Error paths: unsupported solid class, invalid/degenerate surface, missing
  coverage, triangle/BVH limits, or a host-estimated runtime footprint over the
  declared preflight threshold are fatal. Hardware-resident allocation is an
  Inc 10 gate.

##### tools/ogworld/geometry.py

- What changes: compute brush polygons once in the canonical IR, orient them
  from the transformed face normal, deduplicate vertices, fan-triangulate, and
  attach source/material identity. The global collision list and duplicated
  visual room inputs derive from these same polygons.
- Function(s): `build_world_geometry(world_ir, scale, eps)`,
  `validate_polygon()`, `validate_triangle_normal()`.
- Error paths: degenerate triangles, non-upward death filters, NaN values, and
  quantization-risk ranges are reported per source face.

##### tools/bake_interconnected_map.py

- What changes: new orchestration entrypoint that parses once, builds the IR,
  searches candidate chunk sizes, and produces an in-memory `WorldBuild` before
  any output directory is touched.
- CLI: `<map> --out-dir DIR --scale 0.2 --chunk-size N [--auto-chunk-size]
  [--strict] --mappack-id forsyken-city`.
- Error paths: global collision budget failure, no visual chunk size fits,
  room count exceeds `kMaxRooms`, or any source geometry is unowned is fatal
  before publish.

#### Edge cases

- Large brushes can be duplicated into multiple visual cells; the report must
  show source brush → cell count and the largest duplication multiplier. They
  must occur exactly once in global collision.
- Brushes that touch a seam within epsilon belong to both sides; use a stable
  epsilon policy and test exact seam coordinates.
- Visual-only cells may have no local collision because collision is global;
  the manifest must still identify them as non-playable decoration/hazard
  cells.

#### Verification

- Run the 2×2 fixture with an overhanging brush and ray/sweep on both sides of
  every seam against the single global collision scene.
- Done: no seam probe depends on a visual room's collision sidecar, and every
  visual chunk's source coverage is explainable in the report.

### Inc 4 — Deterministic global CMSH/LVL/map-pack v2 writer (L) — DONE

**Depends on:** 3  
**Unblocks:** 5, 6  
**Done criteria:** one staged invocation emits one global CMSH, all visual chunk
LVL2 artifacts, a versioned map-pack manifest, and a source-hash report with no
stale files or timestamp-dependent bytes.

#### Files to touch

##### tools/writers/colmesh_world_writer.py

- What changes: write one CMSH v1 from the global collision triangle stream,
  including quantization, BVH construction from quantized positions,
  face/material ids, and a runtime-query audit sidecar mapping triangle ids to
  source ids. Record that sidecar's path/hash as host diagnostics in the
  manifest (it need not ship in the ROM). The expected current reference is
  approximately 20,824 vertices,
  10,596 triangles, and 8,191 BVH nodes.
- Function(s): `write_colmesh(global_collision, path)`,
  `audit_quantized_mesh(global_collision, path)`.
- Error paths: invalid quantization range, int16 overflow, triangle index
  overflow, BVH range overflow, runtime-memory budget failure, or a post-load
  normal mismatch is fatal.

##### tools/writers/lvl_world_writer.py

- What changes: write one LVL2 visual/entity room per visual cell from the
  canonical polygons. Preserve entity properties in the props blob where the
  runtime needs them, but do not use LVL entity order for Start/anchor routing.
- Integration points: existing `tools/lvl_format.py` and `entity_ids.py`.
- Error paths: face/vertex caps, missing material ids, malformed props, and
  actor count overflow are fatal.

##### tools/mappack_format.py

- What changes: add map-pack v2 with source-map SHA, pipeline version, scale,
  chunk size, one global collision path/hash/counts, per-visual-room cell
  indices, render origins, world AABBs, artifact hashes/counts, neighbor ids,
  and a fixed/offset spawn table containing named Start/anchor/actor records.
- Data shapes: binary remains fixed-width/big-endian for N64; JSON mirrors it
  for host diagnostics. Define the v2 header, hash encoding, string limits,
  offset alignment, spawn-table record widths, checksum/version policy, and
  malformed/truncated-file behavior in the format module. v1 readers reject
  v2 explicitly rather than guessing.
- Error paths: truncated strings, invalid room references, duplicate cells,
  hash mismatch, and room count > `MapSpec::kMaxRooms` fail load.

##### tools/artifact_hash.py and src/user/gameplay/world/artifact_hash.{hpp,cpp}

- What changes: define one small deterministic content hash (CRC32 or the
  repository's existing fixed-width hash primitive) over each artifact and
  the source map. The writer records hash plus byte size; the N64 loader uses
  the same implementation to reject stale/truncated DFS files. Human-readable
  reports may additionally include SHA-256, but the runtime contract does not
  depend on a host-only hash.
- Error paths: short reads, hash mismatch, and size mismatch reject the pack
  before exposing a staged room.

##### Makefile

- What changes: replace the multi-target pattern-rule bake with one explicit
  `bake-forsaken-city` staging command. Inc 4 writes v2 only to an isolated
  staging directory; the existing published v1 directory is not switched
  until the v2 reader lands in Inc 6. Once switched, publish the complete
  directory only after the manifest and every referenced artifact exist,
  using a validated directory swap and no suppressed copy failures.
- Integration points: DFS paths remain under
  `filesystem/lvl/forsyken-city/`, matching `rom:/lvl/forsyken-city/...`.
- Error paths: partial bake, missing global CMSH, missing visual room, or stale
  source hash prevents DFS publication.

#### Verification

- Run two clean bakes into separate directories and compare every published
  byte except intentionally human-readable diagnostics.
- Done: no generated manifest timestamp changes the build; the pack references
  exactly the files that are staged for DFS.

### Inc 5 — Whole-map offline validation and budgets (M) — DONE

**Depends on:** 4  
**Unblocks:** 6, 10  
**Done criteria:** the complete `1.map` build passes geometry, collision,
  reachability, spawn, seam, and artifact-integrity checks before ROM work.

#### Files to touch

##### tests/interconnected_map_smoke.py

- What changes: assert global collision source coverage exactly once, visual
  source coverage with declared seam duplication, global memory/triangle/BVH
  budgets, visual room caps, artifact hashes, deterministic output, explicit
  class-policy counts, dynamic-proxy counts, and named spawn records (11
  PlayerSpawns including exactly one `Start`, 20 Strawberries, 10 Cassettes,
  plus all Refill/Spring and brush-bearing classes).
- Error paths: any unowned brush, silent class drop, stale artifact, missing
  collision for a traversable cell, or count mismatch fails.

##### tests/interconnected_collision_smoke.cpp

- What changes: load the single emitted CMSH with the real runtime loader and
  run floor, wall, ceiling, death, and seam probes from the fixture plus
  selected full-map anchors. Verify post-quantization normals, maximum position
  error, material flags, source diagnostics, and host-estimated resident
  memory. The actual N64 allocation gate is performed in Inc 10.
- Error paths: a host pass with inconsistent serialized data is a failure, not
  a reason to waive the check.

##### tests/interconnected_reachability.py

- What changes: BFS the manifest's ±X/±Z graph from the named Start room and
  require every collision-bearing room to be reachable. Decoration-only or
  isolated hazard cells must be explicitly classified and listed, never
  silently counted as playable world.

##### docs/room_artifact_contract.md

- What changes: document the canonical IR, overlap ownership, map-pack v2,
  source hashes, render origins, and the distinction between world-space
  collision and chunk-local rendering.

#### Verification

- Run all three tests against a clean `1.map` build.
- Done: the global collision mesh covers every policy-approved solid brush;
  every visual room containing traversable geometry is reachable from
  `cell_00_00`; visual-only/isolated cells are explained by policy; and every
  seam in the traversable graph has a global collision hit on both sides.
  Every source solid that is not Start-reachable must either be proven by the
  class policy to be intentional non-playable decoration/hazard geometry or
  fail the full-world gate; an unexplained disconnected solid is not accepted
  as a successful interconnected map.

### Inc 6 — Global collision + authoritative visual-room runtime core (L) — DONE

**Depends on:** 1, 4, 5  
**Unblocks:** 7  
**Done criteria:** a host runtime can load map-pack v2 and the one global CMSH,
  transition visual rooms across a fixture and real-map seam, preserve player
  world position/velocity, and continue static collision without swapping or
  nulling the global mesh.

#### Files to touch

##### src/user/gameplay/world/mappack_loader.{hpp,cpp}

- What changes: read/validate map-pack v2, including source hash/version,
  global collision path/hash/counts, explicit visual-cell indices, render
  origins, room artifact counts, and spawn table.
- Error paths: reject incompatible or incomplete packs before gameplay starts;
  never fall back to a graybox room in the map's coordinate system. Full-world
  boot must halt visibly when the requested v2 pack fails; legacy fallback is
  permitted only when no map-pack was requested at all.

##### src/user/gameplay/world/map_runtime.{hpp,cpp}

- What changes: replace the parallel legacy `Map` pool with a runtime that
  owns one global `WorldCollision` plus one staged/active visual room. Expose
  `ActiveRoom()`, `ActiveRoomId()`, `GlobalCollision()`, `RenderOrigin()`,
  `ResolveCellByPosition()`, and explicit spawn/checkpoint lookup.
- Data shapes: `ActiveRoomView` owns room metadata, dynamic colliders,
  renderer handle, room id, cell key, render origin, and actor records;
  `WorldCollision` owns the global CMSH for the map lifetime. No borrowed
  pointer to an evictable per-room collision mesh is exposed.
- Error paths: failed visual transition leaves the old visual room active and
  reports the requested room/position; global static collision remains valid.

##### src/user/gameplay/world/world.{hpp,cpp}

- What changes: introduce the explicit `WorldCollision` ownership/query
  boundary. It owns and frees the one loaded CMSH while active-room dynamic
  colliders are queried alongside it. Add a non-owning `CollisionContext` (or
  equivalent view) for world-space static queries; keep temporary legacy
  `Room` overloads only for the old single-room tests.
- Integration points: `RaycastRoomSource`, floor/ceiling/wall queries,
  `PlayerMotor`, camera collision, and respawn probes must all resolve static
  geometry through this same global mesh lifetime. A v2 active room may expose
  a compatibility pointer to the global mesh, but room cleanup never frees or
  replaces it.
- Error paths: an active room without a valid global collision view fails map
  boot; a visual-room load failure cannot null or replace the global view.

##### src/user/gameplay/player/player_motor.{hpp,cpp} and physics contracts

- What changes: route `Step`, `RefreshContacts`, climb/death material checks,
  sweep probes, and wall queries through the global collision view plus the
  active room's dynamic state. Preserve a legacy adapter for existing graybox
  tests until the v2 scene path is complete.
- Error paths: missing global collision is an explicit boot/runtime error, not
  the current permissive “no mesh” behavior that lets the player fall through.

##### src/user/gameplay/world/respawn_system.{hpp,cpp}

- What changes: consume an explicit spawn/checkpoint record and room id rather
  than a copied `Room::checkpoint` field. The map's `Start` record is the
  initial spawn and default checkpoint. The current source has no dedicated
  `Checkpoint` entity, so the other named `PlayerSpawn` records are anchors,
  not automatically activated checkpoints; a future checkpoint trigger must
  name one of them before save state may select it.
- Error paths: missing/duplicate `Start`, unknown checkpoint name, or a
  checkpoint whose room artifact is absent fails validation/respawn instead of
  silently using the last LVL entity's position.

##### src/user/gameplay/world/map.{hpp,cpp}

- What changes: retire the current per-room collision ownership, three-slot
  LRU loading, and field-mirroring helpers from map-pack v2. Keep any
  single-room compatibility path needed by older tests, but it must not be
  reachable from the v2 full-world boot path.
- Error paths: attempting to load or free a room-local `.colmesh` for a v2
  pack is a hard diagnostic; it must not create a second static collision
  authority.

##### src/user/gameplay/world/level_loader.{hpp,cpp}

- What changes: add checked loading into a staging room/view and validate every
  LVL face range, vertex range, entity range, and props range before commit.
  Load the global CMSH once from the manifest path and validate its hash/counts.
- Integration points: the global CMSH path is never inferred from a per-room
  `.lvl` filename.

##### tests/map_runtime_v2_smoke.cpp

- What changes: host-test global collision load, staged visual-room failure
  rollback, active-room identity, world position/velocity carry, explicit
  Start/anchor lookup, default-checkpoint routing, and cleanup.
- Done: the test passes with a missing visual-room artifact while global
  collision remains queryable and the old visual room stays active.

##### tests/fixtures/malformed-map-pack-v2/

- What changes: add small negative fixtures for duplicate cells, missing
  neighbors, truncated LVL/CMSH data, invalid offsets, hash mismatches, and a
  stale source hash. Each must fail before active-room commit.

### Inc 7 — Global collision/query and scene integration (M) — DONE

**Depends on:** 6  
**Unblocks:** 8, 9  
**Done criteria:** every gameplay query uses the global static collision plus the
active room's dynamic state, and the host scene adapter commits the active view
after movement without stale-room collision or legacy fallback.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp

- What changes: remove the `impl_->room` mirror path for map-pack mode. Route
  `PlayerMotor`, `AdvanceMovingSurfaces`, `RespawnSystem`, camera collision,
  actors, telemetry, and render selection through the runtime's global
  collision + active visual-room interfaces. Check and commit visual-room
  transitions after each fixed-step movement before the next room-local query.
- Error paths: a visual transition failure keeps the old room and global
  collision active; respawn loads the explicit checkpoint visual room before
  resetting player/camera. A requested full-world pack failure is fatal and
  cannot enter the legacy single-room fallback.

##### src/user/gameplay/player/camera_controller.{hpp,cpp}

- What changes: accept the global collision view plus active-room dynamic
  state, query collision in world space, and expose a render-space conversion
  only at the rendering boundary.

##### tests/active_room_scene_contract.cpp

- What changes: exercise an instrumented scene adapter that records the room
  pointer/id used by motor, camera, respawn, actor, and renderer calls; assert
  they all match the active view before and after a transition.

##### tests/scene_map_path_smoke.cpp

- What changes: verify global collision is required before `Step`, failed
  visual-transition rollback, two-cell dash crossing, and that the next
  physics tick uses the committed active-room view. This is still a host
  adapter test, not an N64 renderer test.

### Inc 8 — Chunk-local renderer and checked visual integration (M) — DONE

**Depends on:** 7  
**Unblocks:** 10  
**Done criteria:** every validated visual room loads with a bounded local
render frame, all faces are either drawn or rejected before submission, and a
transition never exposes partially parsed geometry.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}

- What changes: load with a per-room render origin, subtract that origin before
  fixed-point packing, and make batch storage cover all validated faces (or
  reject an artifact instead of silently truncating at 512). Add a counter for
  discarded faces that must remain zero. Validate file size, every offset and
  range, and the relationship between loaded vertex count and every face fan;
  split oversized face batches or reject the artifact rather than clamping
  vertices while drawing the original fan.
- Error paths: coordinate pack overflow, face range overflow, malformed/truncated
  input, and batch count overflow fail renderer load.

### Inc 9 — Actor identity and save/transition integration (M) — DONE

**Depends on:** 7, 8  
**Unblocks:** 10  
**Done criteria:** all source actor spawns have independent stable identities,
visual-room transitions do not duplicate or reset collected actors, and the
Start/default checkpoint record survives death and reload.

##### src/user/gameplay/actor/actor_world.{hpp,cpp} and entity dispatch

- What changes: dispatch actors from the active room's explicit spawn records,
  destroy/rebuild room-local actors at a transition, and preserve global save
  identity so visual overlap cannot create duplicate pickups. Add a stable
  source/entity id to every actor instance, make `ActorWorld` own or otherwise
  retain one independent instance per source spawn (the current factory
  reuses one Strawberry/Refill/Spring object), and make transition teardown
  deterministic.
- Integration points: `actor_factory`, `entity_dispatch`, actor headers, and
  `save_system` must carry stable ids. Strawberry collection uses those ids,
  not room-local array order; overlapping visual geometry never duplicates an
  actor record.
- Error paths: duplicate source ids, more actors than the fixed runtime/save
  capacity, or a missing save bit for a collected actor fail the full-world
  validation.

#### Edge cases

- A player crosses two cells during one dash: resolve the final cell, stage it
  before the next physics tick, and retain world position/velocity.
- A future named checkpoint lies in another room: load that room first, then
  reset player and camera to the saved record; do not use the new room's
  default spawn. For the current source, `Start` remains the default because
  there is no checkpoint entity.
- Visual overlap duplicates a brush: geometry may duplicate, global collision
  and actor/spawn records may not.

### Inc 10 — ROM packaging, hardware traversal, and old-pipeline retirement (M) — DONE

**Depends on:** 5, 8, 9  
**Unblocks:** none  
**Done criteria:** a clean ROM boots the full A-side from the manifest Start
  spawn, traverses every Start-reachable visual room across X and world-Z/depth seams,
  renders each active chunk without fixed-point overflow/truncation, and
  respawns at the explicit `Start` checkpoint (or a named checkpoint only when
  a trigger/save record selected one).

#### Files to touch

##### src/user/rom_main.cpp

- What changes: keep the full A-side map-pack as the explicit boot target and
  remove diagnostic assumptions that still select the single-room path.

##### Makefile, compile-rom.sh, AGENTS.md, README.md

- What changes: after the v2 reader is present, make the new bake target the
  only source of the published Forsaken City directory, document the clean
  bake/build commands, and state that old map-pack artifacts are invalid after
  a source or pipeline-version change. Add the actual N64 memory report/gate
  for the resident CMSH, staging renderer, actor storage, and query scratch
  buffers; the measured total must fit the declared hardware budget.

##### tests/rom_traversal_acceptance.md

- What changes: record the exact Ares/Mupen64Plus procedure and expected logs:
  boot room/start position, active room changes, collision normal, no-null-mesh
  transitions, render-origin values, global-mesh identity, and checkpoint
  respawn.

#### Verification

- Run the complete host suite, `./compile-rom.sh`, then launch under Ares (or
  the available emulator).
- Execute a deterministic replay/route that visits every Start-reachable room
  and crosses every reachable manifest edge with player-radius floor/wall/
  sweep probes; include at least one +X and one world-Z/depth seam, cross back,
  collect an actor in a non-start room, fall/die, and verify the explicit Start
  checkpoint is restored. Any source solid classified as playable must be on
  that route or fail acceptance.
- Done: no fall-through, no stale-room collision, no renderer truncation, the
  measured N64 global-collision memory gate passes, and no artifact/hash
  mismatch exists in the ROM's DFS.

## Cross-cutting verification

- `python3 tests/interconnected_map_contract.py`
- `python3 tests/interconnected_map_smoke.py`
- `python3 tests/interconnected_reachability.py`
- host CMSH loader/raycast audit over the single emitted global collision mesh
- host active-room and scene-contract tests
- `./compile-rom.sh`
- Ares/emulator traversal with captured telemetry

The final acceptance gate is hardware traversal, not merely “47 visual files
and one CMSH were generated” or “the manifest loader test passed.”

## Standards / common-mistakes referenced

- `AGENTS.md` — preserve gameplay/ROM separation; inspect coordinate-system
  boundaries; rebuild ROM after N64-facing changes.
- `.agents/map-creation.md` — canonical transform, artifact dependencies,
  material/entity id synchronization, and fixed-point constraints.
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to Inc 2–5;
  validate transformed winding and quantized runtime normals.
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to Inc 4 and 10;
  manifest paths must match the DFS subtree exactly.
- `.agents/common-mistakes/missing-player-start-init.md` — applies to Inc 6–10;
  distinguish initial spawn, transition carry, and checkpoint respawn.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to Inc 7–10;
  reset camera only after the checkpoint room is active.

## Open questions (CONSIDER from review)

- After the active-only visual path is stable, measure whether one preloaded
  visual neighbor removes visible hitches without exceeding N64 memory; do not
  add the pool before the authoritative path is correct.
- Decide whether dynamic OG classes should remain static collision proxies or
  gain actor-owned collision before shipping public gameplay.
- If the global CMSH fails the hardware memory gate, prototype overlapping
  collision sectors with a player-radius/sweep-step halo before changing room
  caps or restoring center-owned collision.
- Revisit T3DM/textured rendering after collision and placeholder rendering are
  stable; it is not a prerequisite for this plan.

## Out of scope

- B-side `1-1`…`1-10` map-pack conversion and cassette scene-stack semantics.
- Full OG gameplay parity for moving/falling/breakable blocks, NPCs, cutscenes,
  fixed cameras, and navigation runtime.
- T3DM renderer cutover and final original-art redistribution.
- Increasing N64 room/face caps just to make the current center-owned bake fit.
- Porting the original Celeste64 C# runtime wholesale.
