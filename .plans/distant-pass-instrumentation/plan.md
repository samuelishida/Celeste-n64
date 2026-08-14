# Distant-Pass Cost Instrumentation

> **STATUS: IMPLEMENTED.** All 4 increments done. The plan-review MUST-1
> (phase_average_ms stale with a "huge interval") was resolved by adding a
> `silent_` flag to `FrameProfiler`: the renderer + local profilers keep the
> 60-frame interval (averages stay fresh) but suppress their debugf self-print;
> `rom_main.cpp` is the single `[profiler]`-family report path. See the
> per-increment "done" markers below.

## Context

The Forsaken City map holds ~60 fps facing the ground but drops to ~22–30 ms
(44 → 33 fps) when the camera looks across the whole map. The `[counters]`
report shows `distant_cells` jumping 0 → 14 → 9 at that moment, confirming the
distant pass is the driver — **but its actual cost is invisible in the current
profiler output**. The report always shows every phase at `0.000 ms` even when
`distant_cells=14`.

Two root causes make the distant cost unmeasurable today:

1. **The renderer's per-phase profiler is never driven or reported.**
   `OpenWorldRenderer` already opens/closes phases around distant, low-priority,
   high-priority, and streaming (`open_world_renderer.cpp:96-133`). But that
   `profiler_` is never `BeginFrame`/`EndFrame`'d, and no accessor surfaces its
   `phase_average_ms()`. `rom_main.cpp:53-64` owns a **separate** local
   `n64::FrameProfiler` that only times the whole frame and prints every phase
   as `0.000` because it never calls `BeginPhase`. So the real distant ms is
   measured into the renderer's profiler and thrown away.
2. **The distant pass produces NO draw counters at all.** The shared
   `RenderCounters.syncs`/`vert_loads`/`near_batches` are incremented only by
   the **near** pass: `TileStreamer::SetCounters` threads a counter pointer to
   the near `LvlRoomRenderer`/`TexturedRoomRenderer` instances
   (`tile_streamer.cpp:49-107`), but the distant `LvlRoomRenderer` meshes
   created in `DistantWorldRenderer::Load` never receive a counter pointer
   (`distant_world_renderer.cpp` only calls `SetCameraPosition`, never
   `SetCounters`). So `syncs=56` is purely near-pass; the distant pass's RSP
   sync/vert/batch cost is completely absent from the report. We cannot even
   see how much the distant pass draws, let alone what it costs.

