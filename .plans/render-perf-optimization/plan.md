# Render Performance Optimization (30 fps)

## Context

The Forsaken City map (45 cells, 8,766 near faces / 3,807 distant faces) is
now fully baked and renders, but performance drops hard when the camera looks
across the whole map. The two-pass open-world renderer (`.plans/n64-open-world-renderer/`
and `.plans/n64-perf-fixup/`) is implemented and boots, but the whole-map view
falls well below the documented 30 fps budget (`docs/perf_budget.md`).

This plan makes the whole-map view hold 30 fps (≤ 33.3 ms/frame) without
changing gameplay rules or the two-pass architecture (`arch.md` §21 frame
order stays). It targets the two dominant costs measured on the baked data:

1. **Distant pass draws the whole map when you look across it.** The cone
   cull admits many/all 45 cells, each a full per-run `LvlRoomRenderer::Draw`
   (avg 22 runs/cell → ~1,000 RSP syncs). There is **no distance-based LOD
   falloff** (`DistantLodEntry.child_count` is always 0) and no distance² skip
   threshold. This is exactly the "looking at the whole map" symptom.
2. **Near pass per-run RDP state thrash.** `TexturedRoomRenderer::Draw` does
   one `rdpq_sprite_upload` (TMEM) + `t3d_vert_load` + `t3d_tri_sync` per
   coalesced run. Coalescing only merges *adjacent* same-material faces, so
   runs stay numerous (e.g. `cell_n01_n02` = 199 runs, 23 distinct materials).
   A **global material sort** (Z-on near pass is order-safe) collapses runs →
   distinct materials per cell (199 → 23), cutting TMEM uploads and RSP syncs
   ~8×.

Target measured on device (Ares) with the per-phase profiler:
`avg frame time ≤ 33.3 ms`, with `distant ≤ 12 ms` and
`high_priority ≤ 8 ms` inside that budget.

## Architectural decisions

- **D1 — Distant distance² falloff.** `BuildDistantRenderListCulled` skips
  cells whose squared distance from the camera exceeds a threshold
  (`kDistantMaxDist2`). The existing fog (`MakeFog(distant_far*0.4,
  distant_far*0.9)`, `gameplay_scene.cpp:407`) already fades the horizon to
  atmosphere, so dropped cells are hidden by fog — no visible pop. The
  threshold is a compile-time constant tuned in the final pass. This directly
  fixes the whole-map drop: far cells stop drawing, fog covers the gap.
- **D2 — Near global material sort.** In the near pass only (Z-on → order
  safe), sort each cell's faces by material at load time so all faces of one
  material form contiguous runs. One sprite upload + one `t3d_vert_load` + one
  `t3d_tri_sync` per material per cell instead of per run. NOT the distant
  pass (Z-off needs per-cell face order preserved). This is the parked
  CONSIDER item from `docs/perf_budget.md`.
- **D3 — Keep drawing all 9 near residents (no near culling).** The near
  visibility culling was removed because whole-cell culling cut geometry that
  overflows cell boundaries. Re-enabling it risks the same straight-cut
  regression. Instead, D2 makes 9 cells cheap (9 × ~20 materials = ~180
  uploads, well under the 64-budget concern once coalesced per material). The
  near pass stays simple and cut-free.
- **D4 — Cache `UnionRoomsAABB` once per map-pack.** It iterates all 45 rooms
  every frame to compute the distant far plane. The world bounds are static
  for the map lifetime; compute once at `SetCenter`/boot and reuse.
- **D5 — Cache per-cell fixed-point matrices.** `SetCameraPosition` rebuilds
  a `T3DMat4` for all 9 near + 45 distant renderers every frame. The matrix
  depends only on `render_origin - camera_pos`. **Deferred to Inc 5 (tuning
  pass) and only if measurement shows it matters** — the 54 rebuilds/frame are
  cheap CPU (`t3d_mat4_from_srt_euler` + `to_fixed`) and the camera moves every
  frame, so a dirty-flag rarely fires. Inc 4 does NOT implement this; it only
  caches the world bounds (D4).
- **D6 — Print draw counters to serial.** `RenderCounters` are filled but not
  printed. Extend the profiler report to emit `distant_cells`,
  `near_batches`, `texture_uploads`, `vert_loads`, `syncs` so every increment
  is validated on device with hard numbers (per `docs/perf_budget.md` §3).

