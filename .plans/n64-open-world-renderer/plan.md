# N64 Seamless Open-World Renderer

## Context

The player reports that the current Forsaken City map works (cross-seam
traversal passes) but **performance is slow and the world doesn't feel
connected** — the renderer can only show a tiny piece at a time and frame time
is high. This plan is a major overhaul of the render pipeline to present a
large, apparently seamless open world despite the N64's depth-buffer, memory,
bandwidth, and geometry limits.

The authoritative architecture spec is
`.plans/n64-open-world-renderer/arch.md`
(`N64 Seamless Open-World Renderer — Technical Specification`), which derives
from `lambertjamesd/n64brew2025`
(`src/overworld/overworld_render.c`, `src/render/material.c`,
`src/overworld/overworld_load.c`). The central principle:

> Do not attempt to render the entire world using one depth-tested coordinate
> space. Divide the visible world into rendering domains with different
> precision requirements.

**Intended outcome:** the player can move continuously across the whole 45-cell
Forsaken City world (currently 240-unit cells, ~45 cells) with:
- a **distant pass** (compressed coordinates, Z-buffer off, explicit
  back-to-front sort, LOD hierarchy, directional variants, frustum culling) so
  the horizon is visible;
- a **near pass** (camera-relative coordinates, Z-buffer on, textured
  high-detail tiles) around the player, preceded by a Z-off **low-priority**
  subpass for water/background surfaces;
- **tile streaming** so the world loads incrementally, never all at once;
- bounded per-frame work (visible-tile/LOD/material budgets), explicit material
  batching, and RSPQ display-list blocks where practical;
- **fog + skybox** to hide the distant/near transition;
- **debug visualization modes** (tile boundaries, LOD level, pass isolation,
  streaming state) for tuning; and
- **per-phase profiling** so regressions are measurable.

This plan follows the implementation order recommended in `arch.md` §41.

## Current state (verified by exploration, 2026-08-11)

**Inc 1 (camera-relative foundation) is DONE.** The codebase has:
- `camera_space_math.hpp` — `ToCameraSpace`/`FromCameraSpace`/`PackedFitsInt16`
- `LvlRoomRenderer::SetCameraPosition()` — model matrix is camera-relative
- `ChunkRingRenderer::SetCameraPosition()` — fans out to children
- `gameplay_scene.cpp` — camera-at-origin view (`view_origin = {0,0,0}`,
  `view_target = camera_target - camera_position`) so view + model agree
- `tests/camera_space_math.cpp` — host round-trip + int16 overflow guard
- `tests/render_pipeline_contract.py` — sweeps camera across all cells,
  asserts `PackedFitsInt16` holds at `kPosScale=32`

**Inc 2 (two-pass architecture) is PARTIALLY implemented.** The codebase has:
- `open_world_renderer.hpp` — orchestrator with `PassCameras`, `BuildPassCameras`,
  `FrameStage` enum, `OrderedFrameStages()`, owns `ChunkRingRenderer* ring_`
  (legacy near pass) and forward-declared `TileStreamer*`/`DistantWorldRenderer*`
- `pass_camera_math.hpp` — `CameraDesc`, `MakeNearCamera`, `MakeDistantCamera`
  (matches `arch.md` §5: `distant.near = near.far * 0.25f * lod_scale`,
  `distant.far = tile_size * 1.4f`)
- `tile_visibility.hpp` — `Mat4`, `ProjectFrustumToGround`, `ScanlineTileRanges`
  (matches `arch.md` §14-15)
- `tile_streamer.hpp` — **stub** (empty resident pool, no visibility logic)
- `distant_world_renderer.hpp` — **stub** (flips Z off briefly, no LOD/drawing)
- `gameplay_scene.cpp` — map-pack mode calls `open_world_.Render(cams)` with
  `BuildPassCameras(...)` using `lod_scale = 0.25f`
- `gameplay_scene.cpp` — `TransitionToRoom` and `BootMapPack` call
  `open_world_.SetCenter(...)` to load the ring
- `open_world_renderer.cpp` — device-side implementation already present
  (drives `ring_` near pass + distant/low-priority stubs)

**What Inc 2 still needs (the three host tests — all DONE 2026-08-11):**
- `tests/pass_camera_math.cpp` — done, PASSES
- `tests/tile_visibility_contract.cpp` — done, PASSES
- `tests/frame_order_contract.cpp` — done, PASSES
- Device verification of the two-pass frame order on Ares with the stubs
  (not host-testable; done at the next Ares launch)

**Remaining pre-overhaul facts (still true):**
- `LvlRoomRenderer` packs each cell's vertices as int16 at `kPosScale=32`,
  rebased to the cell's `render_origin` (cell center). `kPosScale=32` gives
  ±1024 game units of int16 headroom.
- `ChunkRingRenderer` draws the active cell + its ≤4 neighbors (5
  `LvlRoomRenderer`s), no LOD, no culling, no distance sort, no streaming
  eviction. Ring is always the same fixed ±1 cells regardless of camera.
  **This is the active near pass in the current code** (held by
  `open_world_renderer.hpp::ring_`); Inc 3 replaces it with `TileStreamer`.
- `MapRuntime` owns ONE global collision mesh + ONE active visual room.
  `ResolveCellByPosition` formula is duplicated in 3 places (runtime
  `map_runtime.cpp:98-101`, bake `chunking.py:25`, `brush_grid.py`).
- 45 cells at `--chunk-size 1200 --scale 0.2` → 240-unit cells;
  `render_origin` = cell center (`bake_interconnected_map.py:243-245`).
- The bake produces full-res per-cell `.lvl` only; **no LOD or low-detail
  representations exist**.
- Textures are dormant: `.sprite` files exist in `filesystem/tex/`, but the
  map-pack cells render flat-color. `MaterialCatalog` reads a per-room
  `.manifest` and is **not** wired to the forsyken-city pack. It is not
  `#include`d or instantiated in `gameplay_scene.cpp`.
- No per-subsystem profiler; only whole-frame timing (`n64/profiler.cpp`).
- No test runner; each host test has its build command in a header comment.

## Architectural decisions

- **Decision: follow `arch.md` as the authoritative architecture spec.**
  Every increment below maps to a section of `arch.md`; the final section of
  this plan contains an explicit coverage matrix proving all §40
  implementation requirements are addressed. The implementation order also
  follows `arch.md` §41.
