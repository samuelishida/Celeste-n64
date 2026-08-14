# Distant-Pass Performance: Material Sort + Horizon Fade

## Context

The Forsaken City map (45 cells, 2400×2160 world units, diagonal ≈ 3228) holds
60 fps when the camera looks at nothing, but drops hard when the camera turns
to the whole map. The previous plan (`.plans/render-perf-optimization/`)
optimized the **near** pass (material sort in `TexturedRoomRenderer`) and added
a distance² falloff, but the whole-map view still falls below the 30 fps budget
(`docs/perf_budget.md`).

Investigation of the baked data (`filesystem/lvl/forsyken-city/*_distant.lvl`)
and the render path found the dominant remaining cost:

1. **The distant pass is NOT material-sorted.** `LvlRoomRenderer::Load`
   (`src/user/gameplay/render/lvl_room_renderer.cpp`) calls only
   `CoalesceBatches` (adjacent-only merge). The bake
   (`tools/ogworld/distant_lod.py`) emits faces in **arbitrary original
   polygon order** — not material-grouped, not back-to-front. So adjacent
   coalescing yields **~22.6 runs/cell → ~1015 RSP syncs/frame** across all 45
   cells (measured from the baked LVLs). When looking across the map the
   distance² falloff already culls to ~15-20 drawn cells, so the real baseline
   is ~15-20 × 22.6 ≈ 340-450 syncs/frame. The near
   pass (`TexturedRoomRenderer`) was material-sorted in the previous plan; the
   distant pass was deliberately excluded on a Z-off ordering concern that the
   bake data shows is moot (the intra-cell order is already arbitrary).
2. **The distance² falloff is pop-y.** The drop threshold is
   `sqrt(kDistantMaxDist2) = sqrt(1.77e6) ≈ 1330` world units, but the fog
   range is `0.4×distant_far → 0.9×distant_far = 1485 → 3342`. So cells are
   dropped at 1330 **before fog even starts** (1485) — a hard pop at the drop
   edge. The fog must complete **before** the drop threshold so far cells are
   fully fogged when dropped.

This plan makes the whole-map view hold 30 fps (≤ 33.3 ms/frame) by (a)
material-sorting the distant pass at load time (collapses the distant pass's
RSP syncs ~3.3× — measured 1015 → 303 runs across all 45 cells, ~22.6 → ~6.7
runs/cell), and (b) retuning the fog to complete within the existing drop
threshold so the distance² falloff drops far cells pop-free. The user confirmed
the horizon-fade visual change.

## Architectural decisions

- **D1 — Material-sort the distant pass at load time.** Add
  `SortFacesByMaterial` + `CoalesceBatches` to `LvlRoomRenderer::Load`,
  mirroring the proven `TexturedRoomRenderer` pattern (sort → physically
  reorder a scratch `FaceSpec` array → coalesce). Measured from the baked
  LVLs: 1015 → 303 runs across all 45 cells (~22.6 → ~6.7 runs/cell), a ~3.3×
  reduction; when looking across the map (~15-20 drawn cells) that is ~340-450
  → ~100-134 syncs/frame. **Safe for
  Z-off**: the bake emits faces in arbitrary order (not back-to-front), so
  material-sorting is no worse for intra-cell correctness; cells are still
  sorted back-to-front by distance² in `DistantWorldRenderer::Render`. The
  previous plan's "Z-off needs per-cell face order" rejection is based on a
  false premise — there is no meaningful order to preserve.
- **D2 — Retune fog to complete within the drop threshold.** The drop
  threshold is `sqrt(kDistantMaxDist2) ≈ 1330` world units. Retune the fog
  range from `0.4×distant_far → 0.9×distant_far` (1485 → 3342) to
  `0.4×drop → 0.9×drop` (532 → 1197), so fog completes **before** the drop
  threshold and dropped cells are fully fogged — no pop. This keeps
  `kDistantMaxDist2` unchanged (the cell count stays low, ~15-20 when looking
  across the map) and makes the horizon fade within the map (fog completes at
  ~1197, well inside the 3228 diagonal). The fog range is derived from
  `kDistantMaxDist2` in code (`sqrt(kDistantMaxDist2) * 0.4/0.9`), keeping the
  drop/fog coupling in one place.