This plan makes the distant-pass cost **visible and attributable**: real
per-phase ms on device, a distant-pass counter set (its own sync/vert/batch
counts), and a per-cell cost breakdown so the expensive cell(s) can be
identified. It is measurement + reporting only; tuning/optimization is
explicitly out of scope (a follow-up plan driven by these numbers, per the
user's stated goal of then chasing the 30 fps target).

## Architectural decisions

- **D1 — Drive and report the renderer's existing per-phase profiler.** The
  phase markers already exist. Add `profiler_.BeginFrame()` to the renderer's
  `BeginFrame()` (called at the top of `Update`, so streaming survives — see
  below) and a per-frame `EndFrame()` (called at the end of `Render`). Expose
  the averages via `OpenWorldRenderer::Profiler()` (already present) and have
  `rom_main.cpp` print them. This reuses proven `FrameProfiler` logic — no new
  timing primitive.
  - Rationale: `FrameProfiler` already accumulates + averages per phase. We
    only need to wire BeginFrame/EndFrame across the full frame span and read
    the averages.
  - Alternatives rejected: adding a whole new timer class (overkill; the
    existing one is correct, just undriven). Timing inside the scene with raw
    `timer_ticks()` (duplicates `FrameProfiler`).
  - **BeginFrame placement (streaming-safe):** `kPhaseStreaming` is emitted
    inside `OpenWorldRenderer::SetCenter`, which is called from `Update`
    (transitions/boot) **before** `GameplayScene::Render`. `FrameProfiler::
    BeginFrame()` resets `phase_accum_ticks_`, so if we call it from Render it
    would wipe the streaming ticks. Therefore `open_world_.BeginFrame()` must
    be called at the **top of `GameplayScene::Update`** (before any
    SetCenter/transition), and `EndFrame()` at the **end of `GameplayScene::
    Render`** — spanning Update + Render so streaming survives. The renderer's
    per-frame arena + counters reset must move to that same Update-time call.
- **D2 — Give the distant pass its own counter set.** The near pass already
  owns the shared `RenderCounters` fields. Add `distant_syncs` /
  `distant_vert_loads` / `distant_batches` to `RenderCounters` and have
  `DistantWorldRenderer::Render` accumulate them from per-cell accessors on
  `LvlRoomRenderer` (`RunCount()` / `BatchCount()` / `VertexCount()`). This
  attributes the distant pass's real draw cost without mutating the near
  pass's shared counters.
  - Rationale: the distant pass does exactly one `t3d_vert_load` + one
    `t3d_tri_sync` per run (or per batch in the fallback), so its sync count is
    the sum of drawn cells' active run/batch count — computable at render time.
    No need to thread a counter pointer into the distant meshes.
  - Alternatives rejected: giving `LvlRoomRenderer` a `bool distant` flag to
    redirect to a second counter pointer (more state, two paths to keep in
    sync); re-running a separate build to measure (no — measurement is runtime).
- **D3 — Per-cell cost summary in `DistantWorldRenderer::Render`.** Record a
  bounded member array of `DistantCellStat { cell_ix, cell_iz, runs, verts,
  distance_sq }` for the drawn cells each frame, exposed via a getter. This
  identifies the single most expensive cell (highest runs) and the distance²
  falloff shape. Storage is a **plain member array sized to the entries cap**
  (64), reset by a count at the top of `Render` — no arena dependency, no
  allocation, no stale data.
  - Rationale: the distant pass already iterates the culled list; writing a
    small struct per drawn cell is negligible.
  - Alternatives rejected: printing per-cell inline every frame (serial spam);
    frame-arena array (adds an alloc-after-reset ordering dependency for no
    benefit over a fixed 64-slot member array).
- **D4 — Report at the existing 60-frame cadence, from one reporting path.**
  The renderer's `FrameProfiler` is constructed with a large/high report
  interval (or EndFrame is told not to self-report) so it does NOT emit its own
  `[profiler]` line; `rom_main.cpp` reads `phase_average_ms()` and prints the
  consolidated per-phase + distant-split + top-cell report every 60 frames
  alongside the existing whole-frame avg. This avoids two competing
  `[profiler] avg frame time` lines.

## Assumptions and answers from code

- Distant pass is already wrapped in `kPhaseDistant` phases —
  code @ `open_world_renderer.cpp:96-98`.
- The renderer's `profiler_` is never BeginFrame/EndFrame'd and not reported —
  code @ `open_world_renderer.cpp` (only `BeginPhase`/`EndPhase`), and
  `rom_main.cpp:78-79` explicitly notes "the renderer's own profiler never
  calls EndFrame."
- `rom_main.cpp` uses a local profiler that reports whole-frame avg but all
  phases `0.000` — code @ `rom_main.cpp:53-64, 76-91`.
- **Only the near pass fills the shared draw counters.** `TileStreamer::
  SetCounters` threads to near `LvlRoomRenderer`/`TexturedRoomRenderer`
  (`tile_streamer.cpp:49-107`); `DistantWorldRenderer` never calls
  `SetCounters` on its meshes (only `SetCameraPosition`) — so distant
  contributes zero today. Code @ `distant_world_renderer.cpp:109-118`.
- Distant draw is one `t3d_vert_load` + one `t3d_tri_sync` per run (or per
  batch in the fallback) — code @ `lvl_room_renderer.cpp:330-345`.
- `DistantWorldRenderer::Render` already iterates the culled list and knows
  `cell_index` per drawn cell — code @ `distant_world_renderer.cpp:156-169`.
- `OpenWorldRenderer::BeginFrame()` currently resets the arena + counters —
  code @ `open_world_renderer.cpp:109-112`; this must move to the top of Update
  (D1 streaming-safe placement).
- `GameplayScene::Update` runs before `Render` each frame and is where
  `SetCenter`/transitions occur — code @ `gameplay_scene.cpp:482,512,686+`.
- `FrameProfiler::phase_average_ms(phase)` is the read accessor; `EndFrame()`
  reports at `report_interval_` and resets — code @ `n64/profiler.hpp:44,51-52`
  and `n64/profiler.cpp:28-71`.
- **`n64/profiler.cpp` is NOT host-safe**: it includes libdragon headers
  (`<debug.h>`, `<timer.h>`) and calls `timer_ticks()`. It is not in the host
  smoke-test link line. Therefore no host test may instantiate/drive the real
  `FrameProfiler`; host tests for Inc 1 are wiring-only (forwarding identity),
  and per-phase ms verification is device-only. Code @ `n64/profiler.cpp:1-4`.
- `rom_main.cpp` already prints `[counters]` (distant_cells/near_batches/…)
  every 60 frames via `gameplay.GetRenderCounters()` — code @ `rom_main.cpp:76-91`.
- `docs/perf_budget.md` documents the 30 fps target, ≤12 ms distant budget, and
  the 60-frame measurement cadence.
- Scope (measurement + report only) — user-unavailable decision; aligns with the
  request's explicit goal ("so we can see exactly what the distant cells cost").

## Risks accepted

- **Per-frame `EndFrame()` overhead**: one `timer_ticks()` delta + a couple of
  adds per frame — negligible. The report is still every 60 frames.
- **Per-cell struct capture cost**: up to ~64 small structs written per frame
  into a fixed member array — trivial.
- **`distant_syncs` accuracy**: assumes distant is one sync per run (or per
  batch in the fallback). The render loop reads `RunCount() > 0` to pick the
  run path else `BatchCount()`, matching `Draw()`'s own gate, so the number is
  exact. A host test asserts this equivalence.
- **`DistantCellStat.verts` is the cell's baked vertex count, not per-frame
  drawn span**: in the run path only ≤`kMaxRunSpan` vertices load per run, so
  `verts` is a cell-size proxy for costliest-cell selection, not per-frame RSP
  load. Documented; fine for the goal.
- **`DistantCellStat.distance_sq` is distance²** (matches
  `DistantRenderItem.distance` = `dx²+dz²`), not euclidean distance. Named
  `distance_sq` to avoid the misleading "distance falloff shape" claim.
- **Measurement-only scope**: the 30 fps fix is NOT in this plan. The user
  asked to instrument first; tuning is a follow-up. This is the accepted
  trade-off, not a gap.
- **No non-regression check in scope**: the plan is additive (new counter
  fields + report lines), so `distant_cells` and the render path are unchanged.
  The Inc-4 cross-cutting check asserts `distant_cells` byte-identical before/
  after to prove the instrumentation didn't alter the draw path.

## Increment DAG

- Inc 1 — Report the renderer's real per-phase ms (M) — depends: none — unblocks: 4
- Inc 2 — Give the distant pass its own counter set (M) — depends: none — unblocks: 3, 4
- Inc 3 — Per-cell distant cost summary (S) — depends: 2 — unblocks: 4
- Inc 4 — Aggregated report + verify + host tests (S) — depends: 1, 2, 3 — unblocks: —

```
Inc1 ──┐
       ├──► Inc4
Inc2 ──┼──► Inc3 ──► Inc4
       │
Inc3 ──┘
```

Inc 1 and Inc 2 are independent (different header regions and different .cpp
files) and can land in parallel. **To avoid merge conflicts, neither Inc 1 nor
Inc 2 edits `rom_main.cpp`** — all report printing is consolidated in Inc 4.
Inc 3 needs Inc 2's `LvlRoomRenderer` accessors (not its counter fields). Inc 4
consumes 1, 2, 3 and does all `rom_main.cpp` report work + the scene forwarding
accessors.

## Increments

### Inc 1 — Report the renderer's real per-phase ms (M)
**Depends on:** none
**Unblocks:** 4
**Done criteria:** On device, the `[render-phases]` report line shows non-zero
`distant` / `high_priority` ms (matching `distant_cells > 0`), instead of all
`0.000`.
**STATUS: DONE.**

#### Files to touch

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: in `BeginFrame()`, call `profiler_.BeginFrame()`. Add
  `void OpenWorldRenderer::EndFrame()` that calls `profiler_.EndFrame()`.
- Function(s):
  - `void BeginFrame()` — add `profiler_.BeginFrame();` at the top (after
    `arena_.Reset()` and the counter reset are unchanged; `BeginFrame` itself
    moves to the top of Update per D1).
  - `void EndFrame()` — `profiler_.EndFrame();`
- Data shapes: none (the profiler already tracks phases + frame time).
- Integration points: `BeginFrame()` moves to the top of `GameplayScene::Update`;
  the new `EndFrame()` is called at the end of `GameplayScene::Render`.
- Error paths: none — `FrameProfiler::EndFrame()` is safe to call every frame.

##### src/user/gameplay/render/open_world_renderer.hpp
- What changes: declare `void EndFrame();`. Note the renderer's `profiler_` is
  constructed with a high `report_interval` (e.g. `FrameProfiler(60*1000)`) so
  it does NOT self-print its own `[profiler]` line; rom_main reads
  `phase_average_ms()` instead.
- Function(s): `void EndFrame();`
- Data shapes: none.
- Integration points: `Profiler()` accessor already exists (line 137) for reading
  `phase_average_ms()`; unchanged.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: (a) move `impl_->open_world_.BeginFrame()` from `Render()` to
  the top of `Update()` (before any SetCenter/transition), and (b) call
  `impl_->open_world_.EndFrame()` at the end of `Render()`.
- Function(s): the `Update` top and the map-pack render branch tail
  (`gameplay_scene.cpp:931` and `:952`).
- Data shapes: none.
- Integration points: `BeginFrame` must precede `SetCenter` (streaming phase);
  `EndFrame` must follow all phases (after `Render(cams)`).
- Error paths: `Update`/`Render` already guard `impl_` null.

##### src/user/gameplay/scene/gameplay_scene.hpp / .cpp
- What changes: expose the renderer profiler's phase averages to rom_main. Add
  `const n64::FrameProfiler& Profiler() const` forwarding to
  `impl_->open_world_.Profiler()`.
- Function(s): `const n64::FrameProfiler& Profiler() const;`
- Data shapes: returns the `FrameProfiler` (already host-safe type).
- Integration points: called by `rom_main.cpp` (Inc 4) to print phase ms.
- Error paths: guard `impl_` null (return a static fallback), mirroring
  `GetRenderCounters()`.

##### src/user/gameplay/render/open_world_renderer.cpp — profiler ctor
- What changes: construct `profiler_` with a large `report_interval_` so
  `EndFrame` does not self-report (rom_main is the single reporting path).
- Function(s): the `profiler_` member init in the ctor.
- Data shapes: none.
- Integration points: `EndFrame()` still accumulates + resets phase averages
  at the interval, but the `debugf` self-print only fires once the (huge)
  interval elapses — effectively never in a normal run.
- Error paths: none.

#### Edge cases
- First 60 frames before `EndFrame` reaches the report interval: phase averages
  are 0 until the first report — expected; the report prints after 60 frames.
- If `impl_` is null (scene not init), `Profiler()` returns a zeroed fallback.
- `BeginPhase`/`EndPhase` must still nest correctly inside `Render`; calling
  `EndFrame()` once per frame after `Render(cams)` is the only new call.
- Streaming: because `BeginFrame()` runs at the top of Update, the streaming
  ticks (emitted in `SetCenter` from Update) are NOT wiped — they accumulate
  across the Update+Render span and appear in the `streaming` phase average.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: a **wiring-only** host test
  (`tests/renderer_profiler_contract.cpp`) asserting the scene's `Profiler()`
  forwards to the same `OpenWorldRenderer` object identity. **Do NOT invoke
  `BeginFrame`/`EndFrame` or read `phase_average_ms` on host** — `n64/
  profiler.cpp` is not host-safe (`timer_ticks`/libdragon). Per-phase ms is
  verified on device only. Wire into `tests/run_host_tests.sh`.
- Done: device `[render-phases]` line shows `distant` ms > 0 when looking
  across the map.

### Inc 2 — Give the distant pass its own counter set (M)
**Depends on:** none
**Unblocks:** 3, 4
**Done criteria:** The `[counters]` report shows `distant_syncs` /
`distant_vert_loads` / `distant_batches` (the distant pass's real draw counts),
separate from the near `syncs` / `vert_loads` / `near_batches`.
**STATUS: DONE.**

#### Files to touch

##### src/user/gameplay/render/open_world_renderer.hpp
- What changes: add three fields to `RenderCounters`:
  `uint32_t distant_syncs = 0; uint32_t distant_vert_loads = 0;
  uint32_t distant_batches = 0;`
- Function(s): none (struct fields).
- Data shapes: three new `uint32_t` on `RenderCounters` (host-safe).
- Integration points: reset in `OpenWorldRenderer::BeginFrame` (`counters_ = {}`
  already zero-inits all fields); filled by `DistantWorldRenderer::Render`.
- Error paths: none.

##### src/user/gameplay/render/lvl_room_renderer.hpp
- What changes: add read accessors `int RunCount() const { return run_count_; }`,
  `int BatchCount() const { return batch_count_; }`, `int VertexCount() const
  { return static_cast<int>(vert_count_); }`.
- Function(s): the three accessors above.
- Data shapes: `int` returns of existing private members.
- Integration points: called by `DistantWorldRenderer::Render` to compute the
  distant counter split.
- Error paths: none (simple getters; `run_count_` may be `-1` when coalescing
  failed — the Render loop must treat `run_count_ > 0` as runs-path else
  `batch_count_`, matching `Draw()`'s `run_count_>0 && runs_ && run_faces_`
  gate).

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: in `Render`, for each drawn cell, accumulate
  `counters_->distant_batches`, `distant_vert_loads`, `distant_syncs` from the
  drawn `LvlRoomRenderer`'s active path (runs if `run_count_ > 0`, else
  batches). Each of the three counters += `runs` (or `batches`) for that cell.
  Guard `counters_` null.
- Function(s): `Render(const CameraDesc& cam)` — the per-cell draw loop.
- Data shapes: none.
- Integration points: reads the accessors from Inc 2's first bullet.
- Error paths: `counters_` null → skip (matches existing `if (counters_)`
  pattern); renderer not loaded → accessors return 0.

#### Edge cases
- Coalescing failed (`run_count_ < 0`): the renderer uses the per-face batch
  path; the split must read `batch_count_` for that cell, not `run_count_`. The
  accessor design keeps both; the Render loop picks the active path using the
  same `run_count_ > 0 && runs_ && run_faces_` condition `Draw()` uses.
- A cell with no renderable mesh: `DistantWorldRenderer::Load` skips it, so it
  never appears in `entries_` — no contribution.
- `distant_cells` already exists and is untouched (still the per-cell draw
  count).

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: a host contract test (`tests/distant_counter_split_contract.cpp`)
  that builds a fake `RenderCounters`, calls the distant list builder + a
  simulated per-cell draw using the accessor math, and asserts
  `distant_syncs == Σ drawn cells' active run/batch count`. Wire into
  `tests/run_host_tests.sh`.
- Done: device report separates distant syncs from near syncs; with the camera
  facing the ground, `distant_syncs ≈ 0`; facing the map, `distant_syncs > 0`.

### Inc 3 — Per-cell distant cost summary (S)
**Depends on:** 2
**Unblocks:** 4
**Done criteria:** A device report lists the drawn distant cells with their
run/sync and baked-vertex counts, identifying the costliest cell.
**STATUS: DONE.**

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.hpp
- What changes: add a `DistantCellStat` struct, a fixed member array sized to
  the entries cap (64), a fill count, and a getter.
- Function(s):
  - `struct DistantCellStat { int cell_ix; int cell_iz; int runs; int verts; float distance_sq; };`
  - `const DistantCellStat* CellStats(int* count) const { if (count) *count = cell_stat_count_; return cell_stats_; }`
- Data shapes: `DistantCellStat` (host-safe), `DistantCellStat cell_stats_[64]`
  member, `int cell_stat_count_ = 0`.
- Integration points: filled in `Render`; read by `rom_main.cpp` (Inc 4).
- Error paths: `count` out-param set to the filled count (≤ 64).

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: in `Render`, at the top set `cell_stat_count_ = 0`. In the per-
  cell draw loop, for each drawn cell, capture a `DistantCellStat`:
  `cell_ix`/`cell_iz` from `entries_[e]`, `runs` from `RunCount()` (or
  `BatchCount()` if the batch path is active), `verts` from `VertexCount()`,
  `distance_sq` from the list item's `distance` (which is `dx²+dz²`). Stop when
  `cell_stat_count_` reaches 64.
- Function(s): `Render(const CameraDesc& cam)`.
- Data shapes: writes the member array + count.
- Integration points: reads the Inc-2 accessors; exposes via `CellStats()`.
- Error paths: none (fixed array, no allocation).

#### Edge cases
- More than 64 drawn cells: impossible (entries_ cap is 64, culled ≤ that); the
  count is clamped to 64.
- No cells drawn (facing ground): `cell_stat_count_ = 0`, report prints n=0.
- The per-cell struct is rebuilt every frame from the member array; a new
  `cell_stat_count_ = 0` at the top of `Render` prevents stale data.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: a host test (`tests/distant_cellstats_contract.cpp`)
  asserting the capture array is bounded (≤64), cell stats match the drawn
  list, and the top-cell-by-runs selection works on a synthetic entries_ table.
  Wire into `tests/run_host_tests.sh`.
- Done: device report identifies the costliest distant cell when looking across
  the map.

### Inc 4 — Aggregated report + verify + host tests (S)
**Depends on:** 1, 2, 3
**Unblocks:** —
**Done criteria:** One coherent report block every 60 frames shows per-phase ms,
the distant-vs-near counter split, and the top distant cells; host suite green;
device walk confirms `distant_cells` is byte-identical to before the
instrumentation (no draw-path change).
**STATUS: DONE.**

#### Files to touch

##### src/user/rom_main.cpp
- What changes: consolidate the report. In the existing 60-frame block
  (`counter_report_counter >= 60`):
  - Keep the whole-frame `[profiler]` avg (local profiler, unchanged).
  - Read `gameplay.Profiler().phase_average_ms(...)` and print a
    `[render-phases]` line (distant/high/low/streaming ms).
  - Extend the `[counters]` line with `distant_syncs`/`distant_vert_loads`/
    `distant_batches`.
  - Read `gameplay.GetDistantCellStats(...)` and print a `[distant-cells]`
    line naming the costliest cell (by runs) + count.
- Function(s): the `counter_report_counter >= 60` block.
- Data shapes: none.
- Integration points: `gameplay.Profiler()` (Inc 1), `gameplay.GetRenderCounters()`
  (existing), `gameplay.GetDistantCellStats()` (Inc 3/4).
- Error paths: all accessors guard `impl_` null (zeroed fallbacks), so the
  report prints zeros pre-init.

##### src/user/gameplay/scene/gameplay_scene.hpp / .cpp
- What changes: add the missing scene forwarder `const DistantCellStat*
  GetDistantCellStats(int* count) const` returning
  `impl_->open_world_` distant cell stats. (`Profiler()` was already added in
  Inc 1 — do NOT re-add it here.)
- Function(s): `const DistantCellStat* GetDistantCellStats(int* count) const;`
- Data shapes: forward `DistantWorldRenderer::CellStats` output.
- Integration points: called by `rom_main.cpp`.
- Error paths: `impl_` null → return zeroed fallback (`count=0`, nullptr).

##### docs/perf_budget.md
- What changes: update "How to measure" to document the new `[render-phases]`
  and `[distant-cells]` report lines and the distant-pass counter set, so future
  tuning reads the right numbers.
- Function(s): documentation only.
- Data shapes: none.
- Integration points: none.
- Error paths: none.

#### Edge cases
- The three report sources must align on the same frame window: they all read
  state at the same point after 60 frames, so they describe the same window.
  Document this invariant in the code comment.
- If any accessor returns a zeroed fallback (pre-init), the report prints zeros —
  acceptable (matches existing `GetRenderCounters` behavior).
- The renderer's own `FrameProfiler` must NOT self-report (Inc 1 sets a huge
  interval); otherwise two `[profiler] avg frame time` lines appear. Verify
  only one `[profiler] avg frame time` line exists per 60 frames.