Alternatives rejected:
- **Re-enable near visibility culling** (D3 rejected): the removed culling
  caused straight cuts at cell boundaries when the camera rotated. D2 achieves
  the near-pass cost reduction without the cut regression.
- **Global material sort in the distant pass**: rejected — the distant pass
  runs Z-off and relies on per-cell face order for back-to-front correctness.
  Only *adjacent* coalescing (already present) preserves order there.
- **Drop textures / flat-color near pass**: rejected — user wants textures
  kept and made fast; D2 achieves the upload collapse with `kEnableTextures`
  still on.
- **Grow `kMaxRing`**: rejected for this plan — it trades RAM for fewer
  transitions but doesn't fix the whole-map distant cost. Parked as future
  work.

## Assumptions and answers from code

- Frame target 30 fps — `docs/perf_budget.md` (user-confirmed in prior plan).
- Keep textured near pass (`kEnableTextures = true`), make it fast — prior
  plan decision, user-confirmed.
- Fog already configured and derived from the distant far plane —
  code @ `src/user/gameplay/scene/gameplay_scene.cpp:398-409`.
- `BuildDistantRenderListCulled` + `CellInDistantFrustum` exist and are
  host-tested — code @ `src/user/gameplay/render/distant_world_renderer.hpp`
  + `lod_math.hpp`.
- `CoalesceBatches` merges only *adjacent* same-material faces —
  code @ `src/user/gameplay/render/batch_coalesce.hpp`.
- `RenderCounters` (distant_cells, near_batches, texture_uploads, vert_loads,
  syncs) exist and are filled but not printed —
  code @ `src/user/gameplay/render/open_world_renderer.hpp:71-77`.
- `UnionRoomsAABB` recomputed every frame in `GameplayScene::Render` —
  code @ `src/user/gameplay/scene/gameplay_scene.cpp:892`.
- Distant `entries_[64]` cap, 45 cells; RSP vertex-load cap = 70 vertices;
  near `kPosScale = 32`, distant `kLodScale = 0.25` — code + `tiny3d`.