- **D3 — Keep the near pass unchanged.** The near pass (`TexturedRoomRenderer`)
  is already material-sorted and draws the bounded ring (≤9 cells). No change.
- **D4 — `LvlRoomRenderer` is shared near-flat/distant.** It is used for the
  near flat pass (when `kEnableTextures` is false) and the distant pass. Adding
  the material sort affects both; the near flat pass is Z-on (order-safe), so
  no regression. In practice `kEnableTextures` is true, so the near pass uses
  `TexturedRoomRenderer` and `LvlRoomRenderer` is distant-only.

Alternatives rejected:
- **Raise `kDistantMaxDist2` to match a later fog completion**: rejected — the
  drop threshold is monotonic in world-XZ distance, so raising it draws *more*
  cells (all 45 from the map center), defeating the perf goal. The correct
  direction is to lower fog completion to within the existing drop threshold.
- **Decimate distant meshes more aggressively at bake time**: rejected — the
  current bake already drops coplanar duplicates + quantizes; a true quadric
  decimation is out of scope and does not fix the run count (faces would still
  be arbitrary-ordered). Material-sort is the direct fix for the RSP-sync
  bottleneck.
- **Material-sort at bake time (emit grouped faces)**: rejected — requires a
  re-bake and changes the artifact; the runtime sort is load-time-only, uses
  already-tested code, and needs no re-bake.

## Assumptions and answers from code

- Frame target 30 fps — `docs/perf_budget.md` (user-confirmed in prior plan).
- Distant pass budget ≤ 12 ms — `docs/perf_budget.md`.
- `LvlRoomRenderer::Load` does NOT call `SortFacesByMaterial` (only
  `CoalesceBatches`) — code @ `src/user/gameplay/render/lvl_room_renderer.cpp:200`.
- `TexturedRoomRenderer::Load` DOES sort (the pattern to mirror) —
  code @ `src/user/gameplay/render/textured_room_renderer.cpp:198-207`.
- Bake emits distant faces in arbitrary order (not material-grouped) —
  code @ `tools/ogworld/distant_lod.py:74-104` (Explore-confirmed).
- Distant pass draws `meshes[0]` per culled cell, back-to-front by distance² —
  code @ `src/user/gameplay/render/distant_world_renderer.cpp:166-169`.
- Fog configured at `gameplay_scene.cpp:412-419` from `MapFarClipDistance`
  (distant_far = 3713 for this map); `kDistantMaxDist2 = 1.77e6` (threshold
  1330) — code @ `lod_math.hpp:22-31`.
- Map diagonal ≈ 3228 (X 2400, Z 2160) — measured from cell grid.
- Distant run baseline ≈ 1015 syncs across 45 cells (~22.6/cell) — measured
  from the baked LVLs (adjacent-only coalescing). Distinct-material best case
  ≈ 303 runs (~6.7/cell) → ~3.3× reduction. When looking across the map the
  distance² falloff culls to ~15-20 drawn cells, so the drawn baseline is
  ~340-450 syncs → ~100-134 after sort.
- `SortFacesByMaterial` + `CoalesceBatches` are host-tested and stable —
  code @ `batch_coalesce.hpp`, tests @ `material_sort_contract.cpp`,
  `batch_coalesce_contract.cpp`.
- `[counters]` report prints `distant_cells`, `near_batches`, `texture_uploads`,
  `vert_loads`, `syncs` every 60 frames — code @ `rom_main.cpp:78-91`.

## Risks accepted

- **Material sort changes distant intra-cell draw order**: the distant pass is
  Z-off, but the bake order is already arbitrary (not back-to-front), so
  material-sorting is no worse. Cells remain back-to-front sorted. A host
  contract test asserts geometry equivalence (same triangle set) after the
  sort.
- **Horizon fade is a visual change**: the map edge now fades to blue-grey
  atmosphere within the map (fog completes at ~1197). User-confirmed. The fog
  color (120,150,180) and skybox (88,163,221) are close enough that the fade
  reads as atmospheric, not a hard cut.
- **Fog range in depth space is approximate**: `t3d_fog_set_range` operates in
  the distant projection's depth space, and depth is non-linear (~1/z) in a
  perspective projection. A cell at world-XZ distance 1330 is not at depth 1330
  unless it lies on the view axis. The `0.4/0.9 × drop` range is a world-space
  approximation; Inc 3 tunes by feel. Off-axis cells have depth > world-XZ
  distance, so they are *more* fogged at the drop — directionally safe.
