# N64 Render Performance Fixup (30 fps)

## Context

The two-pass open-world renderer (all increments of `.plans/n64-open-world-renderer/`
are implemented) boots and runs on device, but the ROM lags hard — well below
30 fps. The bottleneck is in the render passes, not gameplay: redundant distant
draws, per-face RDP state thrash (texture uploads, vertex loads, pipe/RSP
syncs), no visibility culling, and per-frame USB debug output. This fixup makes
the ROM hit a solid 30 fps without changing gameplay rules or the two-pass
architecture (`arch.md` §21 frame order stays).

Target measured on device (Ares) with the Inc-7 per-phase profiler:
`avg frame time ≤ 33.3 ms`, with the distant + high-priority passes inside that
budget.

## Architectural decisions

- **D1 — Distant pass draws each cell once.** `DistantWorldRenderer::Load`
  points all four directional `meshes[d]` slots at the *same* `LvlRoomRenderer*`,
  and `Render()` loops all four slots → every distant cell is drawn 4× (45
  cells → 180 mesh draws). Fix: draw `meshes[0]` only. The 4-slot data shape
  stays for future directional variants; `distant_pass_order.cpp` already
  tests the list builder, not the slot loop.
- **D2 — Distant pass frustum-culls.** Only cells inside the distant camera's
  view cone + depth range are drawn (45 → ~10-15). New host-testable
  `CullDistantCells(...)` in `lod_math.hpp`; renderer consumes it. No new
  N64 math needed (Vec3 only).
