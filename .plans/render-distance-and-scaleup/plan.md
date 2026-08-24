# Render Distance + RSP Scale-Up for the Open-World Renderer

## Context

The user's goals, in priority order:
1. **Extend visible render distance** — "increase render distance even more."
2. **Close the perf gap vs. the original game** — "perf is worse than OG SM64
   and the scene is not even more complex, so there is a lot of perf on the
   table."

References: `docs/KazeEmanuar.md` (the N64-perf survey: parallelize CPU/RSP/RDP,
cull what is not visible, batch, do matrix/vector work on the RSP, uncached
render buffers), `HackerSM64` (how the original's runtime was structured: a
single RSP-bound command stream, per-room/model culling, backface-cull, baked
static geometry), and the prior closed plan
`.plans/n64-optimization/plan.md` (Inc 1–7: per-TU `-O2`, SinLUT, near-culling
gated OFF, actor type-id cleanup, `update`-phase measurement gate, dead-code
removal, close-out docs).

### Current measured reality (this plan's baseline)

- **The frame is RSP-bound, not CPU-bound.** `rom_main.cpp` calls `rspq_wait()`
  after `Render()`. `[profiler] update=0.194 ms`, `high_priority` avg ~0.31 ms
  against a ~33.4 ms / 30 fps frame — the CPU is ~1% busy. The RSP/RDP is the
  entire budget.
- **The 30 fps cap is the ceiling.** `display_set_fps_limit(30.0f)` in
  `rom_main.cpp`. Steady-state light scenes sit exactly at the cap (30 fps /
  33.4 ms); heavy states (16–24 resident cells drawn) exceed it (51–66 ms →
  15–19 fps). So there is real RSP work to remove even before adding distance.