- **`kDistantMaxDist2` is map-specific**: the constant bakes in the Forsaken
  City diagonal. Any other map with a different diagonal needs the constant
  re-derived. The repo is single-map today; the plan documents this and keeps
  the fog derived from the constant in code.

## Increment DAG

- Inc 1 — Distant: material-sort at load (M) — depends: none — unblocks: 3
- Inc 2 — Fog: complete within the drop threshold (M) — depends: none — unblocks: 3
- Inc 3 — 30 fps tuning pass (S) — depends: 1, 2 — unblocks: —

```
Inc1 ──┐
       ├──► Inc3
Inc2 ──┘
```

Inc 1 and Inc 2 are independent (both are load-time/config changes to
different files) and can land in parallel; both unblock the on-device tuning
pass.

## Increments

### Inc 1 — Distant: material-sort at load (M) — DONE
**Depends on:** none
**Unblocks:** 3
**Status:** Implemented + verified 2026-08-12. `LvlRoomRenderer::Load` now
material-sorts before coalescing (mirrors `TexturedRoomRenderer`). New host
test `tests/distant_sort_contract.cpp` asserts sorted ≤ adjacent run count +
geometry equivalence (passes). Runtime-verified in Ares: all 45 `*_distant.lvl`
load with `0 discarded` (material sort applied), no crashes. Host suite: 26
passed, 0 failed. ROM builds clean.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.cpp
- What changes: in `Load`, after building `specs[]` and before calling
  `CoalesceBatches`, stable-sort by material and physically reorder a scratch
  `FaceSpec` array using the permutation (mirror `textured_room_renderer.cpp`
  lines 198-207). `CoalesceBatches` takes a contiguous array and iterates
  linearly — it cannot consume indices directly.
- Function(s): `bool Load(const char* lvl_path, const Vec3& render_origin,
  float pos_scale)` — add the sort step in the `FreeRuns()` block.
- Data shapes: `FaceSpec* specs` (existing), `uint16_t* order`, `FaceSpec*
  sorted_specs` (scratch, freed after coalesce).
- Integration points: `DistantWorldRenderer::Load` (distant pass) and
  `TileStreamer::SetCenter` (near flat pass, when textures off).
- Error paths: sort/alloc failure → `run_count_ = -1` → fall back to the
  existing per-face batch path (never a silent truncation). `DiscardedFaces()`
  stays 0.

##### src/user/gameplay/render/lvl_room_renderer.hpp
- What changes: no signature change. `Load` already includes
  `batch_coalesce.hpp` (for `FaceSpec`/`BatchRun`/`RunFace`). No header edit
  needed unless a comment is added.

#### Edge cases
- Faces sharing vertices across materials: the sort reorders faces but each
  face still loads its own vertex span (runs cap at 70 vertices), so shared
  vertices are loaded per run — correct, just slightly more vertex loads.
- 70-vertex cap may bind: `CoalesceBatches` splits a run when the loaded span
  exceeds 70. Sorting *faces* by material does not reorder *vertices*, so a
  material whose faces are scattered across the vertex array produces a run
  span covering the gap. The done-criterion (syncs ≈ distinct materials, ~6.7/
  cell) is a best case; the measured win is still large (~3.3×) even if the
  cap splits some runs above the best case.
- The sort is stable → within a material, faces keep load order (no geometry
  change, only grouping).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: extend `tests/material_sort_contract.cpp` (or add a new
  `tests/distant_sort_contract.cpp`) with a run-count comparison: build an
  interleaved `FaceSpec` list (materials alternating, simulating the bake's
  arbitrary order), run (a) adjacent-only `CoalesceBatches` and (b)
  `SortFacesByMaterial` + `CoalesceBatches`, and assert (b) produces ≤ runs
  than (a) AND the same triangle set (reuse the `TrianglesUncoalesced` /
  `TrianglesCoalesced` geometry-equivalence replay pattern). Wire into
  `tests/run_host_tests.sh`.
- Done: host test passes; device `[counters] syncs` drops from ~340-450 toward
  ~100-134 when looking across the map (~3.3× reduction).