- **D3 — Material-run batch coalescing (fan-preserving).** Both
  `LvlRoomRenderer` and `TexturedRoomRenderer` merge **adjacent same-material
  faces at load time** into runs. A run shares one RDP-state block (sprite
  upload, combiner, prim color) plus one `t3d_vert_load` + one `t3d_tri_sync`,
  but **emits each face as its own fan** (a run stores the sub-range of its
  faces) so triangulation never crosses a face boundary. Runs are capped at
  the RSP vertex-load limit of **70 vertices** (`T3D_VERTEX_CACHE_SIZE`;
  `t3d_vert_load`'s `count` is in vertices, 1-70, rounded up to even pairs),
  NOT 70 pairs. In the near pass this is the difference between ~1350 TMEM
  sprite uploads/frame and ~10-25.
- **D4 — Near pass visibility culling.** Wire the existing-but-unused
  `ResolveVisibleTiles` path: the orchestrator computes `inv_view_proj` from
  the near camera, `TileStreamer::UpdateCamera(pos, inv_view_proj, ground_y)`
  marks visible residents, and `DrawHighPriority` draws only visible cells.
  The center cell is always drawn (transition/respawn safety). 9 residents →
  typically 1-4 drawn.
- **D5 — Debug-serial gating.** Per-frame `debugf` in `GameplayScene::Update`
  (`[update] frame start`, `[tick%d] pre-step`) moves behind
  `kVerboseFrameLogging` (default false). USB serial output blocks and is a
  large hidden per-frame cost. Boot/init logs and the 60-frame profiler report
  stay (they are not per-frame hot).
- **D6 — Memory/stack diet.** (a) Both renderers heap-allocate their batch
  array sized to the actual face count instead of an embedded
  `Batch batches_[1024]` (16 KB each — ~720 KB across 45 distant cells).
  (b) Per-frame scratch — the distant render list `order[]` and the per-frame
  visible-tile snapshot — moves into the currently-unused 64 KB `FrameArena`
  (reset per frame in `OpenWorldRenderer::BeginFrame`). The **resident ring
  and its renderers stay persistent member state** (never the arena — the
  arena is reset every frame and would thrash streaming state). Only
  per-frame draw snapshots live in the arena.
- **D7 — Profiler completeness.** Wire the unused `kPhaseTextureUpload` and
  `kPhaseStreaming` phases and add per-pass draw counters (batches, texture
  uploads, vertex loads, syncs, cells drawn) to `FrameProfiler`/`OpenWorldRenderer`
  so every increment is measured on device with hard numbers.

Alternatives rejected:
- **Flat-color near pass** (drop textures): rejected — user wants textures kept
  and made fast; D3 achieves the texture-upload collapse with `kEnableTextures`
  still on.
- **Global face re-sort by material in the distant pass**: rejected — the
  distant pass runs Z-off and relies on face order within a cell; only
  *adjacent* coalescing (D3) preserves order. A global near-pass sort is
  parked as a CONSIDER item if texture uploads still dominate after D3.
- **Per-frame CPU room-AABB culling**: rejected — the scanline ground
  projection (`ResolveVisibleTiles`) already exists, is host-tested, and is
  cheaper than a full AABB pass.

## Assumptions and answers from code

- Frame target 30 fps — user-confirmed.
- Keep textured near pass (`kEnableTextures = true`), make it fast — user-confirmed.
- Gate per-frame debugf behind a flag, default off — user-confirmed.
- `ResolveVisibleTiles`, `ScanlineTileRanges`, `ProjectFrustumToGround`, `Mat4`
  exist and are host-tested — code @ `src/user/gameplay/render/tile_visibility.hpp`.
- `TileStreamer::UpdateCamera(const Vec3&, const Mat4&, float)` exists as a
  no-op stub — code @ `src/user/gameplay/render/tile_streamer.cpp:96`.
- `FrameArena` (64 KB) is reset each frame but nothing allocates from it —
  code @ `src/user/gameplay/render/open_world_renderer.cpp` (`BeginFrame`); no
  `arena_.Alloc` call exists in `src/`.
- Distant `entries_[64]` cap, 45 cells; `kMaxBatches = 1024` in both
  renderers; RSP vertex-load cap = **70 vertices** (`T3D_VERTEX_CACHE_SIZE`;
  `t3d_vert_load`'s `count` is in vertices, 1-70, and always loaded in pairs);
  each face fans from its own `first_vertex` (`tri_count = vertex_count - 2`);
  near `kPosScale = 32`, distant `kLodScale = 0.25` — code +
  `tiny3d/src/t3d/t3d.{h,c}`.
- Fixed-step accumulator runs 60 Hz ticks; render loop is uncapped — target is
  render ≤ 33.3 ms so the loop holds 30 fps without accumulator debt.

## Risks accepted

- **Coalescing geometry regression**: merging faces into one run must preserve
  each face's fan origin or the run renders garbage triangles (a run-wide fan
  crosses face boundaries). Mitigation: runs store their per-face sub-ranges
  and `Draw` fans each face from its own origin; a host contract test asserts
  the coalesced run emits the **same set of triangles** as the uncoalesced
  face list, span ≤ 70 vertices, first/last face preserved, and
  `DiscardedFaces()==0` stays true.
- **Visibility culling pops at frustum edges**: the scanline enumerator is
  conservative (over-includes) and the center cell is always drawn; `SetCenter`
  (transition path) is untouched.
- **Distant cone cull too aggressive**: using vertical FOV for the cone would
  drop cells at the horizontal screen edges. Mitigation: hfov = 2·atan(tan(fov/2)
  ·aspect) with aspect 4:3; host test covers a camera pointed away (expect 0).
- **debugf gating hides diagnostics**: single documented flag; telemetry +
  profiler reports remain enabled (60-frame cadence).

## Increment DAG

- Inc 1 — Instrument & gate debug (S) — depends: none — unblocks: 2, 3, 4, 5, 6
- Inc 2 — Distant: dedupe + frustum cull (M) — depends: 1 — unblocks: 5
- Inc 3 — Near: material-run coalescing (M) — depends: 1 — unblocks: 4, 5
- Inc 4 — Near: visibility culling (M) — depends: 3 — unblocks: 5, 6
- Inc 5 — Memory: compact batches + frame arena (M) — depends: 2, 3, 4 — unblocks: 6
- Inc 6 — 30 fps tuning pass (S) — depends: 4, 5 — unblocks: —

```
Inc1 ──► Inc2 ──┬────► Inc5 ──► Inc6
  │             │
  └──► Inc3 ──► Inc4 ──►┘
```

Note: Inc 5 depends on Inc 4 because Inc 4 introduces the per-frame `visible_`
mask + visible-tile snapshot that Inc 5 then arena-allocates, and both touch
`tile_streamer.cpp` / `open_world_renderer.cpp`.

## Increments

### Inc 1 — Instrument & gate debug (S)
**Depends on:** none
**Unblocks:** 2, 3, 4, 5, 6
**Status:** done (18/18 host tests pass; ROM builds clean, no warnings)
**Done criteria:** on device, the per-frame `[update]`/`[tick]` lines are gone;
the profiler prints distant + high_priority + texture_upload averages with draw
counters; baseline numbers captured.

#### Files to touch

##### src/user/gameplay/debug_flags.hpp (new)
- What changes: single home for the verbose-logging gate.
- Function(s): `inline constexpr bool kVerboseFrameLogging = false;`
- Integration points: included by `gameplay_scene.cpp`.
- Error paths: none (compile-time constant).

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: gate the two per-frame `debugf` calls in `Update()` behind
  `kVerboseFrameLogging`; remove the unconditional `[update] frame start` +
  `[tick%d] pre-step` prints (keep them behind the flag). Add a small
  `#include "gameplay/debug_flags.hpp"`.
- Function(s): `GameplayScene::Update(float)` — wrap debug output.
- Integration points: none else (boot logs stay).
- Error paths: none.

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: expose draw counters + wire the unused profiler phases.
- Function(s):
  - `struct RenderCounters { uint32_t distant_cells; uint32_t near_batches; uint32_t texture_uploads; uint32_t vert_loads; uint32_t syncs; };`
  - `const RenderCounters& Counters() const;` (reset per frame in `BeginFrame`).
- Integration points: `RenderDistant`/`RenderHighPriority` increment counters;
  `FrameProfiler::kPhaseTextureUpload` wraps `TexturedRoomRenderer::Draw`.
- Error paths: counters are diagnostics; zeroed each frame.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: increment `counters.distant_cells` per cell drawn (slots
  handled in Inc 2; this increment only counts).
- Error paths: none.

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: increment `counters.texture_uploads` when `rdpq_sprite_upload`
  runs and `counters.vert_loads`/`syncs` per batch; expose via
  `OpenWorldRenderer::Counters()` (thread through `TileStreamer`).
- Error paths: none.

##### src/user/n64/profiler.{hpp,cpp}
- What changes: ensure `kPhaseTextureUpload` and `kPhaseStreaming` are reported
  (already in `kPhaseNames`; nothing to change beyond ensuring phases are
  emitted — see open_world_renderer wiring above).

#### Edge cases
- Flag must default false (release behavior); host tests never depend on the
  debug prints.
- Profiler report (every 60 frames) is one `debugf` burst — acceptable; do not
  gate it.
- The 60-frame `telemetry.PrintLine()` burst is small but can spike one frame;
  gate it behind `kVerboseFrameLogging` too (it is diagnostics, not runtime).
- `kPhaseStreaming` emission is explicitly deferred to Inc 6 (see Open
  questions) — Inc 1 wires only `kPhaseTextureUpload` (D7).

#### Verification
- Run: `./tests/run_host_tests.sh` (17 pass) + `./compile-rom.sh` (clean build).
- Tests to add/update: `tests/debug_flags_contract.cpp` (Pattern A) asserting
  `kVerboseFrameLogging == false`.
- Done: Ares boot shows no per-frame `[update]`/`[tick]` spam; profiler prints
  per-phase averages + counters; baseline (ms) recorded in the plan's
  cross-cutting verification notes.

### Inc 2 — Distant: dedupe + frustum cull (M)
**Depends on:** 1
**Unblocks:** 5
**Status:** done (19/19 host tests incl. new distant_cull_contract; ROM builds clean, no warnings)
**Done criteria:** distant phase ms drops ~4-6× on device; `Counters().distant_cells`
≤ ~15 while looking across the map; `distant_pass_order` host test still passes.

#### Files to touch

##### src/user/gameplay/render/lod_math.hpp
- What changes: add the host-testable distant cull predicate.
- Function(s):
  ```cpp
  // Returns true if `origin` is inside the camera's horizontal view cone
  // and depth range. hfov is the horizontal half-FOV (radians) used for the
  // cone. Accepts a `cone_margin` multiplier (>1 = wider cone).
  inline bool CellInDistantFrustum(const Vec3& cam_pos, const Vec3& cam_target,
                                   float hfov_deg, float near_d, float far_d,
                                   const Vec3& origin, float margin = 1.15f);
  ```
- Data shapes: pure Vec3/float; host-safe.
- Integration points: called by `BuildDistantRenderList` (below).
- Error paths: `near_d >= far_d` → returns false (empty frustum).

##### src/user/gameplay/render/distant_world_renderer.hpp (inline list builder)
- What changes: extend `BuildDistantRenderList` with optional culling.
- Function(s): add `bool cull` parameter (default true) OR a second function
  `BuildDistantRenderListCulled(...)`; keeps `distant_pass_order.cpp` passing
  (it may keep calling the unculled variant).
- Data shapes: `out[]` filled only with in-frustum cells; return count.
- Integration points: `DistantWorldRenderer::Render`.
- Error paths: out_capacity respected; empty list → nothing drawn (fog-only).

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: (a) `Render()` draws `meshes[0]` only (D1); (b) compute
  `hfov = 2·atan(tan(fov_deg/2)·(4/3))` from `cam.fov_deg` and pass to the
  culled list builder; (c) increment `counters.distant_cells`.
- Function(s): `void Render(const CameraDesc& cam)` — replace the
  `for d in 0..3` slot loop with a single draw.
- Integration points: `OpenWorldRenderer::RenderDistant`.
- Cull source camera: use the `distant_cam` from `BuildPassCameras` (the cull
  `near_d`/`far_d` come from `distant_cam.near`/`distant_cam.far`, NOT the
  20/800 near camera — the cull must match the distant pass's clip range so
  cells behind the near far-plane are still drawn in the distant pass).
- Error paths: entry with null `meshes[0]` skipped (already handled).

#### Edge cases
- Camera pointed straight down or away from all cells → 0 cells drawn (fog +
  sky only) — acceptable, near pass still covers the ring.
- Cone margin: too tight pops horizon cells; too wide draws the whole map.
  Start at 1.15×, tune in Inc 6.
- `distant_pass_order.cpp` must keep passing: it exercises `BuildDistantRenderList`
  ordering — keep the unculled overload so the sort contract is unchanged.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/distant_cull_contract.cpp` (Pattern A):
  camera at cell_00_00 facing +X → only +X-side cells survive; camera facing
  -X → the +X cells culled; full-reverse → 0 cells; near/far clamps.
- Done: distant phase on device ≈ 1/4 of pre-fixup; no horizon popping during
  a 360° turn (fog covers the transition).

### Inc 3 — Near: material-run coalescing (M)
**Depends on:** 1
**Unblocks:** 4, 5
**Status:** done (20/20 host tests incl. new batch_coalesce_contract; ROM builds clean, no warnings)
**Done criteria:** `Counters().texture_uploads` drops from ~per-face to ~per-run
(≤ ~25 for a full ring); `[texroom] loaded … discarded 0` unchanged; the
coalesced draw emits the **same triangles** as the uncoalesced face list
(host contract — see MUST-FIX #1: a single run-wide fan crosses face
boundaries and renders garbage).

#### Files to touch

##### src/user/gameplay/render/batch_coalesce.hpp (new)
- What changes: host-testable run-coalescing shared by both renderers.
- Function(s):
  ```cpp
  struct FaceSpec { uint32_t first_vertex; uint32_t vertex_count;
                    uint32_t tri_count; uint16_t material_id; };
  struct BatchRun {
      uint32_t first_vertex;  // run span start (absolute vertex index)
      uint32_t vertex_count;  // run span length (≤ max_span vertices)
      uint16_t material_id;
      uint16_t first_face;    // index into out_faces (first face of this run)
      uint16_t face_count;    // faces in this run (adjacent, same material)
  };
  struct RunFace { uint32_t offset;   // face origin relative to run.first_vertex
                   uint32_t tri_count; };
  // Merge adjacent same-material faces into runs, splitting when the run span
  // would exceed max_span (70 vertices) or the material changes. out_faces is
  // a flattened per-face list (grouped by run, indexed by BatchRun.first_face).
  // Returns the number of runs (or -1 if either capacity is exceeded).
  int CoalesceBatches(const FaceSpec* src, int n, BatchRun* out, int out_cap,
                      RunFace* out_faces, int face_cap, uint32_t max_span);
  ```
- Data shapes: as above; `RunFace.offset` is within the run's loaded buffer, so
  `Draw` can fan each face from `offset + (run.first_vertex & 1)`.
- Integration points: `LvlRoomRenderer::Load`, `TexturedRoomRenderer::Load`.
- Error paths: capacity exceeded → -1 and `Load` falls back to per-face batches
  (never a silent truncation); `DiscardedFaces()` semantics unchanged.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: keep the per-face batch list (as today) AND the coalesced run
  list + flattened `RunFace` sub-ranges from `CoalesceBatches`.
- Function(s): `Load()` builds both; `Draw()` rewrites to:
  ```
  for each run:
      rdpq_set_prim_color(run.material_color)          // once per run
      t3d_vert_load(verts_ + run.first_vertex/2, 0, run.vertex_count)  // once per run (≤70 verts)
      for each face in run (run.first_face .. +run.face_count):
          emit that face's OWN fan from its RunFace.offset (NOT the run start)
      t3d_tri_sync()                                    // once per run
  ```
- Integration points: `Draw()` — the fan origin is per-face (MUST-FIX #1);
  `base_vertex` odd/even alignment is preserved via `offset + (first_vertex&1)`.
- Error paths: run span > 70 → split at load time (never at draw; MUST-FIX #2).

##### src/user/gameplay/render/textured_room_renderer.{hpp,cpp}
- What changes: same run + `RunFace` structure; `Draw()` sets the combiner +
  `rdpq_sprite_upload(TILE0, sprite)` **once per run**, prim color once, then
  fans each face. Increment `counters.texture_uploads` per run (Inc 1 wiring).
- Error paths: material with no sprite → flat color for the whole run (never
  per-face); same span/cap rules as LvlRoomRenderer.

#### Edge cases
- Adjacent same-material faces with a span > 70 vertices → split into multiple
  runs (cost reverts to per-run, never worse than per-face).
- Faces sharing vertices across runs are fine — each run loads its own span.
- Order preservation is load-order (adjacent only) → Z-off distant pass stays
  correct (Inc 3 also applies to `LvlRoomRenderer`, which the distant pass
  uses; adjacent-only preserves the back-to-front face order within a cell).
- A run's loaded span must be rounded to an even vertex count for
  `t3d_vert_load` (it loads in pairs); cap is 70 **vertices** (35 pairs).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/batch_coalesce_contract.cpp` (Pattern A):
  (a) 3 consecutive same-material faces → 1 run, span ≤ 70; (b) span cap splits
  into 2 runs; (c) different-material neighbors stay separate; (d) **geometry
  equivalence** — replay the run list with per-face fan origins and assert the
  exact same (a,b,c) triangle index sets as the uncoalesced faces; (e) first/last
  face preserved.
- Done: device `texture_uploads` ≈ distinct material runs in the visible ring
  (5-25), not face count; `discarded 0` unchanged; Ares check of a near cell
  shows no holes/artifacts.

### Inc 4 — Near: visibility culling (M)
**Depends on:** 3
**Unblocks:** 5, 6
**Status:** done (21/21 host tests incl. new near_visibility_contract; ROM builds clean, no warnings)
**Done criteria:** `Counters().near_batches` drops to the visible subset;
`ResolveVisibleTiles` drives which residents draw; no black-screen during
transitions or respawn.

#### Files to touch

##### src/user/gameplay/render/tile_streamer.{hpp,cpp}
- What changes: implement the `UpdateCamera` stub body, but **preserve the
  existing eviction/over-capacity branch** (it runs independently of the
  visibility mask).
- Function(s):
  ```cpp
  void UpdateCamera(const Vec3& camera_pos, const Mat4& inv_view_proj, float ground_y) override;
  ```
  Resolves the visible resident set via `ResolveVisibleTiles(spec_, inv_view_proj, ground_y, cell, out, kMaxRing)`, maps each visible room to its resident index via `set_.IndexOf(...)`, and stores a per-frame `bool visible_[kMaxRing]` (always mark index 0 = center visible).
- Data shapes: `visible_[]` mask; `spec_` stored from last `SetCenter`.
- Integration points: called by `OpenWorldRenderer::UpdateCamera` (new, below); read by `DrawHighPriority`.
- Error paths: empty visible set (all culled) → fall back to drawing the center only; `ResolveVisibleTiles` returns 0 → keep center.
- Testability: the host test exercises the **pure** `ResolveVisibleTiles` +
  `IndexOf` mapping (Pattern A) and asserts the center-always-visible + empty-set
  fallback rules; the real `TileStreamer::UpdateCamera` wiring is covered by the
  device visual walk (it is device-only).

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: new `UpdateCamera(const Vec3&, const Mat4& inv_view_proj, float ground_y)` forwarding to `tile_streamer_->UpdateCamera`.
- Integration points: called from `GameplayScene::Render` before `Render(cams)`.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: compute `inv_view_proj` from the **world-space** near camera and
  pass it. `ResolveVisibleTiles` projects NDC corners to **world** XZ tile
  indices, so the matrix MUST be the world-space view/proj (pos/target/fov,
  planes 20..800) — NOT the camera-at-origin view used for drawing (that would
  cull in a camera-relative frame and drop the wrong cells).
- Function(s): `GameplayScene::Render()` — build the world-space near
  view-projection (the same transform the viewport uses, but with the real
  camera pos/target), invert with `t3d_mat4_inverse` (device), and call
  `impl_->open_world_.UpdateCamera(camera.position, inv_view_proj, 0.0f)`.
- Integration points: near the existing `BuildPassCameras` call.
- Error paths: if inverse fails (singular), skip culling this frame (draw all) — never black-screen.

##### src/user/gameplay/render/tile_streamer.cpp (`DrawHighPriority`)
- What changes: draw only `visible_[i]` residents (plus always the center).
- Error paths: same as above.

#### Edge cases
- Player at a map edge: fewer residents; visibility still resolves.
- Respawn/transition: `SetCenter` rebuilds the ring and clears `visible_`;
  next `UpdateCamera` re-resolves. Center always drawn → no black frame.
- Host tests construct a synthetic `Mat4` inverse (already the pattern in
  `tile_streamer_smoke.cpp`).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/near_visibility_contract.cpp` (Pattern A/C):
  a synthetic frustum that only covers the center cell yields `visible_` = center only; a frustum covering a neighbor marks it.
- Done: device `near_batches` counter ≈ visible cells × runs (1-4 cells), not
  9 cells; no popping at the ring edge during a slow turn.

### Inc 5 — Memory: compact batches + frame arena (M)
**Depends on:** 2, 3, 4
**Unblocks:** 6
**Done criteria:** memory report (`[memory] used=…`) drops by roughly the freed
embedded batch arrays (~700 KB); render stack frames shrink; `FrameArena`
accounts nonzero `Used()` during a frame.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: heap-allocate `batches_` sized to `face_count` (clamped to `kMaxBatches`) in `Load()`; `Free()` releases it. `Load()` must `Free()` any existing `batches_` **before** reallocating (streaming re-loads / SetCenter repeatedly calls Load) or it leaks.
- Function(s): `Load()` — `Free()` old, then `batches_ = malloc(sizeof(Batch)*min(face_count,kMaxBatches))`; `Free()` — `free(batches_); batches_=nullptr;`.
- Data shapes: `Batch* batches_` replaces `Batch batches_[kMaxBatches]`.
- Integration points: `Draw()`, `DiscardedFaces()` unchanged (guard `batches_ != nullptr`).
- Error paths: malloc failure → `Load` returns false (existing pattern).
- Arena scope: only the per-frame **visible-tile snapshot** (Inc 4's `visible_`
  derived list) lives in the arena; the resident ring + renderers stay members.

##### src/user/gameplay/render/textured_room_renderer.{hpp,cpp}
- What changes: same heap-allocated batch array as above.
- Error paths: same.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: (a) allocate the render list from the frame arena instead of a stack `order[64]`; (b) `Render()` uses `open_world Arena` — thread an arena pointer (or use `DistantWorldRenderer::SetArena(FrameArena*)`).
- Function(s): `void SetArena(n64::FrameArena* arena);` — used for the per-frame `DistantRenderItem` list.
- Error paths: arena null/exhausted → fall back to a small stack buffer (budget 64 KB is ample for 64 items).

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: allocate the per-frame **visible-tile snapshot** (the
  `ResolveVisibleTiles` output list / `visible_`-derived draw order) from the
  arena (threaded from `OpenWorldRenderer`). The resident ring `set_` + its
  renderers stay persistent member state — never the arena (it resets every
  frame; putting the ring there would thrash streaming). The `SetCenter`
  transient `ring[kMaxRing]` can stay on the stack (it is not per-frame hot).
- Error paths: same fallback (stack) if arena exhausted; assert the snapshot
  budget (64 KB is ample for ≤ 64 items) so a mis-accounted frame is caught.

##### src/user/gameplay/render/open_world_renderer.{hpp,cpp}
- What changes: `BeginFrame()` already resets `arena_`; ensure it forwards the arena to `distant_`/`tile_streamer_` (set once in ctor). Add `MemorySnapshot::CaptureWithArena` wiring if not already present (Inc 7).
- Error paths: none.

#### Edge cases
- `Free()` must null the pointer to avoid double-free in the destructor path
  (both renderers' destructors call `Free()`).
- Distant cells: 45 × compact batches (~65 faces each) ≈ 45 × 1 KB instead of
  45 × 16 KB.
- Arena exhaustion: assert (budget violation) per the Inc-7 convention, but
  with a stack fallback so a mis-accounted frame never crashes.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/renderer_memory_contract.cpp` (Pattern A):
  after `Load` of a synthetic 3-face level, allocated batch capacity == 3
  (not 1024); after `Free`, pointer nulled.
- Done: device `[memory] used=` drops ~700 KB after boot; profiler memory
  report shows `arena_` used > 0.

### Inc 6 — 30 fps tuning pass (S)
**Depends on:** 4, 5
**Unblocks:** —
**Status:** done (22/22 host tests; ROM builds clean, no warnings; `kPhaseStreaming`
wired around `SetCenter`, `docs/perf_budget.md` written with the phase budget +
tuning knobs). DEVICE TUNING REMAINS: per `docs/perf_budget.md`, confirm the
budget on device (Ares → Mupen64Plus/HW) and tune `kCullMargin` / `kMaxRing` /
the optional near-pass material sort with real profiler numbers before closing
the 30 fps target. The conditional Inc 6 changes (distant decimation, near
material sort) were intentionally NOT applied speculatively — they depend on
device phase numbers and would risk a visual regression otherwise.
**Done criteria:** device profiler average ≤ 33.3 ms with distant +
high-priority within budget; 30 fps held through a full Forsaken City walk.

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: tune the cull cone `margin` (Inc 2) and, if the distant pass
  still dominates, raise `kLodScale`-style decimation or reduce the drawn ring
  (e.g., skip cells beyond distance² threshold).
- Error paths: keep the near ring + fog covering the gap.

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes (only if `texture_uploads` still dominates): global near-pass
  material sort (see CONSIDER) OR a per-cell material-run cache.
- Error paths: Z-on near pass is order-insensitive for opaque faces, so a
  global sort is safe here (unlike the distant pass).

##### src/user/gameplay/render/tile_streamer.{hpp,cpp}
- What changes: tune ring size / `kMaxRing` if memory allows more or fewer
  residents; verify the LRU eviction under the new visible-mask logic.
- Error paths: center never evicted (existing invariant).

##### docs (this repo)
- What changes: record the final per-phase budget in `docs/` (e.g., a short
  `docs/perf_budget.md`): distant ≤ 12 ms, high_priority ≤ 8 ms, update ≤ 6 ms,
  present/rest ≤ 7 ms → ≤ 33.3 ms.

#### Edge cases
- Frame spikes at chunk transitions (SetCenter loads a cell mid-frame): keep
  transitions on a load-once budget; the tile streamer already loads outside
  the draw (SetCenter is called from Update/transition, not per-frame draw).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests: no new host tests (tuning only); re-run all existing.
- Done: full walk of Forsaken City at 30 fps with no hard hitches; profiler
  shows the phase budget above.

## Cross-cutting verification

- **Every increment** re-runs `./tests/run_host_tests.sh` (17 tests, must stay
  green) and `./compile-rom.sh` (clean build, no warnings).
- **Ares visual smoke after each rendering-changing increment**: Inc 2 — distant
  horizon is not double-drawn (no 4× darkening) and does not pop on a 360° turn;
  Inc 3 — near cells have no holes/garbage triangles; Inc 4 — no black-screen on
  turn/respawn and no popping at the ring edge during a slow turn. Host contract
  tests cannot catch z-fighting/holes/wrong triangulation, so each of these gets
  a boot-and-look device check, not just Inc 6.
- **Device walk** (Ares, after Inc 6): start at `cell_00_00`, walk the whole
  map; confirm no Z-fighting, no popping at LOD/frustum boundaries, no holes,
  no visible tile loading, no precision artifacts, smooth 30 fps movement.
- **Per-phase budget** (recorded after Inc 1 as baseline, re-measured each
  increment): distant, high_priority, texture_upload ms + draw counters.
- **Memory**: `[memory] total/used/free` before and after Inc 5 to quantify
  the freed embedded batch arrays.
- **Hardware cross-check**: Ares timing is not real-hardware timing. Before
  closing Inc 6, cross-check the frame rate on Mupen64Plus (or real HW); the
  Ares profiler budget is a proxy, not proof.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: any new `rom:/`
  paths added (none expected; if a distant/visibility asset is added, verify
  `filesystem/` layout).
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 4
  must not change boot/respawn ordering (center always drawn).
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: Inc 4
  visibility must not interfere with respawn camera reset.
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 3 must
  not alter face winding/order (adjacent-only coalescing preserves it).

## Open questions (CONSIDER from review)

- CONSIDER: after Inc 3, if `texture_uploads` still dominates, add a global
  per-cell material sort in the *near* pass only (Z-on → order-safe) for one
  upload per material rather than per run.
- CONSIDER: a per-frame `kPhaseStreaming` report of `SetCenter` load ms to
  catch transition hitches (Inc 6 budget).
- CONSIDER: whether `kMaxRing` (9) can grow once Inc 5 frees ~700 KB, trading
  RAM for fewer stream transitions.

## Out of scope

- Gameplay rule changes (movement, camera feel, physics).
- The two-pass architecture (`arch.md` §21) — passes stay.
- New art/assets; the player model (`player.t3dm` conversion remains broken).
- Frame-arena conversion of gameplay-side (non-render) allocations.
- RDP micro-optimizations beyond material-run batching (e.g., TMEM-tiled
  mega-textures) — future work.

## Implementation status (implement-plan, 2026-08-11)

All 6 increments implemented and verified:

- **Inc 1** — `debug_flags.hpp` (kVerboseFrameLogging=false), per-frame
  debugf + 60-frame telemetry gated in `gameplay_scene.cpp`, `RenderCounters`
  (distant_cells/near_batches/texture_uploads/vert_loads/syncs) wired through
  `OpenWorldRenderer` → `DistantWorldRenderer`/`TileStreamer` → room renderers,
  `kPhaseTextureUpload` emitted around the textured near draw.
- **Inc 2** — `CellInDistantFrustum` + `BuildDistantRenderListCulled` in
  `lod_math.hpp`/`distant_world_renderer.hpp`; `Render()` draws `meshes[0]`
  only (kills the 4× redundancy) and culls by the distant pass clip range.
- **Inc 3** — `batch_coalesce.hpp` (fan-preserving, span-capped-at-70 runs) +
  `CoalesceBatches`; both room renderers build runs at Load and Draw one
  RDP-state + one vert_load + one tri_sync per run, each face fanned from its
  own origin (geometry-equivalence host test proves identical triangles).
- **Inc 4** — `Mat4Invert` in `tile_visibility.hpp`; `TileStreamer::UpdateCamera`
  resolves the visible resident set (center always visible), `DrawHighPriority`
  culls; `GameplayScene::Render` inverts the WORLD-SPACE near view-proj and
  calls `OpenWorldRenderer::UpdateCamera`.
- **Inc 5** — both renderers heap-allocate `batches_` sized to face count
  (Free-before-realloc, Free-nulls); distant render list + visible snapshot
  allocated from the frame arena (`SetArena` threaded from `OpenWorldRenderer`).
- **Inc 6** — `kPhaseStreaming` wired around `SetCenter`; `docs/perf_budget.md`
  written (phase budget, measurement, tuning knobs). Conditional tunings
  (distant decimation, near material sort, kMaxRing growth) deliberately NOT
  applied speculatively — they need device phase numbers (see perf_budget.md).

**Verification:** 22/22 host tests pass (17 pre-existing + 5 new contract
tests); ROM builds clean (no warnings) after a from-scratch clean rebuild.
Baseline before fixup: 17/17. New host tests: `debug_flags_contract`,
`distant_cull_contract`, `batch_coalesce_contract`, `near_visibility_contract`,
`renderer_memory_contract`.

**Remaining manual device verification** (per cross-cutting section): Ares
boot-and-look after the render changes (distant not 4×-darkened/popping, near
no holes/garbage, no black-screen on turn/respawn), the full Forsaken City
walk at 30 fps, and the `[memory] used=` ~700 KB drop. Ares is GUI-only; the
user runs the Ares CLI launch from AGENTS.md.