#### Verification
- Run: `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Tests to add/update: all tests from Inc 1–3 wired in; add a report-format
  contract if practical (assert the new `[counters]` string contains
  `distant_syncs`). Wire into `tests/run_host_tests.sh`.
- Done: `[render-phases]` shows non-zero `distant` ms; `[counters]` shows
  `distant_syncs`/`distant_vert_loads`/`distant_batches`; `[distant-cells]`
  lists the costliest cells; host suite green; one `[profiler] avg` line per
  60 frames.

## Cross-cutting verification

After Inc 4, on device (Ares CLI per `AGENTS.md`):
1. Boot; run ≥ 120 frames; read the consolidated report every 60 frames.
2. Face the ground → confirm `distant_cells`/`distant_syncs` ≈ 0 and `distant`
   ms ≈ 0.
3. Turn the camera to the whole map → confirm `distant_cells` jumps (0 → 14/9),
   `distant` ms rises, `distant_syncs` > 0, and the `[distant-cells]` line names
   the costliest cell.
4. The sum of per-phase ms should approximate the whole-frame avg (within
   phase-nesting + RDP idle tolerance). If the phases don't sum to the total,
   investigate work outside the phase markers (e.g. `t3d_frame_start`, the
   skybox, or camera/actor update).
5. **Non-regression:** confirm `distant_cells` and the render output are
   unchanged from before the instrumentation (the change is additive — new
   counters + report lines only). No draw-path code changed.
6. Walk the map; confirm no new hitches and that the report stays legible.

## Standards / common-mistakes referenced
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: none (no new
  `rom:/` paths; measurement only).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: none (no bake
  or geometry changes).
- `.agents/map-creation.md` — applies to: none (no new rooms).

## Open questions (CONSIDER from review)
- Whether the distant pass's per-cell work should ALSO get per-cell **ms** (the
  plan does per-cell **counts** only; a per-cell ms phase would need finer
  timers). If the counts don't explain the 22–30 ms drop, a per-cell ms phase
  is the next step.
- Whether `docs/perf_budget.md`'s distant budget (≤12 ms) needs updating once
  real numbers land — defer to the follow-up tuning plan.
- `DistantCellStat.verts` is baked cell vertex count, not per-frame loaded
  span (≤70/run); the "costliest cell" selection uses `runs` (the true RSP
  sync driver), not `verts`. Keep `verts` as a cell-size signal only.

## Out of scope
- **Optimization/tuning** to actually hit 30 fps — explicitly deferred; this
  plan is measurement + reporting so the follow-up can be data-driven.
- Per-cell **millisecond** timing (only counts + per-pass ms in scope).
- Changing distant culling (`kCullMargin`, `kDistantMaxDist2`, fog) — those are
  the follow-up's tuning knobs (`docs/perf_budget.md` documents them).
- Near-pass visibility culling, ring growth, or material-sort changes (already
  handled in `.plans/distant-pass-perf/`).