### Inc 2 — Fog: complete within the drop threshold (M) — DONE
**Depends on:** none
**Unblocks:** 3
**Status:** Implemented + verified 2026-08-12. `gameplay_scene.cpp` now derives
the fog range from `sqrt(kDistantMaxDist2) * 0.4/0.9` (≈532 → 1197) when
`world_bounds_valid_`, falling back to the old `distant_far` range otherwise.
`lod_math.hpp` comment documents the drop/fog invariant. Extended
`tests/fog_math.cpp` with the named-ratio drop/fog coupling assertions (passes).
Runtime-verified in Ares: fog config block executes cleanly, no crash. Host
suite: 26 passed, 0 failed. ROM builds clean.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: retune the fog range from `distant_far * 0.4 → distant_far *
  0.9` (1485 → 3342) to a range derived from the **drop threshold** so fog
  completes before cells are dropped. Compute
  `const float drop_dist = sqrtf(kDistantMaxDist2);` and set
  `MakeFog(drop_dist * 0.4f, drop_dist * 0.9f, {120,150,180})`. For this map:
  drop ≈ 1330 → min ≈ 532, max ≈ 1197. This keeps `kDistantMaxDist2` unchanged
  (cell count stays low) and makes the horizon fade within the map.
- Function(s): the fog-config block at `gameplay_scene.cpp:412-419`.
- Data shapes: `FogParams` (unchanged).
- Integration points: `open_world_.SetFog(fog)`.
- Error paths: `world_bounds_valid_` false → keep the current
  `distant_far * 0.4/0.9` range as the fallback (the existing behavior), so a
  null/empty bounds never produces an invalid `MakeFog(0,0,…)`. `MakeFog`
  clamps min to `kFogMaxMinDistance` (4000) — the new min (532) is well under,
  so no clamp.

##### src/user/gameplay/render/lod_math.hpp
- What changes: **no constant change** — `kDistantMaxDist2` stays `1.77e6`
  (threshold 1330). Update the comment to document the fog-coupled invariant:
  the fog range must complete before `sqrt(kDistantMaxDist2)` so dropped cells
  are fully fogged. Note the constant is map-specific (re-derive per map).
- Function(s): none (comment only).
- Data shapes: none.
- Integration points: none (the fog is derived from this constant in
  `gameplay_scene.cpp`).
- Error paths: none.

#### Edge cases
- Camera at a map corner looking across: cells beyond the drop threshold
  (1330) are dropped and fully fogged (fog completed at 1197) — no pop. Near
  cells (within the ring) are unaffected.
- Fog completes at 1197, which is within the map diagonal (3228) — the horizon
  fades to atmosphere inside the map, as the user requested.
- `distant_cull_contract.cpp` must keep passing: it calls the culled builder
  with explicit `max_dist2` args — the constant is unchanged, so no impact.
- **Depth-space caveat (C1):** the `0.4/0.9 × drop` range is a world-XZ
  approximation fed into `t3d_fog_set_range`, which operates in the distant
  projection's depth space (non-linear ~1/z). Fog completes at world distance
  1197 only along the view axis; off-axis cells are at greater depth and thus
  *more* fogged at the drop — directionally safe. The Inc 2 host test asserts
  a structural invariant (fog max < drop in world units), NOT a visual-pop
  guarantee; Inc 3 tunes the ratios by feel.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: extend `tests/fog_math.cpp` (or add a new
  `tests/horizon_fade_contract.cpp`) to assert the drop/fog coupling with
  NAMED ratio constants (not literals), so a future ratio > 1.0 is caught:
  `constexpr float kFogMinRatio = 0.4f, kFogMaxRatio = 0.9f;` then (a) the fog
  range `MakeFog(sqrt(kDistantMaxDist2)*kFogMinRatio,
  sqrt(kDistantMaxDist2)*kFogMaxRatio, …)` passes `ValidateFogRange`; (b)
  `kFogMaxRatio <= 1.0f` (fog completes before the drop threshold, so dropped
  cells are fully fogged). This test is buildable because it only uses the
  compile-time `kDistantMaxDist2` constant (not a runtime `fog_max`). Wire
  into `tests/run_host_tests.sh`.
- Done: host test passes; device `[counters] distant_cells` stays ~15-20 when
  looking across the map, no pop at the drop edge.