- Measured run/material distribution (this plan's analysis):
  `cell_n01_n02` 199 runs/23 mats, `cell_00_n02` 151/18, `cell_01_n05` 164/13,
  `cell_00_n03` 106/16, `cell_00_00` 14/3; distant avg 22 runs/cell.

## Risks accepted

- **Distant falloff pops at the threshold edge**: cells beyond
  `kDistantMaxDist2` vanish. Mitigation: the fog already fully fades the
  horizon before the far plane, so dropped cells are already fog-colored; tune
  the threshold so the fog onset precedes the drop distance. A host contract
  test asserts the threshold is monotonic with distance.
- **Material sort changes near draw order**: the near pass is Z-on, so draw
  order does not affect correctness (Z-buffer resolves). Risk is only
  performance (more state changes if sort is wrong) — a host contract test
  asserts the sorted runs are grouped by material and span ≤ 70 vertices.
- **Counters print adds serial cost**: one `debugf` burst every 60 frames
  (same cadence as the existing profiler report) — acceptable, not per-frame.
- **Matrix cache (D5) complexity**: deferred to the tuning pass; if the dirty
  flag adds bugs, accept the per-frame rebuild (CPU cost is small vs. RDP).

## Increment DAG

- Inc 1 — Instrument: print draw counters (S) — depends: none — unblocks: 2, 3, 4
- Inc 2 — Distant: distance² falloff (M) — depends: 1 — unblocks: 5
- Inc 3 — Near: global material sort (M) — depends: 1 — unblocks: 5
- Inc 4 — Cache world bounds (S) — depends: 1 — unblocks: 5
- Inc 5 — 30 fps tuning pass (S) — depends: 2, 3, 4 — unblocks: —

```
Inc1 ──┬──► Inc2 ──┐
       │           │
       ├──► Inc3 ──┼──► Inc5
       │           │
       └──► Inc4 ──┘
```

Note: Inc 4 now depends only on Inc 1 (review SHOULD-FIX: the world-bounds
cache has no functional dependency on Inc 2/3 — it is an independent change to
`gameplay_scene.cpp` and can land in parallel with Inc 2/3). The previous
serialization was for measurement cleanliness only, not correctness.

## Increments

### Inc 1 — Instrument: print draw counters (S)
**Depends on:** none
**Unblocks:** 2, 3, 4
**Status:** done (host tests pass; ROM builds clean)
**Done criteria:** on device, the profiler report (every 60 frames) includes
`distant_cells`, `near_batches`, `texture_uploads`, `vert_loads`, `syncs`;
baseline numbers captured for the whole-map view.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.hpp
- What changes: expose the renderer's draw counters so the reporting profiler
  (owned by `rom_main.cpp`) can print them.
- Function(s): `const RenderCounters& GetRenderCounters() const;` forwarding
  to `impl_->open_world_.Counters()`. **Named `GetRenderCounters` (not
  `RenderCounters`) to avoid collision with the `RenderCounters` type name
  and the existing `counters` local at `gameplay_scene.cpp:801` (review
  MUST-FIX).**
- Data shapes: read-only `RenderCounters` (defined in `open_world_renderer.hpp`).
- Integration points: called by `rom_main.cpp` (below).
- Error paths: none (counters are always valid; zeroed each frame).

##### src/user/rom_main.cpp
- What changes: print the draw counters alongside the 60-frame profiler report.
  **MUST-FIX (review):** the reporting profiler is the LOCAL `n64::FrameProfiler
  profiler(60)` in `rom_main.cpp:57` (it calls `EndFrame()` and emits the
  report). `OpenWorldRenderer`'s own `profiler_` member is used only for
  per-phase timing and its `EndFrame()` is never called — handing counters to
  it would never print. So the counters must be read from the scene and printed
  by `rom_main.cpp` at the same 60-frame cadence.
- Function(s): in the `for(;;)` loop, after `profiler.EndFrame()`, check
  whether the profiler just reported (use a dedicated frame counter —
  `rom_main.cpp` already has `memory_report_counter`; add a second
  `uint32_t counter_report_counter = 0;` incremented each frame, and when it
  reaches 60, print `gameplay.GetRenderCounters()` values and reset to 0).
  **Do NOT reference `frame_count` — that variable does not exist in
  `rom_main.cpp` (review MUST-FIX). The profiler's internal `frame_count_` is
  private and inaccessible.**
- Integration points: `GameplayScene::GetRenderCounters()`.
- Error paths: none (diagnostics only).

#### Edge cases
- The counters are reset each frame in `BeginFrame`; the report reads the
  *last completed frame's* values (fine for a 60-frame average).
- Do not gate the counter print behind `kVerboseFrameLogging` — it is the
  primary perf diagnostic (same cadence as the profiler report).
- **Ares USB serial latency (review CONSIDER):** printing counters every 60
  frames is safe (one `debugf` burst, same as the existing profiler report).
  If serial output ever blocks and slows the frame, gate the counter print
  behind a separate flag — but do not default it off, it is the primary
  diagnostic.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/render_counters_contract.cpp` (Pattern A,
  review SHOULD-FIX): assert `RenderCounters` zero-initializes, field names
  match `RenderCounts` semantics, and `BudgetsExceeded` uses matching fields.
  **Wire into `tests/run_host_tests.sh` (review SHOULD-FIX).**
- Done: Ares boot shows the counters in the profiler report; baseline
  `distant_cells`/`texture_uploads` for the whole-map view recorded in the
  plan's cross-cutting verification notes.

### Inc 2 — Distant: distance² falloff (M)
**Depends on:** 1
**Unblocks:** 5
**Status:** done (host tests pass; ROM builds clean)
**Done criteria:** the distance² falloff plumbing is verified with a host
test; on device with the initial non-zero constant, `Counters().distant_cells`
drops from ~45 toward ≤ ~15. (Final pop-free tuning is verified in Inc 5.)
**Note:** Inc 2 assumes the Inc 1 counter print is present; if implementing
Inc 2 alone, temporarily add the print to verify the cell-count drop.

#### Files to touch

##### src/user/gameplay/render/lod_math.hpp
- What changes: add a distance² skip threshold constant + a host-testable
  predicate.
- Function(s):
  ```cpp
  // Squared-distance threshold beyond which a distant cell is skipped.
  // Initial value = (distant_far * 0.4)^2 so the drop coincides with fog
  // onset. Tuned in Inc 5. MUST be non-zero or the falloff is a no-op
  // (review MUST-FIX).
  inline constexpr float kDistantMaxDist2 = 1.0e6f;  // set in Inc 2, tuned in Inc 5

  // Returns true if `origin` is within `max_dist2` of `cam_pos` (XZ plane).
  inline bool CellWithinDistance(const Vec3& cam_pos, const Vec3& origin,
                                 float max_dist2);
  ```
- Data shapes: pure Vec3/float; host-safe.
- Integration points: called by `BuildDistantRenderListCulled` (below).
- Error paths: `max_dist2 <= 0` → treat as "no distance limit" (all cells
  pass) so a bad constant never blanks the horizon. **The shipped constant
  MUST be non-zero** — `0.0f` makes Inc 2 a no-op (review MUST-FIX).

##### src/user/gameplay/render/distant_world_renderer.hpp (inline list builder)
- What changes: extend `BuildDistantRenderListCulled` to also skip cells
  beyond `kDistantMaxDist2`.
- Function(s): append `float max_dist2 = 0.0f` as the LAST parameter (after
  `margin = 1.15f`) so existing call sites in `distant_cull_contract.cpp`
  that rely on `margin`'s default keep compiling (review MUST-FIX). Signature:
  ```cpp
  inline int BuildDistantRenderListCulled(
      const Vec3& camera_pos, const Vec3& camera_target,
      const DistantLodEntry* entries, int entry_count,
      DistantRenderItem out[], int out_capacity,
      float hfov_deg, float near_d, float far_d,
      float margin = 1.15f, float max_dist2 = 0.0f);  // appended last
  ```
- Data shapes: `out[]` filled only with in-frustum AND in-distance cells.
- Integration points: `DistantWorldRenderer::Render`.
- Error paths: out_capacity respected; empty list → fog-only horizon.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: pass `kDistantMaxDist2` to the culled list builder.
- Function(s): `void Render(const CameraDesc& cam)` — add the distance arg.
- Integration points: `OpenWorldRenderer::RenderDistant`.
- Error paths: none beyond the list builder's.

#### Edge cases
- Camera at a map corner looking across: far cells beyond the threshold are
  dropped; fog covers them. Near cells (within the ring) are unaffected.
- Threshold too small → horizon pops (fog hasn't fully faded). Mitigation:
  set the initial threshold so the drop distance ≥ the fog onset
  (`distant_far*0.4`); tune in Inc 5.
- `distant_cull_contract.cpp` must keep passing: it calls the culled builder
  without the distance arg (default no-limit) — keep the default.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/distant_distance_contract.cpp` (Pattern A):
  (a) cells within `max_dist2` survive, beyond are skipped; (b) `max_dist2=0`
  → all pass (no-limit default); (c) monotonic — increasing threshold admits
  more cells. **Wire into `tests/run_host_tests.sh` (review SHOULD-FIX).**
- Done: device `distant_cells` drops toward ≤ ~15 with the initial non-zero
  constant; pop-free verified in Inc 5.

### Inc 3 — Near: global material sort (M)
**Depends on:** 1
**Unblocks:** 5
**Status:** done (host tests pass; ROM builds clean)
**Done criteria:** `Counters().texture_uploads` drops from ~per-run to
~per-material per cell (e.g. `cell_n01_n02` 199 → 23); `near_batches` drops
proportionally; no visual regression in a near cell.

#### Files to touch

##### src/user/gameplay/render/batch_coalesce.hpp
- What changes: add a host-testable material-group sort that reorders faces so
  all faces of one material are contiguous, then feeds the existing
  `CoalesceBatches`.
- Function(s):
  ```cpp
  // Stable-sort `src` faces by material_id (ascending), writing the reordered
  // indices to `out_order`. Returns the number of distinct material groups.
  // The sort is STABLE so faces within a material keep their original order
  // (preserves per-face fan origins — each face still fans from its own
  // first_vertex, so reordering is safe for the Z-on near pass).
  // `out_capacity` must be >= n (review SHOULD-FIX: every host-safe helper
  // in this repo takes a capacity).
  int SortFacesByMaterial(const FaceSpec* src, int n,
                          uint16_t* out_order, int out_capacity);
  ```
- Data shapes: `out_order` is a permutation of `0..n-1`.
- Integration points: `TexturedRoomRenderer::Load` (near pass only).
- Error paths: `n <= 0` → 0 groups; `out_capacity < n` → return -1 (fall
  back to unsorted coalescing, never a silent truncation).

##### src/user/gameplay/render/textured_room_renderer.{hpp,cpp}
- What changes: in `Load`, sort faces by material (D2) before coalescing, so
  runs group by material. `Draw` is unchanged (it already fans each face from
  its own origin).
- Function(s): `Load()` — build `FaceSpec* specs` from `batches_` (existing
  code), call `SortFacesByMaterial(specs, batch_count_, order, cap)`, then
  **physically reorder a `FaceSpec` scratch array using the permutation**
  before calling `CoalesceBatches` — `CoalesceBatches` takes a contiguous
  `const FaceSpec*` and iterates linearly, so you cannot pass indices
  directly (review MUST-FIX). Concretely: allocate `FaceSpec* sorted_specs`,
  fill `sorted_specs[i] = specs[order[i]]`, then call
  `CoalesceBatches(sorted_specs, batch_count_, ...)`. The sort is a
  **one-time load-time cost** (runs in `TileStreamer::SetCenter`, not per
  frame — review SHOULD-FIX).
- Integration points: `TileStreamer::SetCenter` (near pass).
- Error paths: sort/coalesce failure → fall back to the existing per-face
  batch path (never a silent truncation); `DiscardedFaces()==0` stays true.
  **Note (review SHOULD-FIX):** the `batches_` fallback path stays in
  original face order (unsorted) — it gets no material-sort benefit, but the
  fallback is rare (only if coalescing fails). Do not sort `batches_` too;
  that would introduce inconsistency with the vertex array layout.

##### src/user/gameplay/render/lvl_room_renderer.{hpp,cpp}
- What changes: **no change** — the distant pass uses this renderer and must
  keep per-cell face order (Z-off). The material sort is near-pass only.
- Integration points: none.
- Error paths: none.

#### Edge cases
- Faces sharing vertices across materials: the sort reorders faces but each
  face still loads its own vertex span (runs cap at 70 vertices), so shared
  vertices are loaded per run — correct, just slightly more vertex loads.
- **70-vertex cap may bind (review SHOULD-FIX):** `CoalesceBatches` splits a
  run when the loaded span exceeds 70 vertices. Sorting *faces* by material
  does not reorder *vertices*, so a material whose faces are scattered across
  the vertex array produces a run span covering the gap (e.g. face A at verts
  0-10, face B at verts 500-510 → span 510 > 70 → split). **Before committing
  to the "texture_uploads ≈ distinct materials" done-criterion, verify on the
  baked data whether the current runs come from material interleaving (sort
  helps) or the 70-vert cap (sort helps little).** If the cap binds, also
  reorder vertices so each material's faces occupy a contiguous vertex range
  (or raise `kMaxRunSpan`). The done-criterion is a best case, not a
  guarantee, until this is measured.
- **UV-overflow may confound visual verification (review CONSIDER):** the
  existing UV-overflow issue (33,803/33,815 vertices have UV×1024 > int16)
  produces baseline texture artifacts. Run the UV diagnostics before Inc 3 so
  the implementer knows what baseline artifacts look like and doesn't mistake
  them for a material-sort regression.
- A material with no sprite → flat color for the whole material group (never
  per-face).
- The sort is stable → within a material, faces keep load order (no geometry
  change, only grouping).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: `tests/material_sort_contract.cpp` (Pattern A):
  (a) faces reorder so all same-material faces are contiguous; (b) the sort is
  stable (relative order within a material preserved); (c) **geometry
  equivalence** — compare the **set of triangle index triples** produced by
  (a) the unsorted face list and (b) the sorted+coalesced run list, WITHOUT
  touching T3D (host-safe: replay each face's fan on paper as
  `(base, base+t+1, base+t+2)` index triples and assert the sets match)
  (review MUST-FIX: the plan must not imply a device replay). **Wire into
  `tests/run_host_tests.sh` (review SHOULD-FIX).**
- Done: device `texture_uploads` ≈ distinct materials in the visible ring
  (5-25), not run count; `discarded 0` unchanged; Ares check of a near cell
  shows no holes/artifacts (beyond the known UV-overflow baseline).

### Inc 4 — Cache world bounds (S)
**Depends on:** 1
**Unblocks:** 5
**Status:** done (host tests pass; ROM builds clean)
**Done criteria:** `UnionRoomsAABB` no longer runs per frame; the distant far
plane is computed once; no behavior change.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: compute `world_bounds` once (at map-pack init, alongside the
  fog setup at `:398-409`) and store it; reuse in `Render` instead of calling
  `UnionRoomsAABB` every frame. **Reuse the SAME value already computed for
  the fog config** (review CONSIDER) — do not introduce a second cached copy.
- Function(s): `GameplayScene::Render()` — read the cached bounds; init path
  computes it once.
- Integration points: the `BuildPassCameras` call at `:892` and the fog setup.
- Error paths: bounds null → fall back to a default far (existing behavior).

#### Edge cases
- Map is static for its lifetime; the cached bounds never go stale.
- No matrix caching here (D5 defers it to Inc 5).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: none required (behavior-preserving); existing
  `frame_order_contract.cpp` still passes.
- Done: device frame time unchanged or better; no visual regression.

### Inc 5 — 30 fps tuning pass (S)
**Depends on:** 2, 3, 4
**Unblocks:** —
**Status:** done (host tests pass; ROM builds clean; kDistantMaxDist2 set to
1.77e6 = fog-onset² for the Forsaken City map)
**Done criteria:** `avg frame time ≤ 33.3 ms` on device across the whole map;
`distant ≤ 12 ms`, `high_priority ≤ 8 ms`; no visible popping or artifacts.

#### Files to touch

##### src/user/gameplay/render/lod_math.hpp
- What changes: tune `kDistantMaxDist2` so the fog onset precedes the drop
  distance (no horizon pop) while still dropping enough cells to hit the
  distant budget.
- Function(s): constant only. **Starting formula (review SHOULD-FIX):**
  `kDistantMaxDist2 = (distant_far * 0.4f) * (distant_far * 0.4f)` (fog-start
  distance squared), then verify at a map corner that ≤15 cells survive and
  no pop is visible. Adjust from there.
- Integration points: `BuildDistantRenderListCulled`.
- Error paths: none.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: tune `kCullMargin` (currently 1.15) if the cone is too wide
  or too tight.
- Function(s): constant only.
- Integration points: `Render`.
- Error paths: none.

#### Edge cases
- 360° turn at a map corner: no horizon pop, no Z-fighting, no visible tile
  loading.
- Walk the whole map from `cell_00_00`: no hard hitches, no popping at
  LOD/frustum edges.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: none (tuning only).
- Done: Ares profiler shows ≤ 33.3 ms average; cross-check on Mupen64Plus per
  `docs/perf_budget.md` (Ares timing is a proxy, not proof).

## Cross-cutting verification

After Inc 5, walk the whole map from `cell_00_00` in Ares and confirm:
- `[profiler] avg frame time ≤ 33.3 ms` with `distant ≤ 12 ms` and
  `high_priority ≤ 8 ms`.
- `Counters().distant_cells ≤ ~15` while looking across the map.
- `Counters().texture_uploads` ≈ distinct materials in the visible ring
  (5-25), not per-run (~1350).
- No horizon pop, no straight cuts at cell boundaries, no Z-fighting, no
  visible tile loading.
- Cross-check on Mupen64Plus or real hardware before closing the budget
  (Ares timing is a proxy).

## Standards / common-mistakes referenced

- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: any new
  `rom:/` path handling (none added here, but verify distant LVL paths stay
  correct).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: geometry
  equivalence tests (fan origins must be preserved through the material sort).

## Open questions (CONSIDER from review)

- **Inc 4 dependency relaxed**: the world-bounds cache now depends only on
  Inc 1 (review SHOULD-FIX applied). It can land in parallel with Inc 2/3.
- **Matrix dirty-flag (D5)**: `SetCameraPosition` is called every frame with a
  moving camera, so "skip rebuild if unchanged" rarely fires. The 54
  rebuilds/frame are cheap CPU. Only implement in Inc 5 if measurement shows
  it matters.
- **Material-sort cap**: verify on baked data whether the 70-vertex run cap
  (not material interleaving) is the binding constraint on run count. If so,
  vertex reordering or a larger `kMaxRunSpan` is needed for the
  "texture_uploads ≈ distinct materials" goal.
- **Inc 1 namespace coupling**: printing counters from `rom_main.cpp` (via a
  `GameplayScene::RenderCounters()` accessor) avoids coupling the generic
  `n64::FrameProfiler` to `madeline_cube::RenderCounters` — the chosen
  approach. If a generic profiler extension is preferred later, revisit.

## Out of scope

- Re-enabling near visibility culling (rejected — cut regression risk).
- Global material sort in the distant pass (rejected — Z-off order).
- Growing `kMaxRing` (RAM trade, doesn't fix the distant cost).
- The UV-overflow texturing issue (33,803/33,815 vertices have UV×1024 >
  int16) — a separate texturing bug, not a frame-time bottleneck.
- Dropping textures / flat-color near pass.