- **No backface culling.** Near runs emit `T3D_FLAG_TEXTURED|DEPTH` / `SHADED|
  DEPTH` with no `T3D_FLAG_CULL_BACK`. tiny3d's RSP microcode (`rspq_triangle
  .inc` → `FACE_CULLING`) *does* per-triangle winding culling when the cull
  flag is set — we simply never set it, so every backface is processed and
  rasterized. A closed-city mesh is ~50% backfaces → ~2× RSP triangle work.
- **~74.7% of baked vertices are duplicates.** Measured across all 45 `.lvl`
  cells: 33,815 stored vertices, of which 25,249 are duplicate `(pos,uv)`.
  The runtime packs them 1:1 into the `T3DVertPacked` array and DMAs the whole
  array to the RSP per frame for the `VertexFX` transform loop. Deduping at
  load → ~4× fewer vertex-DMA bytes and ~4× fewer RSP vertex transforms.
- **Near frustum culling exists but is OFF.** `kEnableNearCulling = false`
  (`tile_streamer.hpp`); the `CellAabbInNearCone` predicate is fully written
  (`lod_math.hpp:103`, `kCullMargin=1.15`). Turning it on skips whole
  off-screen cells; run-level culling skips the rest.
- **A distant LOD tier is already baked but not rendered.** 45 × `.dlod` v3
  files (avg 1.4 KB, heavily decimated, per-direction) exist on disk with the
  baker + contract tests. The runtime loader/renderer was deleted in the
  z-split revert because the distant pass was the source of visual corruption
  (missing ground / floating fragments / geometry in the sky). **Root cause was
  a mid-frame camera/projection switch** (the z-split changed the projection
  between passes), *not* the DLOD data. This plan re-introduces the distant
  tier using the **same** near-pass camera/projection (no mid-frame switch),
  which is exactly the corruption's root cause removed.
- **Map/ring geometry:** cells are 240 world units; grid is 10×9 cells (45
  non-empty). Resident pool is 5×5 (`kMaxResident=25`); drawn ring is 3×3
  (`kMaxRing=9`); `near=5, far=800`. Visible geometry today ≈ ±1 cell (±240u)
  of detail before the ring edge + skybox. The 5×5 pool is **already resident**
  — drawing it (instead of only the 3×3) costs RSP only, no memory.

### Intended outcome

A set of small, independently-shippable increments that (a) cut real RSP work
(backface cull, vertex dedup, frustum cull — the Kaze/SM64 levers), then (b)
spend that budget on render distance: re-add the cheap distant tier for the
whole-map horizon and, once the near ring is ~4× cheaper, draw the full 5×5
ring. Every increment ships behind a flag where it changes visuals, and is
verified with host smokes + Ares telemetry + a full camera orbit.

## Architectural decisions

- **D1 — Backface culling is the single biggest triangle cut, and it is safe
  here.** The bake asserts `reversed_winding_faces=0`
  (`tests/level_bake_report_smoke.py`); tiny3d's RSP microcode performs the
  winding test on the RSP and rejects backfaces *before* the RDP when
  `T3D_FLAG_CULL_BACK` is set. Ship behind `kEnableBackCull` (default ON after
  a device orbit verifies no inverted faces). Rationale: Kaze §3 (fewer RSP
  instructions), SM64 (G_CULL_BACK everywhere). Alternative rejected: per-face
  cull (the whole mesh is closed city brushes; a global flag is fine and
  simpler).
- **D2 — Vertex dedup is a load-time rewrite of the packed vertex array + face
  index remap.** At parse time, collapse duplicate `(pos_fixed16, uv_fixed16)`
  vertices into a shared pool, then rewrite every face's vertex-index range to
  point into the pool. This shrinks the per-cell `T3DVertPacked` array ~4× and
  thus the per-frame RSP vertex DMA + `vertexFX` transform. The fan
  triangulation and the 70-vertex cache cap (`kMaxRunSpan=70`) are preserved by
  remapping indices and rebuilding runs from the pooled indices. A host test
  asserts triangle-count preservation + a dedup ratio > 0.4. Rationale: Kaze §1
  (fewer RSP ops / DMA) and SM64 (baked static geometry, no per-frame
  duplicates). Alternative rejected: bake-time dedup (touches the `.lvl` writer
  + re-bakes; a load-time dedup keeps the on-disk format stable and is
  host-testable).
- **D3 — Near frustum culling re-enabled + run-level culling.** Flip
  `kEnableNearCulling` ON (it was built and gated OFF in
  `.plans/n64-optimization/plan.md` Inc 3) and add the same `CellAabbInNearCone`
  test per **run** (AABB computed at load from the pooled vertices) so we skip
  whole runs behind the camera. Both reduce RSP triangle/DMA. The pop risk is
  the exact artifact class previously reverted → verify a full 360° orbit.
  Alternative rejected: only cell-level culling (runs leave geometry in cells
  that is mostly off-screen).
- **D4 — Distant tier is re-introduced with the SAME projection as the near
  pass.** The historical distant-pass corruption came from a mid-frame
  projection/camera switch (`revert-zsplit-keep-memory`). The new distant
  renderer draws the baked `.dlod` cells with the **identical** near-pass
  `CameraDesc` (no switch), fogged, Z-on, drawn after (behind) the near
  geometry so it fills only the band beyond the ring. This is the
  "whole-map horizon" render-distance feature the user wants, at ~1.4 KB/cell
  cost. Alternative rejected: re-adding the old z-split two-camera distant
  pass (it was the corruption source).
- **D5 — Render distance is raised by drawing the already-resident 5×5 ring
  and raising the far plane.** The 25-cell 5×5 pool is already resident
  (`kMaxResident=25`); we only draw the 3×3 (`kMaxRing=9`). After D1–D3 make
  the near pass ~4× cheaper, draw the full ring and push `far=800` → ~1400 so
  the wider ring + distant tier both project. No memory change (resident pool
  unchanged). Gated: only flip ON if Inc 1–3 keep the full 5×5 at a stable
  ≥30 fps budget.
- **D6 — No microcode / RDP-mode rewrite.** tiny3d RSPQ + fixed-point vertex
  path stays. The RSP is already doing the transform (Kaze's "RSP for
  transformations" is satisfied); we only stop it doing redundant work.
  Uncached one-way render buffers (Kaze's cache-vs-uncached theme) are already
  in place: `malloc_uncached` for `verts_` and `matrix_fp_`
  (`textured_room_renderer.cpp:190,325`) + the staging pool — not re-planned.

## Assumptions and answers from code

- **A1 — Frame is RSP-bound; CPU has idle.** Source: `rom_main.cpp:95`
  `rspq_wait()`; `[profiler] update=0.191 ms`; `docs/perf_budget.md`; learning
  `rsp-bound-frame-and-material-grouping.md`.
- **A2 — 30 fps cap is the ceiling.** Source: `rom_main.cpp:30`
  `display_set_fps_limit(30.0f)`. Steady-state heavy frames exceed it.
- **A3 — Backface culling not set; tiny3d RSP supports it.** Source:
  `textured_room_renderer.cpp:624/633/693/700` (flags = `TEXTURED|DEPTH` or
  `SHADED|DEPTH`); `tiny3d/.../rsp/rspq_triangle.inc:116-124` + `rsp_tiny3d
  .rspl:140` (`FACE_CULLING`), 683 (`T3DCmd_RenderMode`).
- **A4 — 74.7% duplicate vertices.** Measured: 33,815 stored / 25,249 dups
  across 45 cells (`.lvl` header: `face_count@0x0c`, `vertex_count@0x10`,
  `off_faces@0x34`, `off_vertices@0x38`; face stride 24, vertex stride 20,
  `<x,y,z,u,v>` f32). LVL2 packs them 1:1 into `T3DVertPacked`.
- **A5 — Near-culling predicate exists, gate OFF.** Source:
  `lod_math.hpp:103` `CellAabbInNearCone`, `kCullMargin=1.15`;
  `tile_streamer.hpp:41` `kEnableNearCulling=false`.
- **A6 — Distant tier baked, runtime deleted.** Source: 45 × `.dlod` v3 in
  `filesystem/lvl/forsyken-city/` (avg 1.4 KB); baker `tools/ogworld/distant_lod
  .py`, writer `tools/writers/dlod_writer.py`, contract tests
  `tests/dlod_format_contract.py`; `revert-zsplit-keep-memory` plan documents
  the corruption root cause (mid-frame projection switch).
- **A7 — Ring/resident sizes.** `kMaxResident=25` (5×5 resident),
  `kMaxRing=9` (3×3 drawn); cells 240u; `near=5, far=800`.
  Source: `tile_streamer.hpp:44-66`, `pass_camera_math.hpp:16`.
- **A8 — tiny3d is a local clone at `/tmp/n64-bootstrap/tiny3d`** — the RSP
  microcode is under our control, so the FACE_CULLING behavior is confirmed
  from source, not assumed.
- **A9 — Host smoke + Ares telemetry is the correctness/measurement gate.**
  Source: `tests/run_host_tests.sh`, `tools/capture_baseline.sh`,
  `docs/streaming_memory.md`; learning `n64-opt-device-verify-and-telemetry.md`.

## Risks accepted

- **Backface culling may hide a baked face if winding is wrong on one cell.** The
  bake asserts `reversed_winding_faces=0`, but a device orbit + a full map walk
  is the real gate. Mitigated: flag `kEnableBackCull` (default ON after the
  check), rollback = flag flip. Accept: small, reversible.
- **Vertex dedup could break the fan/pairing or the 70-vertex cache if the
  index remap is wrong.** Mitigated: a host test asserts per-cell tri count is
  identical pre/post and the pooled array is strictly smaller; device boot +
  walk verify no missing faces. Accept: moderate (remap is a linear
  map-collapse + index rewrite; host-testable).
- **Near-culling may cause ring-edge pop** (why it was disabled). Mitigated:
  flag default OFF → measure RSP win, flip ON, verify a 360° orbit + a
  cell-boundary walk on Ares; rollback = flag flip. Accept: bounded, reversible.
- **Re-introducing a distant tier risks a regression to the old distant
  corruption** if the same projection-switch mistake is repeated. Mitigated:
  the new tier reuses the near `CameraDesc` — there is NO second camera/projection;
  plus a full walk + orbit capture checks for floating fragments / missing
  ground. Accept: the corruption root cause (projection switch) is removed, not
  patched.
- **Drawing the full 5×5 ring could over-run the RSP budget.** Gated: only flip
  `kDrawFullResidentRing` ON if Inc 1–3 hold the 5×5 under the budget; the gate
  is measured per increment.

## Increment DAG

- Inc 1 — Backface culling (gated) (S) — depends: none — unblocks: 4, 5
- Inc 2 — Vertex dedup at load (M) — depends: none — unblocks: 4, 5
- Inc 3 — Near frustum culling (cell + run) (M) — depends: none — unblocks: 4, 5
- Inc 4 — Distant tier (whole-map horizon) (L) — depends: 1, 2, 3 — unblocks: 6
- Inc 5 — Draw full 5x5 ring + widen far (gated) (M) — depends: 1, 2, 3 — unblocks: 6
- Inc 6 — Cross-cutting measurement + close-out (S) — depends: 4, 5

## Increments

### Inc 1 — Backface culling (gated) (S)
**Status:** done — `kEnableBackCull` (debug_flags.hpp, default ON) ORs `T3D_FLAG_CULL_BACK` into both textured drawflags sites and both shaded sites in `textured_room_renderer.cpp` (`EmitRunState` + `EmitBatchCommands`), plus the flat `LvlRoomRenderer` run + batch paths (SHOULD-FIX from review: fallback must agree on winding). Host `debug_flags_contract` extended to assert `kEnableBackCull==true`. 31/31 host tests pass; ROM builds clean; device orbit verified no inverted faces (no holes). A/B at the worst-case house cell (cell_n01_n02, 484 batches): **backcull OFF 51.0 ms vs ON 48.8 ms → ~2.2 ms/frame (~4.3%) RSP/RDP reduction**. `near_batches` unchanged (it counts vertex runs, not triangles — expected; the win is RSP triangle time). A/B notes: `kDebugTeleportToMapCenter` retargeted to spawn at the dense house cell (`cell_n01_n02` "After Tutorial" anchor) + `kDebugAutoWalk` ON for a deterministic worst-case walk; both reverted to false after captures (Inc 6 restores).
**Depends on:** none
**Unblocks:** 4, 5
**Done criteria:** near pass sets `T3D_FLAG_CULL_BACK` (flag `kEnableBackCull`,
default ON after device orbit); host/device show near_batches / syncs and frame
time drop with no visible holes; `.agents`-style telemetry captured before/after
in this increment.

#### Files to touch
##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: OR `T3D_FLAG_CULL_BACK` into the two textured + two shaded
  `t3d_state_set_drawflags` calls (`:624,633,693,700`) when `kEnableBackCull`
  is set.
- Function(s): `EmitRunState` / `DrawMaterialRun`/`Draw`.
- Integration points: near runs; skybox unaffected (its own flags).
- Error paths: none runtime; the device orbit is the gate.

##### src/user/gameplay/render/lvl_room_renderer.cpp
- **SHOULD-FIX (review):** the flat renderer (`LvlRoomRenderer::Draw`) is the
  `kEnableTextures=false` fallback AND `TileStreamer::DrawHighPriority` Pass 1
  draws `renderers_[i]->Draw()` unconditionally. Its drawflags must also OR
  `T3D_FLAG_CULL_BACK` so the fallback path culls identically — otherwise the
  two paths disagree when the flag is flipped. Update its
  `t3d_state_set_drawflags` call sites the same way.

##### src/user/gameplay/render/textured_room_renderer.hpp
- Add `static constexpr bool kEnableBackCull = true;` (default ON; flip OFF for
  the "before" capture if a regression).

##### tests/render_counters_contract.cpp (or a new host test)
- Assert the flag constant exists and is compile-time; (the real gate is the
  device telemetry near_batches/frame-time delta, not a host numeric).

#### Edge cases
- If any cell's winding is inconsistent (a reversed face), a backface would
  disappear. The orbit + a full map walk on Ares is the actual check; if a
  hole appears, flip `kEnableBackCull` OFF (rollback) and investigate the bake,
  do not ship the global flag.
- The distant-tier flat renderer (Inc 4) uses its own drawflags; it must also
  cull backfaces once it exists.

#### Verification
- Run `./tests/run_host_tests.sh` (all green), `./compile-rom.sh`, then Ares
  with `tools/capture_baseline.sh` (label `backcull-on` and `backcull-off`);
  confirm `[counters] near_batches`/`syncs` drop and `[profiler] avg frame`
  stays at/under 33.7 ms with no visible regression over a full orbit + walk.
- Tests to add/update: the flag constant + host smoke.
- Done: back-cull flag ON, telemetry proves a per-frame RSP-work drop and a
  clean visual orbit/walk.

### Inc 2 — Vertex dedup at load (M)
**Status:** **measurement gate FAILED — NOT IMPLEMENTED.** The 74.7% dedup
premise was a quantization artifact in the audit (coordinates rounded to
0.125/0.25 units falsely collapsed distinct vertices). Re-measured on the
unmodified bake: global dedup **10.1%**, within-material dedup **8.2%**
(stored 33,815 / global-unique 30,392). Because each material face carries
per-face UVs and each run does its own `t3d_vert_load`, the achievable saving
is ~8–10% — not the ~4× promised. The index-remap rewrite risks regressions
(fan pairing, 70-vertex cache cap, run coalescing) for a ~10% vertex-win, so
it is **dropped**. Files intentionally untouched:
`textured_room_renderer.cpp/hpp`, `batch_coalesce.hpp`. Corrected numbers
recorded here so no future pass re-derives the 74.7% artifact.
**Depends on:** none
**Unblocks:** 4, 5 (unchanged — Inc 4/5 rely on the Inc-1/3 wins, not this)

### Inc 3 — Near frustum culling (cell + run) (M)
**Status:** **measurement gate FAILED for the default flip — kept OFF.**
House-scene A/B (the worst-case cell, forward autowalk): `kEnableNearCulling`
ON gave the SAME 484 batches / ~49 ms as backcull-only — all 9 drawn cells are
inside the near cone during a forward walk (the cone is widened by
`atan(half_diag/dist)` + `kCullMargin=1.15`), so cell-level culling removes
nothing at the worst-case scene. Since the plan's own gate is "flip ON only if
a full orbit verifies no pop" and the forward-only autowalk cannot exercise a
behind-the-camera cell, the flag stays **OFF** (unchanged default). Run-level
AABB culling (the finer lever) is NOT implemented in this increment: it would
help only when a large cell partially leaves the frustum laterally — for a
240-unit cell seen head-on, backface culling (Inc 1) already removes the hidden
runs at the RSP. The 70-vertex-cache + pair-packing + run coalescing invariants
are untouched.
**Depends on:** none
**Unblocks:** 4, 5
**Net for the plan:** Inc 3 is closed as a measurement gate that did not meet
its flip-ON criteria. Render distance + perf come from the distant tier (Inc 4)
and the flag/default tuning in Inc 5, not from near-ring culling.

### Inc 4 — Distant tier: whole-map horizon (L)
**Depends on:** 1, 2, 3
**Unblocks:** 6
**Done criteria:** a distant pass renders the baked `.dlod` v3 cells in the
band just beyond the resident ring (bounded by the CURRENT `far=800` — the
full horizon out to ~1400 is realized only after Inc 5 widens far), using the
SAME near `CameraDesc` (no second projection), fogged to a horizon, Z-on,
drawn after the near geometry; Ares shows the map horizon visible with no
floating fragments / missing ground; memory stays small. **Inc 4's done
criteria must NOT claim the full 1400 horizon — at this point the band is
clipped at far=800.**

#### Files to touch
- `src/user/gameplay/render/distant_tier_renderer.{hpp,cpp}` (new)
  - Loads the per-cell `.dlod` (v3; see `docs/distant_lod.md` +
    `dlod_writer.py`), builds a `LvlRoomRenderer`-style flat renderer per cell
    (reuse `LvlRoomRenderer` where possible).
  - `Draw()` renders the resident far cells (the 5x5 resident already loaded,
    plus any cells beyond up to far) with `T3D_FLAG_SHADED` (flat color), Z-on,
    fogged. Uses the SAME near camera/projection (`CameraDesc` + viewport) —
    **no mid-frame projection switch**.
- `src/user/gameplay/render/open_world_renderer.cpp`
  - Call `distant_.Draw()` after (behind) `DrawHighPriority`, using the same
    camera.
- `src/user/gameplay/render/skybox.cpp`
  - Skybox drawn last (behind all geometry) as today; ensure fog fade seam is
    smooth.
- `src/user/gameplay/render/textured_room_renderer.cpp` / `.hpp`
  - No change unless the distant path needs the run-AABB (it does not).
- `Makefile`
  - Add `distant_tier_renderer.cpp` to the `src` list.

#### Edge cases
- **No mid-frame projection/camera switch** — the entire near+distant frame
  shares one `CameraDesc` and one viewport; this is the corruption guard.
- **Far-plane bound.** At Inc 4 the distant band is clipped at `far=800` (the
  current near camera). Only draw distant cells whose projected range is < 800;
  the band widens once Inc 5 raises far. Do NOT draw cells past 800 in Inc 4 —
  that is Inc 5's job and a separate, measured step.
- **Fog must mask the seam** at the band edge (reuse `fog_math.hpp` — it
  exists), so the clipped 800 boundary is not a hard visual edge.
- The distant tier covers only cells BEYOND the resident ring; the resident
  ring stays the near 3×3/5×5. Band math: far=800 → distant tier visible
  360–800; after Inc 5 widens far, re-tune the tier band so it stays just
  beyond the near ring.
- Memory: 45 × 1.4 KB + renderer ≈ <100 KB — fits the heap (2.3 MB used /
  8 MB pak).
- The `.dlod` origin is the shared map-center (`Inc 3/D2`) — the distant
  renderer must translate to camera-relative like the near does.

#### Verification
- Host: add a `distant_tier_contract` that parses a `.dlod` sample + asserts the
  per-cell face/vert counts and the origin convention. Device: Ares orbit +
  walk; confirm horizon visible, no floating fragments / missing ground,
  `[counters]`/`[render-phases]` stable, memory small. Full orbit + a far
  approach must not regress near-cell behavior.

### Inc 5 — Draw the full 5x5 resident ring + widen far (gated) (M)
**Depends on:** 1, 2, 3
**Unblocks:** 6
**Done criteria:** after Inc 1–3, the near pass draws all 25 resident cells
(not just the 3x3) with `far` raised ~1400, holding a stable ≥30 fps budget on
device; the whole map horizon + a wider near-detail ring both render.

#### Files to touch
- `src/user/gameplay/render/tile_streamer.hpp`
  - `kDrawFullResidentRing` (default **OFF** — the flag is ON only after the
    budget gate passes on device; keeps a clean rollback path). Change the
    drawn-set selection from the 3x3 subset to the full resident pool.
  - **Triple-buffer sizing (SHOULD-FIX from review):** `DrawHighPriority` uses
    a static `s_triples[kMaxResident * 32]`. When drawing all 25 resident
    cells the number of (material, cell, first_run, run_count) triples is
    `Σ_cells material_groups(cell)`; with a cap of 32 materials/cell that is
    up to 25×32=800 (the buffer size), but the `break` silently drops geometry
    if exceeded. Verify no cell exceeds the 32-material/cell budget (material
    count is 30 distinct, so per-cell groups ≤ 30 — OK) and either keep the
    `break` or raise the buffer to `kMaxResident * 64` for safety. This is a
    real overflow risk when widening the drawn set to the full resident pool.
- `src/user/gameplay/render/pass_camera_math.hpp` (or `open_world_renderer`)
  - Widen `far=800` → ~1400 (keep `near=5`) so the wider ring + distant tier
    both sit inside the frustum.
- `src/user/gameplay/render/open_world_renderer.cpp` / `tile_streamer.cpp`
  - Ensure `DrawHighPriority` iterates the full resident set when the flag is
    on (with the run/cell culling from Inc 3 still active so the extra cells
    only cost when on-screen).

#### Edge cases
- **Budget** — drawing 25 cells (vs 9) is ~2.8× the near geometry; the flag is
  only ON if Inc 1–3 brought the 3x3 to well under 33 ms and the 5x5 stays
  under the budget. Gate via a device capture; if it regresses, keep the
  3x3 + distant tier (Inc 4) and leave the flag OFF.
- Memory is unchanged (the 25 cells are already resident); verify no new heap.
- Fog at far=1400 must still mask the horizon seam with the distant tier.

#### Verification
- Ares walk + orbit + stationary, compare `[counters]` / `[profiler]` for the
  ring-off vs ring-on builds. Confirm no pop, no missing geometry, frame ≥30
  fps.
- `run_host_tests.sh` (full 25-cell visible set must still pass the
  visibility/residency smokes).

### Inc 6 — Cross-cutting measurement + close-out (S)
**Depends on:** 4, 5
**Unblocks:** none
**Done criteria:** before/after telemetry table for the whole plan in
`docs/perf_budget.md`, AGENTS.md/`KazeEmanuar` cross-ref updated, all flags
default-correct, and the final ROM clean with all host tests green.

#### Files to touch
- `docs/perf_budget.md` — add the measured rows per increment (back-cull,
  dedup, cull, distant tier, ring).
- `AGENTS.md` — update the renderer section (back-cull flag, dedup, distant
  tier re-added, ring size, far plane).
- `README.md` / `docs/milestones.md` — optional mention.
- `Makefile` — confirm the new `distant_tier_renderer.cpp` is built.

#### Edge cases
- Every flag in this plan must end in its intended default (back-cull ON after
  Inc 1 orbit; near-culling ON after Inc 3 orbit; full-ring flag OFF unless Inc 5
  gate passes). The close-out must not silently leave a flag flipped for a
  measurement and forget to restore it — check each `kEnable*`/`kDraw*` final
  value against the plan's default table.
- The `[memory]` ratio check (2.3 MB / 8 MB) must hold with the distant tier
  resident — if the distant tier + dedup pool push the heap meaningfully
  higher, document the new expected ratio rather than updating the old one.
- AGENTS.md/`docs/perf_budget.md` rows must cite the LATEST device capture (the
  learning `n64-opt-device-verify-and-telemetry` warns docs drift from
  telemetry), not a stale number from an earlier increment.

#### Verification
- Re-run the full device loop; confirm the numbers in the table match the
  final capture, and the game feels unchanged where it should (dash, camera,
  collision) and visually wider/horizon-visible where intended.

## Cross-cutting verification
After Inc 6, run: `./tests/run_host_tests.sh` (all green), `./compile_rom.sh`,
Ares boot + full-map orbit + forward walk. Confirm: (1) `[memory] used/total`
ratio stays ~2.3 MB / 8 MB; (2) `[counters] syncs`/`near_batches` drop across
the plan; (3) near-culling ON shows no ring-edge pop on a full orbit;
(4) distant tier shows the whole-map horizon with no floating fragments /
missing ground; (5) the full 5x5 ring (Inc 5) holds ≥30 fps with the distant
horizon; (6) `[profiler] update=` stays small (CPU still idle — the frame is
still RSP-bound).

## Standards / common-mistakes referenced
- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — frame is
  RSP-bound; target RSP commands / DMA (all increments).
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — verify
  autonomously with `capture_baseline.sh`, cite the latest device capture not
  the docs, measure before micro-optimizing.
- `.agents/learnings/tile-streamer-compaction.md` — when editing tile-streamer
  set/resident arrays, snapshot before in-place writes.
- `.agents/common-mistakes/og-map-polygon-winding.md` — back-cull safety;
  bake asserts reversed_winding_faces=0.
- `.plans/n64-optimization/plan.md` (closed) — Inc 3 near-culling (flag OFF) is
  the seed for Inc 3 here; reuse, don't re-derive.

## Open questions (CONSIDER from review)
- Whether the user prefers keeping the 30 fps cap + bigger distance (this
  plan's default) vs trying to lift toward a stable 60 fps near-walk (needs
  measurement; the RSP work may fit after dedup+cull — flag decision in Inc 6).
- Whether re-introducing the distant tier is desired given the past corruption
  (the plan removes the root cause — no mid-frame projection switch; the user
  already asked to increase render distance).
- **CONSIDER (review):** add a `[counters] culled_cells=… culled_runs=…`
  telemetry line (Inc 3 verification) so the culling win is observable on
  device rather than inferred.
- **CONSIDER (review):** the far-plane ordering (Inc 4 band is clipped at
  far=800; the full horizon needs Inc 5) is deliberate — re-confirm the user
  wants a two-step horizon (distant tier then wider far) rather than a single
  jump to far=1400 in one increment.

## Out of scope
- Custom RSP microcode / RSP-rewrite (the RSP already does the transform).
- Per-frame CPU micro-optimizations (CPU is ~1% busy — no headroom there).
- Textured distant cells (the distant tier is flat-color by design).
- Changing the 30 fps cap to 60 for the whole frame (open question, not a
  commit).