### Inc 3 — 30 fps tuning pass (S) — PENDING MANUAL ON-DEVICE
**Depends on:** 1, 2
**Unblocks:** —
**Status:** 2026-08-12 — NOT completed autonomously. This increment requires
an interactive on-device session: drive the camera to the whole-map view and
read `[profiler]`/`[counters]` under valid timing. Cannot be done in this
environment because (a) Ares is GUI-only with no input-injection to move the
camera, and (b) Ares runs the ROM at ~0.1 fps (16.5 s/frame, software
paraLLEl-RDP + USB-debug output), so its absolute ms timing is NOT a valid
proxy. The constant changes (if any) are safe to make later: `kDistantMaxDist2`
and the fog ratios follow the Inc 2 coupling automatically. A host-side
estimate at the map center showed ~30 cells within the drop threshold (before
frustum culling), so the on-device `distant_cells` count should be verified
against the ≤~20 target before closing.

#### Files to touch
- No new files. Tune the compile-time constants from Inc 1/2 based on the
  on-device `[counters]` + `[profiler]` report. Files in play:
  `src/user/gameplay/render/lod_math.hpp` (`kDistantMaxDist2`),
  `src/user/gameplay/scene/gameplay_scene.cpp` (fog ratios),
  `src/user/gameplay/render/distant_world_renderer.cpp` (`kCullMargin`):
  - `kDistantMaxDist2` (`lod_math.hpp`): adjust if cells pop (raise) or the
    whole map stays drawn (lower). If changed, the fog range in
    `gameplay_scene.cpp` (derived from `sqrt(kDistantMaxDist2)`) follows
    automatically.
  - Fog min/max ratios (`gameplay_scene.cpp`): adjust the `0.4/0.9` ratios if
    the fade is too early (visible geometry fades) or too late (pop at the
    drop). Remember the fog range is in depth space (non-linear ~1/z), so tune
    by feel, not by world distance.
  - `kCullMargin` (`distant_world_renderer.cpp`): tighten/loosen the distant
    frustum cone if horizon cells pop at the screen edge.

#### Edge cases
- Walk the whole map from `cell_00_00`; confirm no hard hitches, no popping at
  LOD/frustum edges, no Z-fighting, no visible tile loading.
- Ares timing is a proxy; cross-check on Mupen64Plus or real hardware before
  closing the budget (RSP/RDP timings differ).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: none (tuning only).
- Done: on-device profiler report meets the 30 fps budget with the counters
  within the targets above.

## Cross-cutting verification

After Inc 3, on device (Ares CLI per `AGENTS.md`):
1. Boot the ROM; let it run ≥ 120 frames; read the `[profiler]` + `[counters]`
   report every 60 frames.
2. Confirm `avg frame time ≤ 33.3 ms`, `distant ≤ 12 ms`, `high_priority ≤ 8 ms`.
3. Turn the camera to the whole map; confirm `distant_cells ≤ ~20` and `syncs`
   ~100-134 (was ~340-450 across the ~15-20 drawn cells; ~1015 across all 45).
4. Walk the whole map; confirm the horizon fades to atmosphere within the map
   (no hard cut), no pop at the drop edge, no Z-fighting, no straight cuts at
   cell boundaries.

## Standards / common-mistakes referenced
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: none (no new
  `rom:/` paths).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: none (no
  bake changes; the material sort preserves per-face fan origins).
- `.agents/map-creation.md` — applies to: none (no new rooms).

## Open questions (CONSIDER from review)
- `docs/perf_budget.md` documents `kDistantMaxDist2` and the fog; update it in
  Inc 2/3 to reflect the new fog range and the drop/fog coupling.
- The distant run baseline (1015 → 303 runs, ~3.3×) is measured from the baked
  LVLs; if a re-bake changes the face order, re-measure before trusting the
  Inc 1 win.

## Out of scope
- Aggressive bake-time decimation (quadric error / silhouette tracing) — future
  work; the runtime material sort is the direct fix.
- Texturing the distant pass — future work; distant cells stay flat-colored.
- Near-pass visibility culling — rejected in the prior plan (straight-cut
  regression); the near ring stays fully drawn.
- Growing `kMaxRing` — parked; not needed for this bottleneck.