- **Decision: camera-relative near pass, compressed-coordinate distant pass.**
  The renderer keeps the camera in world space for gameplay, but shifts the
  render origin to the camera each frame. The near pass uses the normal
  viewport (Z on); the distant pass uses a separate viewport with compressed
  coordinates (Z off) and explicit back-to-front sorting. Rationale: this is
  the core n64brew2025 technique — the far plane doesn't have to share the
  gameplay coordinate system. Alternatives rejected: raising the far clip /
  increasing Z precision (doesn't solve depth non-linearity), keeping the
  current fixed-per-cell render origin (per-cell origins don't follow the
  camera, so distant cells overflow int16).
- **Decision: procedural distant LOD via mesh decimation of existing `.lvl`
  cells, not a Blender/AO texture-bake pipeline.** Rationale: the repo has no
  Blender asset pipeline, and the user said "will work in textures later."
  We generate coarse distant representations offline by decimating the baked
  cell meshes (a `tools/` step that emits a `distant.lvl`-style coarse mesh per
  cell). Full baked AO textures are explicitly future work. Alternatives
  rejected: Blender bake (no pipeline exists).
- **Decision: wire up the dormant texture system for the near pass.** The map
  currently renders flat-color; this overhaul enables per-material textured
  tiles via `MaterialCatalog` + the existing `.sprite` files. Rationale: a
  seamless world needs consistent visual continuity; flat-color distant cells
  look disconnected. This is gated behind a config flag in early increments so
  it can be disabled if RDP state-change cost is too high. RSPQ render blocks
  and material batching are introduced at the same time so texture switches
  do not dominate CPU frame time. Alternatives rejected: keep flat-color
  (doesn't meet "seamless").
- **Decision: gameplay stays active-only and world-space; only rendering
  becomes camera-relative + multi-pass.** `MapRuntime` keeps one global
  collision mesh + one active visual room. The new renderer is a render-only
  subsystem. Rationale: preserves the verified active-only traversal path and
  keeps collision/camera/respawn correct. Alternatives rejected: moving
  gameplay into camera-relative space (risks physics drift).
- **Decision: bounded per-frame work with hard budgets and a debug-mode
  visualization system.** Per `arch.md` §31, §43: visible tile count, LOD
  entries, triangles, material changes, particles, texture uploads, and
  streaming ops get explicit caps; debug modes expose tile boundaries, LOD
  level, pass isolation, and streaming state. Rationale: must not allow
  visibility to create unbounded work.
- **Decision: add a host test runner and per-phase profiler.** Without these,
  the overhaul is unverifiable (the exploration confirmed no test runner and
  only whole-frame timing). Rationale: repeatable per-increment verification
  and measurable performance are required to prove the renderer is both
  correct and fast.
- **Decision: top-view frustum polygon + scanline tile enumeration for near
  visibility (not a distance ring).** `arch.md` §14-15 describes a 2D
  visibility polygon derived from the inverse view-projection matrix and a
  scanline row-by-row tile enumerator. The plan adopts this exactly and
  replaces the current `ChunkRingRenderer` distance-ring idea with a
  `TileVisibility` system. Rationale: scanline enumeration avoids expensive
  per-tile polygon tests, the stated N64 design philosophy.
- **Decision: LOD hierarchy with directional variants.** The distant pass uses
  a tree of LOD entries (`arch.md` §9-11), not a single coarse mesh per cell.
  Each entry stores `child_count`, `lod_scale`, priority, and four
  directional meshes. Rationale: parent representations replace children at
  distance, and directional variants optimize silhouettes per view direction
  — both are required by `arch.md` §40 LOD requirements.
- **Decision: explicit material batching + RSPQ display-list blocks where
  practical.** Per `arch.md` §22-23, materials are changed only when necessary
  and geometry is precompiled into RSPQ blocks to reduce CPU command
  construction each frame. Rationale: reduces RDP state changes and CPU
  overhead. This is introduced in Inc 5 but the data shapes (material slots,
  block handles) are reserved earlier so Inc 5 can attach them without
  restructuring `LvlRoomRenderer`.
- **Decision: low-priority Z-off subpass inside the near pass for
  water/background surfaces.** `arch.md` §20-21 orders the frame as
  `RenderDistant` (Z-off, back-to-front), `RenderLowPriority` (Z-off, also
  back-to-front), then `RenderHighPriority` (Z-on). The plan keeps this
  three-way ordering.
- **Decision: fog + skybox to hide the transition between passes.** `arch.md`
  §25-27 call for linear distance fog per pass and a skybox drawn before both.
  Fog uses pass-specific color and near/far distances; the skybox uses its own
  transform (`arch.md` §27).
- **Decision: the implementation order follows `arch.md` §41.** Phases 1-6 are
  reflected in the increment DAG below; the plan does not introduce work that
  skips phases or reverses dependencies. Phases are explicitly named in the
  DAG to make the correspondence obvious.

## Assumptions and answers from code

- **Decision: `kPosScale=32` (±1024 units int16) is the per-cell packing
  budget.** Source: `lvl_room_renderer.hpp:37-38`, `.agents/map-creation.md`
  kPosFp table.
- **Decision: the distant pass needs its own smaller packing scale**
  (`kLodScale << 1`), because compressed world coords must stay far inside
  int16 range (`distant_far / kLodScale <= 32767`). Source: spec §4-6
  (`lod_scale = 1/tile_x` as a coordinate scale, distinct from clip planes).
- **Decision: `ChunkRingRenderer` (5 fixed slots) is the seed of tile
  streaming and must be generalized.** Source: `chunk_ring_renderer.hpp` —
  `renderers_[5]`, `ResolveRingRooms` inline helper.
- **Decision: cells are 240 world units, render_origin = cell center.**
  Source: `bake_interconnected_map.py:243-245`, `chunk_size 1200 * scale 0.2`.
- **Decision: the near pass must use camera-relative model matrices (world
  origin shifted to camera), not the current fixed cell-center origin.** Source:
  spec §19-20; exploration finding that current origins are fixed per cell and
  don't follow the camera.
- **Decision: `MaterialCatalog` is the near-pass texture path but is not wired
  to forsyken-city; it needs a `.manifest` for the pack.** Source:
  `material_catalog.cpp`, `map-creation.md` (MaterialCatalog reads
  `rom:/lvl/<name>.manifest`).
- **Decision: `ResolveCellByPosition` must stay in sync and must not fork a
  4th copy.** Source: duplicated in 3 places; `interconnected_seam_equivalence.py`
  cross-ref comment.
- **Decision: the distant camera is a distinct camera, not a clip-plane
  multiplier.** `arch.md` §5 specifies:
  - `distant.near = near.far * 0.25f * lod_scale`
  - `distant.far  = tile_size * 1.4f`
  The plan adopts these exact formulas. `lod_scale` is a coordinate-scale
  value used for compressed distant vertices (not a near/far multiplier).
- **Decision: any new render `.cpp` must be added to the `src` list in the
  Makefile.** Source: exploration of the flat Makefile `src` list.
- **Decision: host tests must never include libdragon/t3d headers; render
  math must live in inline headers.** Source: `render_origin_math.hpp`,
  `chunk_ring_renderer.hpp::ResolveRingRooms`.

## Risks accepted

- **Camera-relative shift could break collision/gameplay if render-space leaks
  into physics.** Mitigation: render math is isolated in host-testable inline
  headers; gameplay stays world-space. The camera-relative transform is
  validated by a host test (Inc 1).
- **Distant-pass int16 packing could overflow if the coarse scale is wrong.**
  Mitigation: a host contract test asserts the distant LOD vertex range stays
  within int16 at the chosen `kLodScale` for every cell (Inc 4).
- **Texturing adds RDP state-change cost and `.manifest` material-index
  coupling.** Mitigation: texturing is gated behind a flag; `MaterialCatalog`
  null-slot reservation is preserved (TB_empty). If the cost is too high,
  fall back to flat-color near pass (Inc 5 flag).
- **Streaming/LRU could introduce seams or fall-through if the render ring and
  the gameplay active cell diverge.** Mitigation: gameplay stays active-only;
  the renderer derives its ring from `MapRuntime`'s active cell + neighbors
  and never changes gameplay state.
- **The distant pass's separate camera/projection could misalign with the near
  pass.** Mitigation: both are derived from the same world-space camera in a
  single host-testable function (`BuildPassCameras`), so they cannot drift.
- **Performance budget may still be exceeded.** Mitigation: per-phase profiler
  + hard per-frame budgets with config to shrink the ring / raise LOD distance /
  disable textures if the 240-unit × 45-cell world is too heavy.

## Increment DAG

This DAG mirrors the phases in `arch.md` §41. **Phase ordering is enforced
through the dependency edges** — later phases depend on earlier phases (so
Phase 5 cannot precede Phase 4, and the validation increment is last). Each
increment's `Unblocks` lists only its direct dependents (transitive successors
follow through the edges).

- **Phase 1 — Foundation**
  - Inc 1 — Camera-relative render transform foundation (M) — depends on:
    none — unblocks: 2
- **Phase 2 — Two-pass architecture**
  - Inc 2 — Two-pass render architecture + frame order + low-priority
    subpass stub (M) — depends on: 1 — unblocks: 3, 4
- **Phase 3 — Tile streaming**
  - Inc 3 — Near-field tile streaming with top-view frustum + scanline
    visibility + LRU (L) — depends on: 1, 2 — unblocks: 4, 5
- **Phase 4 — LOD + distant pass**
  - Inc 4 — Distant LOD generation + LOD hierarchy + directional variants +
    compressed distant pass (L) — depends on: 2, 3 — unblocks: 6
- **Phase 5 — Material pipeline**
  - Inc 5 — Textured near pass + material catalog wiring + material batching /
    RSPQ blocks (M) — depends on: 3, 4 — unblocks: 7
- **Phase 6 — Atmosphere + integration**
  - Inc 6 — Fog + skybox + atmospheric fade (M) — depends on: 2, 4 —
    unblocks: 7
- **Phase 6/7 — Validation (must be last)**
  - Inc 7 — Per-phase profiler + host test runner + budgets + debug
    visualization modes (M) — depends on: 2, 3, 4, 5, 6 — unblocks: none

## Increments

### Inc 1 — Camera-relative render transform foundation (M) ✅ DONE

**Status:** DONE (implemented 2026-08-10, verified 2026-08-11)

**Depends on:** none
**Unblocks:** 2 (transitively 3, 4, 5, 6, 7 through the edges)

#### What was built

- **`src/user/gameplay/render/camera_space_math.hpp`** — pure header-only math:
  `ToCameraSpace`, `FromCameraSpace`, `ValidateCameraSpaceRoundTrip`,
  `PackedFitsInt16`. No N64 types; host-testable.
- **`src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}`** —
  `SetCameraPosition(const Vec3&)` recomputes the model matrix translation to
  `render_origin_ - camera_pos` each frame. Vertices stay packed against their
  fixed per-cell render origin (no per-frame re-packing). `LvlRoomRenderer`
  has **no** `SetMaterialCatalog`/`BuildRspqBlocks` hooks — it stays a pure
  flat-color packer (the validated fallback).
- **`src/user/gameplay/render/chunk_ring_renderer.{hpp,cpp}`** —
  `SetCameraPosition(const Vec3&)` fans out to all 5 child renderers.
- **`src/user/gameplay/scene/gameplay_scene.cpp`** — `Render()` sets
  `view_origin = {0,0,0}`, `view_target = camera_target - camera_position`
  (camera-at-origin view) so view + model matrices agree (no double `-camera`
  offset). Calls `SetCameraPosition` on the ring/room before drawing.
- **`tests/camera_space_math.cpp`** — host test: round-trip exactness +
  `PackedFitsInt16` overflow guard at `kPosScale=32`. Build:
  `g++ -std=c++17 -Isrc/user tests/camera_space_math.cpp`.
- **`tests/render_pipeline_contract.py`** — Python host test: sweeps camera
  across all 45 cells, asserts `PackedFitsInt16` holds at every cell center
  and corner.

#### Verification results

- `tests/camera_space_math.cpp` — PASSES
- `python3 tests/render_pipeline_contract.py` — PASSES
- `./compile-rom.sh` — builds `madeline_cube_rom.z64` cleanly
- **Ares seam walk** — player crosses cell boundaries with no double-offset,
  no visible shift/popping. Camera-at-origin view + camera-relative model
  matrices agree. ✅

### Inc 2 — Two-pass render architecture + frame order + low-priority subpass stub (M) ✅ DONE

**Status:** DONE (verified 2026-08-11)

**Depends on:** 1
**Unblocks:** 3, 4 (transitively 5, 6, 7 through the edges)
**Done criteria:** the render frame executes the documented `arch.md` §21
order (skybox, distant Z-off, low-priority near Z-off, high-priority near Z-on)
driven by the `OpenWorldRenderer` orchestrator; host tests assert the
frame-stage order, pass-camera derivation, and tile-visibility math.

#### Already built (exists in codebase)

- **`src/user/gameplay/render/open_world_renderer.hpp`** — orchestrator header
  with `PassCameras`, `BuildPassCameras`, `FrameStage` enum,
  `OrderedFrameStages()`. Owns `ChunkRingRenderer* ring_` (legacy near pass,
  active), forward-declared `TileStreamer*` and `DistantWorldRenderer*`
  (null until Inc 3/4). `Render(const PassCameras&)` drives the §21 frame
  order. `SetCenter(...)` and `SetCameraPosition(...)` fan to children.
- **`src/user/gameplay/render/pass_camera_math.hpp`** — `CameraDesc`,
  `MakeNearCamera`, `MakeDistantCamera` (matches `arch.md` §5 exactly:
  `distant.near = near.far * 0.25f * lod_scale`,
  `distant.far = tile_size * 1.4f`), `ValidateDistantCamera`.
- **`src/user/gameplay/render/tile_visibility.hpp`** — `Mat4` (host-safe 4×4),
  `Mat4TransformPoint`, `Polygon2`, `ProjectFrustumToGround`,
  `ScanlineTileRanges` (matches `arch.md` §14-15).
- **`src/user/gameplay/render/tile_streamer.hpp`** — **stub**: empty resident
  pool (`residents_`/`resident_count_`/`resident_capacity_` all zero), no
  visibility logic. Public API: `UpdateCamera`, `DrawLowPriority`,
  `DrawHighPriority`, `ResidentCount`.
- **`src/user/gameplay/render/distant_world_renderer.hpp`** — **stub**:
  `UpdateCamera`, `Render` (flips Z off briefly to exercise frame order, draws
  nothing). No LOD, no distant meshes.
- **`src/user/gameplay/scene/gameplay_scene.cpp`** — map-pack mode calls
  `open_world_.Render(cams)` with `BuildPassCameras(...)` using
  `lod_scale = 0.25f`. `TransitionToRoom` and `BootMapPack` call
  `open_world_.SetCenter(...)`.

#### What remains for Inc 2 (all DONE 2026-08-11)

- **`tests/pass_camera_math.cpp`** — host test asserting `BuildPassCameras`
  matches `arch.md` §5 (`distant.near = near.far * 0.25f * lod_scale`,
  `distant.far = tile_size * 1.4f`), both passes share orientation, and a
  degenerate distant range is rejected. **PASSES.**
- **`tests/tile_visibility_contract.cpp`** — host test for
  `ProjectFrustumToGround` + `ScanlineTileRanges` on synthetic view boxes,
  including conservative boundary over-inclusion. **PASSES.**
- **`tests/frame_order_contract.cpp`** — host test that `OrderedFrameStages`
  yields `[Distant, LowPriority, HighPriority, Present]` and that
  `BuildPassCameras` derives both passes from one camera. **PASSES.**
- **`open_world_renderer.cpp`** — device-side implementation was already in
  place (drives `ring_` near pass + distant/low-priority stubs).
- **Makefile** — `open_world_renderer.cpp` already in the `src` list.

#### Edge cases
- `distant_far <= far_plane` must be rejected/clamped (distant pass would be
  empty).
- Distant camera `near` must not be ≤ 0.
- Before Inc 4, the distant renderer is unloaded; the frame must still draw the
  near ring (no blank screen).

#### Verification
- Run: host tests `pass_camera_math.cpp`, `tile_visibility_contract.cpp`,
  `frame_order_contract.cpp`; `./compile-rom.sh`.
- Done: the frame runs a documented two-pass order with one camera; no visual
  regression (near pass still draws the ring identically).

### Inc 3 — Near-field tile streaming with top-view frustum + scanline visibility (L)

**Depends on:** 1, 2
**Unblocks:** 4, 5 (transitively 6, 7 through the edges)
**Done criteria:** the renderer computes the camera's top-view visibility
polygon (`arch.md` §14), scanline-enumerates visible tiles (`arch.md` §15),
keeps a bounded resident pool of detailed cells (active cell + neighbors)
with LRU eviction, and never requests more streaming ops than a hard budget.
A host test asserts the scanline enumerator produces the correct tile footprint
for simple frusta, that the ring resolves the correct cells for a swept camera
path, and that the resident pool stays within bounds.

This replaces the fixed-distance `ChunkRingRenderer` with a camera-frustum
driven tile streamer as specified in `arch.md` §14-17.

#### Files to touch

##### src/user/gameplay/render/tile_streamer.{hpp,cpp} (new)
- What changes: a render-only resident pool that loads/evicts cells based on
  the visibility polygon + scanline enumerator from `tile_visibility.hpp`.
  Owns a bounded array of `LvlRoomRenderer` instances (default capacity,
  e.g. 9 = center + 8 neighbors, configurable). Each entry records its cell id
  + render origin for LRU eviction.
- Function(s):
  - `bool SetCenter(const MapSpecV2& spec, const V2RoomSpec& center,
    const char* build_dir)` — sets the active cell and resolves the visible
    set using the scanline enumerator (for now, also keeps immediate neighbors
    so the near pass stays conservative until `arch.md` §15 is fully wired).
  - `void SetCameraPosition(const Vec3&)` — fans out to residents.
  - `void DrawLowPriority(const CameraDesc&)` and
    `void DrawHighPriority(const CameraDesc&)` — draws all residents (near
    pass), with Z on for high priority (Inc 2 already defines the subpass
    order).
  - `int ResidentCount() const`; `int EvictedThisFrame() const`.
  - `inline int ResolveVisibleTiles(const MapSpecV2& spec,
    const Mat4& inv_view_proj, float ground_y, float tile_size,
    const V2RoomSpec* out[])` — host-testable helper that calls
    `ProjectFrustumToGround` + `ScanlineTileRanges` and returns the visible
    cell set, bounded by `kMaxVisibleCells`.
  - **Host-testability rule:** all logic exercised by `tile_streamer_smoke.cpp`
    and `tile_stream_lru_contract.cpp` (visibility resolution, LRU selection,
    ring bounds, "never evict center") lives in **inline header functions** so
    the Pattern C host tests link `mappack_loader.cpp` only. The device-only
    `tile_streamer.cpp` holds the actual resident array + `SetCenter` I/O but
    delegates its decision logic to the header helpers. Any non-inline member
    that a host test needs is flagged explicitly; otherwise it stays out of the
    Pattern C link set.
- Data shapes: `V2RoomSpec` center; `V2RoomSpec* out[]` bounded by a
  `kMaxRing` constant (e.g. 9).
- Integration points: called from `OpenWorldRenderer` near-pass; uses
  `MapRuntime::Spec()`/`ActiveSpec()` and `ResolveCellByPosition`.
- Error paths: a missing neighbor LVL is skipped (non-fatal); a missing center
  is fatal. Eviction over capacity drops the farthest resident (never the
  center).

##### src/user/gameplay/render/chunk_ring_renderer.{hpp,cpp}
- What changes: **removed** (superseded by `TileStreamer`). Its inline
  `ResolveRingRooms` is replaced by `TileStreamer::ResolveDistanceRing`. The
  `ChunkRingRenderer` class and its Makefile entry are deleted.
- **Current state:** `open_world_renderer.hpp` holds BOTH `ChunkRingRenderer*
  ring_` (the active near pass in Inc 2) AND `TileStreamer* tile_streamer_`
  (null stub). Inc 3 must: (1) flesh out `tile_streamer_` with the visibility
  + LRU logic, (2) switch `RenderHighPriority()` from `ring_->Draw()` to
  `tile_streamer_->DrawHighPriority()`, (3) switch `SetCenter()` from
  `ring_->Load(...)` to `tile_streamer_->SetCenter(...)`, (4) remove the
  `ring_` member and its `#include`, (5) delete `chunk_ring_renderer.{hpp,cpp}`.
- **Dangling-consumer cleanup (must land in the same increment):** deleting
  `chunk_ring_renderer` breaks its consumers — (a) `tests/chunk_ring_smoke.cpp`
  includes `gameplay/render/chunk_ring_renderer.hpp` and calls `ResolveRingRooms`,
  and (b) `open_world_renderer.hpp` includes it for the `ring_` member. Inc 3
  must: delete or rewrite `tests/chunk_ring_smoke.cpp` to use
  `TileStreamer::ResolveDistanceRing`, drop the `#include` from
  `open_world_renderer.hpp`, and remove `chunk_ring_renderer.cpp` from the
  Makefile `src` list. Grep the tree for `chunk_ring_renderer` after the
  delete to confirm zero remaining references.
- Integration points: `open_world_renderer.hpp` switches from `ring_` to
  `tile_streamer_` as the sole near-pass renderer.

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: near pass switches from `ChunkRingRenderer* ring_` to
  `TileStreamer* tile_streamer_`. The `tile_streamer_` member already exists
  as a forward-declared null pointer; Inc 3 instantiates it with the full
  `TileStreamer` type and wires it into `RenderHighPriority()` and
  `SetCenter()`. The `ring_` member and its `#include` are removed.
- Function(s): `SetCenter(...)` calls `tile_streamer_->SetCenter(...)` instead
  of `ring_->Load(...)`. `RenderHighPriority()` calls
  `tile_streamer_->DrawHighPriority(...)` instead of `ring_->Draw()`.
  `SetCameraPosition(...)` fans to `tile_streamer_` instead of `ring_`.
- Integration points: `gameplay_scene.cpp` `TransitionToRoom` + `BootMapPack`
  already call `open_world_.SetCenter(...)` — no change needed there.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: `TransitionToRoom` and `BootMapPack` call
  `open_world_.SetCenter(spec, *active_spec, nullptr)` instead of
  `chunk_ring_.Load(...)`.
- Function(s): `TransitionToRoom()`, `BootMapPack()`.
- Integration points: `OpenWorldRenderer` member.
- Error paths: `SetCenter` failure (no center LVL) falls back to near-only with
  the active room already drawn by the legacy path.

##### Makefile
- What changes: remove `chunk_ring_renderer.cpp`; add `tile_streamer.cpp`.
- Function(s): `src` list.

##### src/user/gameplay/world/map_runtime.cpp + tools/ogworld/chunking.py + tools/ogworld/brush_grid.py
- What changes: **consolidate the 3-way `ResolveCellByPosition` duplication.**
  The formula `iz = floor(world_z / (chunk_size * scale))` (with world_z =
  depth = −map_y) is duplicated in `map_runtime.cpp:98-101`,
  `chunking.py:25`, and `brush_grid.py`. Extract a single canonical
  implementation — either a host-testable inline C++ helper in
  `render_origin_math.hpp` (for the runtime side) and a shared Python function
  in `ogworld/chunking.py` (for the bake side), with a cross-language contract
  test in `tests/interconnected_seam_equivalence.py` asserting they produce
  identical results for all 45 cells. This prevents a 4th copy from appearing
  in `tile_streamer.cpp`.
- Function(s): `inline int ResolveCellIndex(const Vec3& world_pos, float
  chunk_size, float scale)` (C++); `def resolve_cell_index(world_pos,
  chunk_size, scale)` (Python).
- Integration points: `TileStreamer::SetCenter` uses the canonical C++ helper;
  `bake_interconnected_map.py` uses the canonical Python helper.
- Error paths: out-of-bounds world position returns −1 (no cell).

##### tests/tile_streamer_smoke.cpp (new)
- What changes: host test that `ResolveDistanceRing` returns the correct cells
  for the real baked map at the start cell and for a swept path crossing
  multiple seams; asserts the ring count never exceeds `kMaxRing` and that the
  center is always first. **This replaces `tests/chunk_ring_smoke.cpp`, which is
  deleted in this increment.**
- Function(s): `main()` (Pattern C — links `mappack_loader.cpp` only).

##### tests/tile_stream_lru_contract.cpp (new)
- What changes: host test simulating a camera walk that triggers eviction;
  asserts `ResidentCount()` never exceeds capacity and the farthest-resident
  LRU eviction drops the correct cell (never the center).
- Function(s): `main()` (Pattern C).

#### Edge cases
- Map-edge cells have fewer neighbors (skip; never fatal).
- A dash crossing two cells in one frame refreshes to the final cell's ring.
- Resident pool must not evict the center cell even under heavy load.
- Distant cells (outside the near ring radius) must not be drawn by the near
  pass — they're the distant pass's job (Inc 4).

#### Verification
- Run: host tests `tile_streamer_smoke.cpp`, `tile_stream_lru_contract.cpp`;
  `./compile-rom.sh`.
- Done: the near pass draws a camera-relative ring of bounded size that follows
  the player, with streaming/eviction; crossing seams shows the next chunk
  without a blank or a stale cell.

### Inc 4 — Distant LOD generation + compressed distant pass (L)

**Depends on:** 2, 3
**Unblocks:** 6 (transitively 7 through the edges)
**Done criteria:** the bake emits a coarse distant representation per cell; the
distant pass renders them Z-off with compressed coordinates and explicit
back-to-front sorting; a host test proves the coarse mesh's packed vertices stay
inside int16 at the chosen `kLodScale` for every cell.

This is the core n64brew2025 trick — a distant world that doesn't share the
near world's coordinate space or Z-buffer.

#### Files to touch

##### tools/ogworld/distant_lod.py (new)
- What changes: offline mesh decimation. Reads the baked per-cell geometry and
  emits a coarse distant representation (a `distant.lvl`-style mesh or a
  dedicated `distant.cmsh`). The decimation: merge coplanar faces, collapse
  edges within a tolerance, quantize to a coarse int16 at a `kLodScale`.
  Produces one distant mesh per cell (or per 2×2 cell block if cells are too
  small to decimate meaningfully).
- Function(s):
  - `build_distant_lod(cell_polys, lod_scale) -> DistantMesh`.
  - `emit_distant_lvl(mesh, out_path)`.
- Data shapes: coarse mesh with reduced face/vertex counts, per-material color
  (baked flat, since texturing is future work).
- Integration points: `bake_interconnected_map.py` calls `build_distant_lod`
  per cell and writes `staging/<cell>_distant.lvl`. A `kLodScale` constant
  (e.g. `1.0f` = no compression, or `0.25f`) is chosen so the whole map fits
  int16 in distant space.
- Error paths: if a cell has no renderable geometry, skip its distant mesh
  (decoration/hazard).

##### tools/bake_interconnected_map.py
- What changes: after building per-cell LVL, call `build_distant_lod` and write
  `<cell>_distant.lvl` into `staging/`. Add the distant files to the DFS pack
  (Makefile wildcard already covers `*.lvl`).
- Function(s): add a distant-LOD stage to the bake.
- Data shapes: per-cell distant LVL.
- Integration points: the `.lvl` wildcard in the Makefile already packages them.

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp} (new)
- What changes: the distant pass. Renders the coarse distant cells with a
  compressed coordinate scale, Z-buffer off, explicit back-to-front sorting,
  **and a LOD hierarchy with directional variants** as specified in
  `arch.md` §8-13. Reuses `LvlRoomRenderer`-style packing but with its own
  `kLodScale` and a separate viewport (from Inc 2's `MakeDistantCamera`).
- Function(s):
  - `bool Load(const MapSpecV2& spec, const char* build_dir)` — load distant
    meshes for all cells (coarse, so fewer) and build the **LOD hierarchy**
    (`DistantLodEntry` tree with `child_count`, `lod_scale`, `priority`,
    `meshes[4]` per `arch.md` §9).
  - `void SetCameraPosition(const Vec3&)` — rebase distant translation.
  - `void Render()` — cull with 2D clipping planes (`arch.md` §13), select LOD
    entries (`arch.md` §10-12: distance² < threshold² × lod_scale² and
    directional mesh), sort back-to-front by distance priority
    (`arch.md` §8), disable Z, draw with compressed coordinates, re-enable Z
    after.
  - `inline int BuildDistantRenderList(const MapSpecV2& spec,
    const Vec3& camera_pos, DistantEntry out[])` — host-testable: cull + select
    visible LOD entries and assign distance-based priority.
- Data shapes:
  - `DistantLodEntry { int child_count; float lod_scale; int priority;
    LvlRoomRenderer* meshes[4]; DistantLodEntry* children[]; }`
    (`arch.md` §9).
  - `DistantEntry { int cell_id; float distance; uint32_t priority; }`.
- Integration points: `OpenWorldRenderer` owns a `DistantWorldRenderer`;
  `Render()` calls distant before near.
- Error paths: missing distant mesh skipped; Z restored even if the sort is
  empty.

##### src/user/gameplay/render/lod_math.hpp (new)
- What changes: host-safe helpers for the LOD hierarchy and directional
  selection math described in `arch.md` §9-13.
- Function(s):
  - `inline int SelectLodLevel(const DistantLodEntry* root, const Vec3&
    camera_pos, float level2_min_distance)` — returns the LOD level whose
    squared-distance threshold `dist² < LEVEL2_MIN_DISTANCE² × lod_scale²`
    (`arch.md` §10) is satisfied.
  - `inline int DirectionalMeshIndex(const Vec3& camera_pos,
    const Vec3& tile_origin, const Vec3& camera_dir, float close_threshold)`
    — returns the N/S/E/W directional index; switches to `camera_dir` when
    `abs(delta.x) < close_threshold && abs(delta.z) < close_threshold`
    per `arch.md` §12.
- Data shapes: `DistantLodEntry` forward-declared; threshold constants.
- Integration points: used by `DistantWorldRenderer::BuildDistantRenderList`;
  host tests `lod_math.cpp` validate selection rules.

##### tests/lod_math.cpp (new)
- What changes: host test that `SelectLodLevel` and `DirectionalMeshIndex`
  return the expected indices for synthetic camera/tile configurations
  (distance thresholds, center-vs-periphery directional selection).
- Function(s): `main()`.

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: own a `DistantWorldRenderer`; `Render()` calls
  `RenderDistant()` (Z off, compressed) then `RenderNear()` (Z on). `UpdateCamera`
  fans the camera to both.
- Function(s): `RenderDistant()`; `Render()`.
- Integration points: `GameplayScene::Render`.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: add a configurable `kPosScale` (or a `SetLodScale`) so the same
  packer can emit distant meshes at a compressed scale. Currently `kPosScale`
  is a `static constexpr` member; make it a parameter with the near default 32.
- Function(s): `Load(..., float pos_scale = kDefaultPosScale)`.
- Data shapes: `float pos_scale`.
- Integration points: `DistantWorldRenderer` loads distant LVLs at `kLodScale`.
- Error paths: clamp `pos_scale` to a sane range.

##### tests/distant_lod_contract.py (new)
- What changes: host test that bakes the map and asserts every distant cell's
  packed vertices stay within int16 at `kLodScale` across the **full world
  extent at the maximum distant-far plane**, not just near the origin. Because
  distant cells are camera-relative (Inc 1), the worst case is a camera at one
  edge looking to the opposite edge: the farthest cell's vertices are at
  `distant_far` from the camera, so the assertion must be
  `distant_far / kLodScale <= 32767` AND every packed distant vertex
  `|v * kLodScale| <= 32767` for a camera sweep from edge to edge.
- Function(s): `test_distant_lod_fits_int16_at_max_far()`.
- Integration points: invokes the bake; reads distant LVLs; uses `kLodScale`
  and the world half-extent (`distant_far`). `kLodScale` and `distant_far` must
  be chosen consistently (a smaller `kLodScale` gives more int16 headroom for a
  larger `distant_far`).

##### tests/distant_pass_order.cpp (new)
- What changes: host test that `BuildDistantRenderList` assigns strictly
  increasing priority with distance (so far cells draw first) and that the
  distant camera (from Inc 2) is used.
- Function(s): `main()` (Pattern A or C).

#### Edge cases
- The distant pass must not draw cells that the near pass already draws (split
  at a configurable distance threshold).
- Distant camera `near` must be > 0.
- A distant cell at the far edge must not overflow int16 at `kLodScale`.
- Z-buffer must be re-enabled before the near pass even if the distant list is
  empty.

#### Verification
- Run: `python3 tests/distant_lod_contract.py`; host test
  `distant_pass_order.cpp`; `./compile-rom.sh`.
- Done: the horizon renders (distant cells visible) with Z-off + compressed
  coords + explicit sort; the near pass renders on top with Z-on; no int16
  overflow.

### Inc 5 — Textured near pass (M)

**Depends on:** 3, 4
**Unblocks:** 7
**Done criteria:** near-pass cells render with their materials textured (via
`MaterialCatalog` + the existing `.sprite` files), gated behind a config flag
that defaults on and can be disabled if RDP cost is too high. A host test
asserts the material index → sprite resolution is stable.

**Dependency rationale:** Inc 5 depends on Inc 4 purely to enforce `arch.md`
§41 phase ordering (Phase 5 after Phase 4). The texturing itself only needs
Inc 3 (near tile streaming). This is intentional — the material pipeline must
not land before the distant pass it visually must integrate with. If Inc 4 is
blocked, Inc 5 can be unblocked by relaxing this edge, but the implementer must
then verify textured-near + flat-distant visual integration separately.

**`kLodScale` value:** `lod_scale = 0.25f` is the definitive value, already
hardcoded in `gameplay_scene.cpp`'s `BuildPassCameras` call. This gives
`distant_far / 0.25 = 4 × distant_far` units of int16 headroom in distant
space. The `distant_lod_contract.py` test (Inc 4) must use this same value.

#### Files to touch

##### src/user/gameplay/render/material_catalog.cpp
- What changes: wire `MaterialCatalog` to the forsyken-city map-pack. It reads
  a per-pack `.manifest` (one material name per line, in bake order) and
  resolves each to `rom:/tex/<name>.sprite`. Preserve the `TB_empty` null-slot
  reservation so material indices don't shift.
- **Wiring path:** `MaterialCatalog` is instantiated in `gameplay_scene.cpp`
  (it is currently not `#include`d or used there). `BootMapPack` loads
  `rom:/lvl/forsyken-city.manifest` via `material_catalog_.Load(...)` after
  `map_runtime_.Init(...)`. The catalog is passed to `OpenWorldRenderer` via
  `open_world_.SetMaterialCatalog(&material_catalog_)`, which stores the
  pointer and forwards it to `TileStreamer` → `TexturedRoomRenderer`.
- Function(s): `Load(pack_manifest_path)`; `Resolve(material_id)`.
- Data shapes: material id → `sprite_t*`.
- Integration points: `TexturedRoomRenderer` uploads the sprite for a batch
  instead of flat `primColor`.

##### src/user/gameplay/render/textured_room_renderer.{hpp,cpp} (new) — **chosen path**
- What changes: a new textured renderer that wraps `LvlRoomRenderer`-style
  packing but draws each material batch with the resolved sprite. **Decision:
  create `textured_room_renderer` as a new file; do NOT extend
  `lvl_room_renderer.cpp` with texturing.** This keeps the flat-color
  `lvl_room_renderer` intact as the validated fallback (so `kEnableTextures`
  can be turned off and the world renders exactly as before), and avoids
  entangling RDP combiner switches into the existing validated path.
  - `LvlRoomRenderer` has no `SetMaterialCatalog`/`BuildRspqBlocks` hooks
    (the Inc 1 reservation was rescinded). `TexturedRoomRenderer` owns its
    own catalog pointer + RSPQ block storage.
  - `LvlRoomRenderer` remains the near-pass renderer in Inc 3; Inc 5 switches
    the near pass (`TileStreamer::DrawHighPriority`) to instantiate
    `TexturedRoomRenderer` when `kEnableTextures` is on, and keeps
    `LvlRoomRenderer` when off.
  - For each batch, look up the material's sprite, upload it as a tile, and
    draw with a textured combiner. Keep the per-batch primColor fallback for
    materials with no sprite. Gated behind `kEnableTextures` (default true).
  - **RSPQ block construction:** after loading a cell's LVL geometry, precompile
    each material batch into an `rspq_block_t*` via `rspq_block_begin()` /
    `rspq_block_end()`. The block captures the full draw sequence for that
    batch (set combiner, upload tile, draw triangles). At render time, call
    `rspq_block_run(block)` instead of re-emitting commands. Blocks are freed
    in the destructor via `rspq_block_free()`.
- **Visual-change guard (default-true is a behavior change):** the current near
  pass is flat `PRIM*SHADE` with a fixed combiner and `material_color()`. The
  current `lvl_room_renderer.cpp:187-195` sets `material_color` per batch.
  Defaulting textures ON switches the combiner per batch (textured vs flat
  fallback) and **changes the visuals** if any forsyken-city `.sprite` files
  exist. Keep the gate explicit in the ROM entrypoint (`rom_main.cpp` or
  `gameplay_scene.cpp`), keep flat-color as the validated fallback (so the flag
  can be turned off and the world renders exactly as before), and document the
  per-batch combiner switch (RDP state-change cost) in this spec. The Ares
  traversal gate must compare textured vs flat output at least once.
- Function(s): `Draw()` — add a textured branch.
- Data shapes: `MaterialCatalog` lookup per batch.
- Integration points: `TileStreamer` uses `TexturedRoomRenderer` for near cells
  when `kEnableTextures` is on.
- Error paths: a missing sprite falls back to the flat primColor; a material
  index with no `.manifest` entry is a hard assertion (indices must stay in
  sync).

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes: distant cells stay flat-color (baked color), per the user's
  "textures later" for distant baked AO. No texturing here.

##### tools/bake_interconnected_map.py / tools/writers/lvl_world_writer.py
- What changes: emit a `forsyken-city.manifest` (material names in bake order)
  alongside the pack, so `MaterialCatalog` can resolve indices.
- **Index-order consistency (must match existing material ids):** `write_lvl_room`
  already stores material names as per-cell `lvl.strings` in bake order, and the
  existing `LvlRoomRenderer::material_color()` switch (`lvl_room_renderer.cpp:189`)
  uses material **id = index into that string table**. The emitted
  `forsyken-city.manifest` must list material names in the **same order** as the
  per-cell string IDs so the texture ↔ material mapping stays consistent with
  the existing `material_color()` ids (rock_1=0, snow_1=1, rock_2=2,
  metal_floor_1=3, floor_dirty_concrete=4). A mismatch shifts indices and breaks
  the mapping — the `MaterialCatalog` null-slot (`TB_empty`) reservation must be
  preserved.
- **Verification mechanism:** `tests/material_catalog_test.cpp` must assert that
  the emitted `.manifest`'s line-N material name matches the per-cell
  `lvl.strings[N]` for every cell in the pack. This catches index shifts at
  bake time.
- Function(s): write the manifest in the bake.
- Data shapes: text file, one material name per line.

##### tests/material_catalog_test.cpp
- What changes: host test that a `.manifest` + `.sprite` set resolves material
  ids correctly, that `TB_empty` reservation holds (indices don't shift), and
  that the manifest order matches per-cell string IDs for all cells.
- Function(s): `main()` (Pattern C — links `mappack_loader.cpp`).
- Build: `g++ -std=c++17 -Isrc/user tests/material_catalog_test.cpp src/user/gameplay/world/mappack_loader.cpp`.

#### Edge cases
- A material index with no sprite → flat fallback (never a crash).
- `TB_empty` null slot → renderer skips the batch (kill volumes).
- RDP state-change cost: if the near pass is too slow with textures, the
  `kEnableTextures` flag drops to flat-color (documented fallback).

#### Verification
- Run: `python3 tests/render_pipeline_contract.py` (material-index stability);
  host test `material_catalog_test.cpp`; `./compile-rom.sh`.
- Done: near-pass cells are textured; distant cells stay flat; the flag can
  disable textures.

### Inc 6 — Fog + skybox + atmospheric fade (M)

**Depends on:** 2, 4
**Unblocks:** 7
**Done criteria:** the distant pass blends into configurable fog, hiding the
   distant/near transition; a skybox is drawn before both passes (`arch.md` §27);
   a host test asserts the fog parameter math and skybox transform.

Note: this depends on Inc 4 because fog is applied in `DistantWorldRenderer`,
which is created in Inc 4. It also depends on Inc 2 for the two-pass frame
structure (fog is applied/removed between the distant and near passes).

#### Files to touch

##### src/user/gameplay/render/fog_math.hpp (new)
- What changes: pure fog-parameter math, host-safe.
- Function(s):
  - `inline FogParams MakeFog(float min_dist, float max_dist,
    const Vec3& color)` — clamps `min_dist` to a sane maximum (per `arch.md`
    §27).
  - `inline bool ValidateFogRange(const FogParams&)`.
- Data shapes: `FogParams { bool enabled; Vec3 color; float min; float max; }`.
- Integration points: `DistantWorldRenderer` applies fog mode before the
  distant pass; host test validates range.

##### src/user/gameplay/render/skybox.{hpp,cpp} (new)
- What changes: a simple skybox drawn before the distant pass. Uses its own
  camera-relative transform (`arch.md` §27) and disables fog/Z so it sits
  behind all world geometry. In the first version it can be a color/gradient
  dome or a textured cube; the API is:
  - `void Init(const char* sprite_path_or_null)`
  - `void Draw(const CameraDesc& cam)`
- Function(s): `Draw()` — set a fixed depth model-view, render a centered cube
  or dome, then restore the camera viewport for the distant pass.
- Data shapes: skybox mesh + optional sprite.
- Integration points: called first in `OpenWorldRenderer::Render()` before
  `RenderDistant()`.
- Error paths: missing sprite falls back to a flat-colored dome.

##### src/user/gameplay/render/distant_world_renderer.{hpp,cpp}
- What changes: before rendering distant cells, configure `rdpq_mode_fog(...)`
  with the fog color/range (per `arch.md` §26-27). Restore the non-fog mode
  for the near pass.
- Function(s): `Render()` — add fog setup/teardown.
- Data shapes: `FogParams` member.
- Integration points: `OpenWorldRenderer`.
- Error paths: fog only applied if `enabled`; always torn down before the near
  pass.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: `Render()` calls `open_world_.Render()`, which now begins with
  the skybox, then distant fog, then near passes.
- Function(s): `Render()`.

##### tests/fog_math.cpp (new)
- What changes: host test that `MakeFog` clamps `min_dist` correctly and that
  `ValidateFogRange` rejects an inverted range.
- Function(s): `main()`.

##### tests/skybox_transform.cpp (new)
- What changes: host test that the skybox model-view keeps the camera at the
  center of the cube and is independent of the camera target.
- Function(s): `main()`.

#### Edge cases
- `min_dist > max_dist` → clamp/reject.
- Fog must not leak into the near pass (torn down after the distant pass).
- Skybox must not leak depth/Z state into the distant pass.

#### Verification
- Run: host tests `fog_math.cpp`, `skybox_transform.cpp`; `./compile-rom.sh`.
- Done: the skybox sits behind the world; the distant horizon fades into
  atmospheric fog; the near pass is unaffected.

### Inc 7 — Per-phase profiler + host test runner + budgets (M)

**Depends on:** 2, 3, 4, 5, 6 (must be last — it wraps every pass and
aggregates the tests produced across Inc 1-6)
**Unblocks:** none
**Done criteria:** a host test runner executes all host C++ + Python tests with
one command; a per-phase profiler reports per-pass timing (distant, near,
particles, texture-upload, streaming); the renderer enforces documented
per-frame budgets.

#### Files to touch

##### tests/run_host_tests.sh (new)
- What changes: a shell script that builds + runs every host C++ smoke test and
  runs every Python test. Single entrypoint: `./tests/run_host_tests.sh`.
  Exit nonzero on any failure.
- **Explicit test inventory with build commands:**

  **Pattern A (header-only, no N64 deps):**
  ```
  g++ -std=c++17 -Isrc/user tests/camera_space_math.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/pass_camera_math.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/tile_visibility_contract.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/frame_order_contract.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/lod_math.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/fog_math.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/skybox_transform.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/render_budgets_contract.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/debug_visualization_contract.cpp -o /tmp/t && /tmp/t
  ```

  **Pattern C (links `mappack_loader.cpp`):**
  ```
  g++ -std=c++17 -Isrc/user tests/tile_streamer_smoke.cpp \
    src/user/gameplay/world/mappack_loader.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/tile_stream_lru_contract.cpp \
    src/user/gameplay/world/mappack_loader.cpp -o /tmp/t && /tmp/t
  g++ -std=c++17 -Isrc/user tests/material_catalog_test.cpp \
    src/user/gameplay/world/mappack_loader.cpp -o /tmp/t && /tmp/t
  ```

  **Python contracts:**
  ```
  python3 tests/interconnected_map_contract.py
  python3 tests/interconnected_seam_equivalence.py
  python3 tests/render_pipeline_contract.py
  python3 tests/distant_lod_contract.py
  ```

- Integration points: documented in `AGENTS.md`; `make test` target invokes it.
- Error paths: any failing test aborts with its output.

##### Makefile
- What changes: add a `test` target that runs `tests/run_host_tests.sh`
  (host-side; no N64 toolchain needed). Keep `.PHONY` updated.
- Function(s): `test`.

##### src/user/n64/profiler.{hpp,cpp}
- What changes: add per-phase timers (distant, near, particles, texture-upload,
  streaming) in addition to the existing whole-frame timer. Each phase gets a
  named scope that reports its rolling average.
- Function(s): `FrameProfiler::BeginPhase(name)` / `EndPhase(name)`; a phase
  table with rolling ms/fps per phase.
- Data shapes: fixed phase-name array.
- Integration points: `OpenWorldRenderer::Render` marks each phase.
- Error paths: an unclosed phase asserts at frame end.

##### src/user/n64/frame_arena.{hpp,cpp} (new)
- What changes: a simple frame-scoped arena allocator for temporary per-frame
  allocations (visible tile lists, distant sort arrays, matrix scratch).
  Mirrors `arch.md` §30 ("frame memory pool for transient allocations") and
  §40 ("Temporary frame allocations use a frame allocator"). The arena is
  reset at the start of each frame; all render passes allocate from it.
- Function(s):
  - `class FrameArena` — owns a fixed-size buffer (e.g. 64 KB). `void*
    Alloc(size_t)` bumps a pointer (no free); `void Reset()` rewinds to
    start. `size_t Used() const` and `size_t Remaining() const` for budget
    tracking.
  - `MemorySnapshot::CaptureArena(const FrameArena&)` — extends the existing
    `MemorySnapshot` to report arena usage alongside heap stats.
- Data shapes: fixed `uint8_t buffer[kArenaSize]`; `size_t offset_`.
- Integration points: `OpenWorldRenderer::Render` calls `arena_.Reset()` at
  frame start; `TileStreamer` and `DistantWorldRenderer` allocate visible-tile
  lists and sort arrays from the arena. `profiler` reports arena high-water
  mark per frame.
- Error paths: `Alloc` exceeding remaining capacity is a hard assert (budget
  violation).

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: wrap each pass in a profiler phase scope.
- Function(s): `Render()` — add phase markers.
- Integration points: `profiler`.

##### src/user/gameplay/render/render_budgets.hpp (new)
- What changes: hard-coded per-frame budget constants + host-testable asserts.
  Mirrors `arch.md` §31 (`maximum visible terrain chunks`, `LOD entries`,
  `triangles`, `material changes`, `particles`, `texture uploads`, `streaming
  operations`). **Concrete default caps (named as tuning constants to be
  adjusted at the first Ares measurement, per the open-world budget):**
  - `kMaxVisibleCells = 9` (near ring: center + 8; distant cells are counted
    separately against `kMaxDistantCells`).
  - `kMaxDistantCellsPerFrame = 64` (all 45 cells at coarse LOD, with headroom).
  - `kMaxTrianglesPerFrame = 6000` (near + distant combined; the current world
    is ~10.6k tris at full detail, so distant LOD + near ring must stay under
    this via LOD).
  - `kMaxMaterialChangesPerFrame = 128` (near textured batches; RDP state
    changes are the expensive path).
  - `kMaxTextureUploadsPerFrame = 64`.
  - `kMaxStreamOpsPerFrame = 4` (tile loads/evictions per frame).
  - `kMaxParticlesPerFrame = 128` (reserved; particles are deferred but the
    budget field exists).
- Function(s):
  - `inline bool BudgetsExceeded(const RenderCounts&)` — true if ANY count
    exceeds its cap.
  - struct `RenderCounts { int visible_cells; int distant_cells; int triangles;
    int material_changes; int texture_uploads; int stream_ops; int particles; }`.
- Data shapes: `RenderCounts` with the above default caps.
- Integration points: `OpenWorldRenderer`/`TileStreamer`/`DistantWorldRenderer`
  increment counts; assert after the frame.
- Error paths: exceeding a budget is a hard assert (or a documented throttling
  path: shrink the ring / raise LOD distance / disable textures).

##### src/user/gameplay/render/debug_visualization.hpp (new)
- What changes: optional debug render overlays exposing tile boundaries, LOD
  level, pass isolation, and streaming state per `arch.md` §43. Host-safe math
  helpers plus device-only debug draw functions.
- Function(s):
  - `inline uint32_t DebugColorForLod(int lod_level)` — maps LOD level to a
    color.
  - `inline uint32_t DebugColorForPass(const char* pass_name)` — maps pass
    names ("distant", "low_priority", "high_priority", "skybox") to colors.
  - `void DrawTileBoundary(const Vec3& origin, float size, uint32_t color)`
    (device-only) — line outline of a tile/cell boundary.
  - `void DrawStreamingOverlay(int resident_count, int visible_count)`
    (device-only) — small screen-space indicator.
- Data shapes: simple color table.
- Integration points: `OpenWorldRenderer` calls the device helpers when
  `cvar_debug_renderer != 0`; host tests validate the color table and
  boundary math.
- Error paths: debug overlay is skipped if budgets would be exceeded; it
  contributes to the triangle/material budgets.

##### tests/render_budgets_contract.cpp (new)
- What changes: host test that `BudgetsExceeded` behaves for boundary values
  (at-cap passes, over-cap fails).
- Function(s): `main()`.

##### tests/debug_visualization_contract.cpp (new)
- What changes: host test that `DebugColorForLod`/`DebugColorForPass` are
  distinct and that tile-boundary line vertices form a closed square.
- Function(s): `main()`.

#### Edge cases
- Profiler phase not closed → assert at frame end (catches renderer bugs).
- Budgets must be configurable (compile-time or config) so the 240-unit ×
  45-cell world can be tuned without editing constants.
- The test runner must run cleanly on a host without the N64 toolchain (all
  host tests are toolchain-independent).

#### Verification
- Run: `./tests/run_host_tests.sh` (all host + Python tests green); build +
  run `tests/render_budgets_contract.cpp`; `./compile-rom.sh`.
- Done: one command runs the whole host suite; the profiler reports per-phase
  timing; budgets are enforced.

## Cross-cutting verification

- Every increment: `./compile-rom.sh` builds `madeline_cube_rom.z64` cleanly.
- After Inc 3: `./tests/run_host_tests.sh` (when added in Inc 7) or the manual
  per-file build commands (before Inc 7) all pass.
- `python3 tests/interconnected_map_contract.py` and
  `python3 tests/interconnected_seam_equivalence.py` must continue to pass
  (the cell-resolution formula must not fork a 4th copy).
- Final acceptance (Ares, not Mupen64Plus/glide — see `AGENTS.md` and
  `/memories/repo/emulators.md`): walk the whole Forsaken City world and
  confirm (a) no Z-fighting, (b) no popping at LOD boundaries, (c) no holes,
  (d) no visible tile loading, (e) no coordinate precision artifacts,
  (f) smooth continuous movement. Use the per-phase profiler to confirm no
  phase regresses.
- The Inc 1 Ares seam walk (camera-relative view/model coupling, which is not
  host-testable) is a mandatory prerequisite before any later increment is
  trusted on device.

## Standards / common-mistakes referenced

- `AGENTS.md` — preserve gameplay/ROM separation; rebuild ROM after
  N64-facing changes; use Ares not Mupen64Plus/glide for validation.
- `.agents/map-creation.md` — canonical transform, kPosFp fixed-point
  constraints, MaterialCatalog manifest index coupling, capacity constants.
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to Inc 4
  (distant decimation must not reverse winding).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to Inc 5 (sprite
  paths must be `rom:/tex/`).
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to Inc 1
  (camera-relative matrices must reset correctly on respawn).
- `.agents/common-mistakes/missing-player-start-init.md` — applies to Inc 3
  (distinguish initial spawn vs transition carry).

## arch.md §40 requirement coverage matrix

| # | `arch.md` requirement | Plan increment(s) | Verification |
|---|-----------------------|-------------------|--------------|
| 1 | Two principal passes (Distant World + Near World) with distinct precision | Inc 2 | `frame_order_contract.cpp`, `pass_camera_math.cpp` |
| 2 | Distant pass: Z-buffer disabled | Inc 2 (stub), Inc 4 (real) | `frame_order_contract.cpp`, Ares distant/near overlap check |
| 3 | Distant pass: reduced coordinate scale (`lod_scale`) | Inc 4 (Inc 1 defines the packing mechanism only; `kLodScale` is chosen in Inc 4) | `distant_lod_contract.py` asserts `distant_far/kLodScale <= 32767` |
| 4 | Distant pass: aggressively reduced geometry | Inc 4 | `distant_lod_contract.py` inspects coarse mesh face/vertex counts |
| 5 | Distant pass: LOD hierarchy | Inc 4 | `distant_world_renderer` data shape; Ares no popping at LOD boundaries |
| 6 | Distant pass: manual back-to-front ordering | Inc 4 | `distant_pass_order.cpp` asserts distance-priority monotonicity |
| 7 | Distant pass: directional mesh selection | Inc 4 | `BuildDistantRenderList` directional index; Ares silhouette check |
| 8 | Distant pass: frustum culling via 2D clipping planes | Inc 4 | `BuildDistantRenderList` clipping-plane cull; host test on synthetic frustum |
| 9 | Distant camera: `near = far * 0.25 * lod_scale`, `far = tile_size * 1.4` | Inc 2 | `pass_camera_math.cpp` exact formula assertion |
| 10 | Distant world transform: compressed + static scale | Inc 4 | `distant_world_renderer.cpp` matrix construction |
| 11 | Near pass: Z-buffer enabled | Inc 2 | `frame_order_contract.cpp` stage order |
| 12 | Near pass: camera-relative coordinates | Inc 1, Inc 3 | `camera_space_math.cpp`, `render_pipeline_contract.py` |
| 13 | Near visibility: top-view frustum polygon (math in `tile_visibility.hpp`, created in Inc 2; used by the streamer in Inc 3) | Inc 2 (math), Inc 3 (used) | `tile_visibility_contract.cpp`, `tile_streamer_smoke.cpp` |
| 14 | Near visibility: scanline tile enumeration (math in `tile_visibility.hpp`, created in Inc 2; used by the streamer in Inc 3) | Inc 2 (math), Inc 3 (used) | `tile_visibility.hpp`, `tile_streamer_smoke.cpp` |
| 15 | Streaming: load/evict, `load_next` deferral | Inc 3 | `tile_stream_lru_contract.cpp` |
| 16 | Frame order: Distant → Low-priority (Z-off) → Main terrain (Z-on); particle pass **deferred** (see Out of scope) | Inc 2 (subpass stubs), Inc 4/6 (real) | `frame_order_contract.cpp` enum order; Ares frame check |
| 17 | RSPQ render blocks | Inc 5 | `textured_room_renderer` precompiles material batches; host test asserts block reuse |
| 18 | Material batching (change only when necessary) | Inc 5 | `render_budgets.hpp` `kMaxMaterialChangesPerFrame`; Ares state-change check |
| 19 | Fog | Inc 6 | `fog_math.cpp`, Ares distant/near transition |
| 20 | Skybox transform | Inc 6 | `skybox_transform.cpp` |
| 21 | Profiling / per-phase timing | Inc 7 | `profiler` phase scopes, Ares per-phase timing output |
| 22 | Bounded budgets (visible chunks, LOD entries, triangles, material changes, particles, texture uploads, streaming ops) | Inc 7 | `render_budgets_contract.cpp` |
| 23 | Debug visualization modes | Inc 7 | `debug_visualization_contract.cpp` |
| 24 | Temporary frame allocations use a frame allocator | Inc 7 | `profiler` `MemorySnapshot` extended to track frame-arena usage; Ares memory report |
| 25 | Rendering work is represented by bounded lists | Inc 3 (tile list), Inc 4 (distant sort list), Inc 7 (budgets enforce caps) | `render_budgets_contract.cpp` asserts all list sizes ≤ caps |

## Open questions (CONSIDER from review)

- Inc 3 and Inc 4 both edit `open_world_renderer`; serializing 3 before 4 is
  justified by file-conflict coupling. The distant pass and near streaming are
  functionally independent, but the §41 phase ordering plus the shared
  `open_world_renderer` edit force the sequence. **Accepted as a deliberate
  ordering edge** — Inc 4 depends on Inc 3 primarily for file-conflict and
  phase ordering, not for a hard functional requirement.
- Inc 5's new `.manifest` overlaps existing per-cell `lvl.strings` material
  names; ensure the emitted order matches so the texture↔material mapping stays
  consistent with `material_color()` ids.
- Inc 1's camera-space math is only ROM-verifiable end-to-end (the view/matrix
  coupling is t3d-only). The Ares seam walk is the authoritative check — make
  sure it happens before trusting later increments on device.
- Budget constants (Inc 7) are initial estimates; they must be recalibrated at
  the first Ares measurement against the real world's triangle count and RDP
  state-change cost.
- Inc 5 depends on Inc 4 purely to enforce §41 phase ordering (Phase 5 after
  Phase 4); the texturing itself only needs Inc 3. This is intentional so the
  material pipeline cannot land before the distant pass it visually must
  integrate with.

## Out of scope

- Full baked AO/texture-bake distant terrain (user: "will work in textures
  later" — distant stays procedural + flat for now).
- B-side `1-1`…`1-10` map-pack conversion and cassette scene-stack semantics.
- Full OG gameplay parity (moving blocks, NPCs, cutscenes, fixed cameras).
- Water/particles/dynamic-object passes from spec Phase 6 — the plan reaches
  through the distant pass + texturing + fog; particles/water are explicitly
  deferred unless a later increment adds them.
- Per-region atmosphere (`arch.md` §28 — locally colored fog per region).
  The plan implements global fog only; per-region atmosphere is a future
  extension.
- Minimal proof-of-concept test scene (`arch.md` §42 — 4×4 terrain chunks,
  3 LOD levels). The plan jumps straight to the 45-cell Forsaken City world.
  If the full world proves too heavy for initial debugging, a 2×2 or 3×3 test
  scene can be added as a gating milestone before Inc 4.

## Budget recalibration process

The budget constants in Inc 7 (`kMaxTrianglesPerFrame = 6000`, etc.) are
initial estimates. After the first Ares boot with all passes active:

1. Run the per-phase profiler (Inc 7) and record actual counts for each budget
   field during a full Forsaken City traversal.
2. If any budget is exceeded, either raise the cap (if the N64 can handle it)
   or reduce the workload (shrink the near ring, raise LOD distance, disable
   textures).
3. Update the constants in `render_budgets.hpp` and re-run
   `tests/render_budgets_contract.cpp` to confirm the new caps.
4. Document the calibrated values in this plan's "Architectural decisions"
   section.
