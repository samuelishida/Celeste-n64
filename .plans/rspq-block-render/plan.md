# RSPQ Block Precompilation (render CPU diet)

## Context

The Forsaken City two-pass renderer (`.plans/n64-open-world-renderer/`,
`.plans/n64-perf-fixup/`, `.plans/z-split-horizon-fix/`,
`.plans/render-perf-optimization/`, `.plans/distant-pass-instrumentation/`,
`.plans/distant-pass-perf/`) is fully implemented and instrumented, but the
30 fps budget has never been closed on device. The dominant remaining
structural cost is **per-frame CPU command construction**: `LvlRoomRenderer`
and `TexturedRoomRenderer` re-emit the full RDPQ/t3d command sequence for
every material run every frame (~330 runs/frame: 9 near cells × ~20 runs +
~15-20 distant cells × ~7 runs), each run costing several `rdpq_*`/`t3d_*`
calls (combiner, drawflags, prim color, sprite upload, vert load, per-face tri
fans, tri sync).

`arch.md` §22 calls for geometry/material combinations to be precompiled into
RSPQ blocks. The repo already proves the pattern: `model.cpp` builds a
`rspq_block_t*` at load (`rspq_block_begin` → `t3d_model_draw` →
`rspq_block_end`) and plays it back per frame
(`t3d_matrix_push` → `rspq_block_run` → `t3d_matrix_pop`). The room renderers
never got the same treatment — the n64-open-world-renderer plan "reserved the
data shapes" but the blocks were never built.

**User decisions:** direction = RSPQ block precompilation first; device
validation uses **Ares only** (override the plans' "cross-check on
Mupen64Plus" guidance).

## Architectural decisions

- **D1 — One block per cell, captured at Load.** After the coalesced runs (or
  fallback batches) are built, wrap the entire run loop in
  `rspq_block_begin`/`rspq_block_end`. Draw becomes
  `t3d_matrix_push(matrix_fp_)` → `rspq_block_run(block_)` →
  `t3d_matrix_pop(1)` — O(1) per cell regardless of run count. Rationale:
  everything inside a run (combiner, drawflags, prim color, sprite upload,
  vert load, tri fans, sync) is static after Load; the only per-frame state is
  the camera-relative model matrix, which stays **outside** the block (the
  RSP applies the matrix stored in DMEM at execution time — same mechanism
  `model.cpp` relies on). Alternatives rejected: per-run blocks (54 cells →
  ~330 blocks; more block-run overhead, no benefit), including the matrix in
  the block (changes every frame — impossible).
- **D2 — Counters become precomputed per-cell sums.** The per-run counter
  increments (near_batches, texture_uploads, vert_loads, syncs) move to
  Load-time sums (`counted_batches_` etc.) computed with the exact same
  predicates the emit loops use; the block-path Draw adds them to
  `counters_` in O(1). Each cell's Draw runs at most once per frame, so the
  per-frame totals are identical to today. Rationale: the counters must keep
  reporting per-frame draw cost; iterating the run list on CPU just to count
  would keep most of the per-frame CPU cost the blocks are meant to remove.
- **D3 — Emitters are shared between block build and legacy fallback.** The
  run/batch emit bodies are factored into `EmitRunCommands(r, counters)` /
  `EmitBatchCommands(b, counters)` (counters arg null during block build).
  The existing per-frame loops remain as the fallback when `block_` is null,
  so the two paths cannot drift. `rspq_block_begin` asserts on OOM (no
  graceful failure), so the fallback is defensive, not expected.
- **D4 — Both renderers get the same treatment.** `LvlRoomRenderer` (distant
  pass + near flat fallback + legacy single-room path) and
  `TexturedRoomRenderer` (near pass) each build one block per loaded cell.
  The textured block resolves each run's sprite via the catalog at Load
  (same staleness semantics as today's draw-time resolution — the catalog is
  fixed after `SetCenter`).
- **D5 — Free discipline.** `FreeBlock()` (rspq_block_free + null) runs in
  `Free()` and at the top of `Load()` (free-before-rebuild, matching the
  batch-array rule). Frees happen in `SetCenter`/destructor, the same places
  the existing code already frees vertex buffers that the RSP may still
  reference in the queue — blocks are no worse than the status quo
  (documented caveat, same as `model.cpp`).

Alternatives rejected:
- **Keep per-frame emission** (status quo): the whole point — ~330 run-level
  command sequences/frame is the remaining structural CPU cost.
- **t3dmodel / gltf_to_t3d conversion**: out of scope; the LVL path is the
  validated artifact pipeline.

## Assumptions and answers from code

- `model.cpp:30-32, 63-68` — the established block pattern (Load: begin/draw/
  end; Draw: push/run/pop). Verified.
- `rspq.h` — `rspq_block_begin`/`end`/`run`/`free`; `begin` mallocs the first
  64-word chunk and **asserts on OOM**; chunks grow by doubling to 8192 words
  (`rspq_constants.h:34-35`); `free` is safe only after the RSP processed the
  block — our frees occur in `SetCenter`/destructor, matching the existing
  vertex-buffer free timing. Verified.
- Both renderer headers already include `<t3d/t3dmodel.h>` (which pulls in
  `rspq.h`), so `rspq_block_t*` members need no new includes. Verified
  (`lvl_room_renderer.hpp:3`, `textured_room_renderer.hpp:3`, `model.hpp:3`).
- No host test includes the room renderer headers (they are device-only);
  the run/batch data shapes are unchanged → all 25 host tests must stay
  green untouched. Verified (`tests/renderer_memory_contract.cpp` uses a
  host-safe mirror; distant tests use inline list builders).
- Per-frame counters are reset in `OpenWorldRenderer::BeginFrame`
  (`open_world_renderer.cpp:119`) and consumed by the 60-frame report in
  `rom_main.cpp` — semantics unchanged by D2.
- `DistantWorldRenderer::Render` computes its own `distant_*` counters from
  `RunCount()`/`BatchCount()` accessors — independent of the renderer
  internal counters, untouched.
- Block memory estimate: 8,766 near + 3,807 distant triangles ≈ 12.6k
  `t3d_tri_draw` commands × 16 B ≈ ~200 KB of command words + per-run
  overhead + block chunk growth waste ≈ ~300-450 KB total, offset by the
  ~720 KB freed in Inc 5 (n64-perf-fixup). Verified against the baked LVL
  counts.

## Risks accepted

- **Block memory** (~300-450 KB heap): measured via the `[memory]` report
  after boot; if it crowds the heap, per-cell blocks can be downgraded to
  per-run blocks for the biggest cells only (future work).
- **RSP state leakage**: identical command sequence, just moved in time — the
  combiner/zbuf/fog setup stays outside the blocks (caller-managed), exactly
  as today.
- **Free-while-queued**: blocks are freed at the same points the codebase
  already frees vertex buffers referenced by queued commands; accepted
  status quo risk (D5).
- **Sprite lifetime**: the textured block captures the sprite upload command
  referencing catalog sprites at Load; the catalog outlives the renderers
  (scene-owned). No regression vs. today.

## Increment DAG

- Inc 1 — `LvlRoomRenderer`: emitters + block build + block-path Draw + counted
  sums (M) — depends: none — unblocks: 3
- Inc 2 — `TexturedRoomRenderer`: same, + `counted_texture_uploads_` (M) —
  depends: none — unblocks: 3
- Inc 3 — Verification + docs: host suite, clean ROM build, Ares boot smoke,
  `docs/perf_budget.md` update (S) — depends: 1, 2 — unblocks: —

```
Inc1 ──┐
       ├──► Inc3
Inc2 ──┘
```

Inc 1 and Inc 2 are independent (different files) and land in parallel.

## Increments

### Inc 1 — `LvlRoomRenderer` block precompile (M)

**Done criteria:** every loaded `LvlRoomRenderer` (distant cells, near flat
fallback, legacy room) holds a non-null `block_` after Load; Draw is
push/run/pop when `kEnableRspqBlocks` is on; counters per frame identical to
before; host suite + ROM build green.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.hpp
- Add members: `rspq_block_t* block_ = nullptr;` and counted sums
  `uint32_t counted_batches_ = 0, counted_vert_loads_ = 0, counted_syncs_ = 0;`.
- Declare `void FreeBlock();`, `void EmitRunCommands(int r, RenderCounters*
  counters) const;`, `void EmitBatchCommands(int b, RenderCounters* counters)
  const;` (private; device-only types already available via t3dmodel.h).

##### src/user/gameplay/render/lvl_room_renderer.cpp
- File-local `constexpr bool kEnableRspqBlocks = true;`.
- Factor the run loop body (lvl_room_renderer.cpp:339-374) into
  `EmitRunCommands` and the batch loop body (:375-...) into
  `EmitBatchCommands`; counter increments take the `counters` arg.
- End of `Load()`: compute counted sums with the emit predicates; if
  `kEnableRspqBlocks && (run_count_ > 0 || batch_count_ > 0)`,
  `rspq_block_begin()` → emit active path with nullptr counters →
  `block_ = rspq_block_end()`.
- `Draw()`: `if (kEnableRspqBlocks && block_)` → add counted sums to
  `counters_` (O(1)), `rspq_block_run(block_)`; else the existing loops
  (which now call the emitters with `counters_`).
- `Free()` + top of `Load()`: `FreeBlock()`.

#### Edge cases
- Coalescing failed (`run_count_ <= 0`) → block built from the batch path;
  counted sums from the same path; `IsActiveRunPath()` still gates the
  distant counter split correctly.
- Zero runs AND zero batches → no block (legacy draws nothing; unchanged).
- Re-load (streaming) → free-before-rebuild, no leak.
- Legacy single-room path (`GameplayScene::room_renderer`) gets the block
  treatment automatically (same class).

#### Verification
- `./tests/run_host_tests.sh` (25 green, unchanged); `./compile-rom.sh`
  (clean). Device (Ares): distant/near render unchanged; `[counters]`
  identical magnitudes; `[memory] used=` rises by the block pool.

### Inc 2 — `TexturedRoomRenderer` block precompile (M)

**Done criteria:** near-pass textured cells draw via blocks; `texture_uploads`
per frame unchanged; sprite fallback (no sprite → flat primColor) preserved
inside the block.

#### Files to touch
- `textured_room_renderer.hpp`: `rspq_block_t* block_`, counted sums incl.
  `counted_texture_uploads_`; declare `FreeBlock()`, `EmitRunCommands`,
  `EmitBatchCommands`.
- `textured_room_renderer.cpp`: mirror Inc 1. The emitters keep the sprite
  resolution (`catalog_->MaterialFor(...)`) — sprite upload + TEX_FLAT +
  white primColor when present, flat PRIM*SHADE when absent. Counted
  `texture_uploads_` counts runs/batches whose material resolves to a sprite
  (`catalog_ && catalog_->MaterialFor(id) != nullptr`).

#### Edge cases
- Material with no sprite → flat path captured in the block (same per-run
  decision as today).
- `catalog_` null (`kEnableTextures` path with no catalog) → all runs flat;
  block still built (uniformly flat commands).

#### Verification
- Same as Inc 1; Ares near-pass check shows no holes/artifacts vs. baseline.

### Inc 3 — Verification + docs (S)

- Re-run `./tests/run_host_tests.sh` + `./compile-rom.sh`.
- Ares boot smoke (user runs the Ares CLI launch per AGENTS.md; Ares only —
  user decision): world renders; profiler + counters report; `[memory]`
  shows the block pool; no crash on seam crossing (SetCenter frees + rebuilds
  blocks).
- Update `docs/perf_budget.md`: document the block path under "Tuning knobs"
  (gate + expected memory), and note Ares as the device-validation
  environment (Mupen64Plus cross-check is dropped per user decision).
- Record the resulting `[render-phases]`/`[counters]` observations for the
  follow-up 30 fps tuning pass.

## Cross-cutting verification

- Host suite (25 tests) stays green — data shapes unchanged.
- ROM builds clean, no warnings.
- Ares: identical visuals (flat + textured), identical counters, no
  transition crash, memory report sane.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/og-map-polygon-winding.md` — the emitters preserve
  per-face fan origins (no geometry change; block build replays the exact
  command sequence).
- `.agents/common-mistakes/missing-player-start-init.md` — no boot/transition
  ordering changes.

## Open questions

- If `[memory]` shows the block pool crowding the heap: per-run blocks for
  the largest cells, or a block cache shared across re-loads of the same cell
  (SetCenter re-loads rebuild blocks today).
- Whether the 60-frame `[counters]` report should gain a `blocks_run` counter
  to prove the block path is active on device (cheap, diagnostic-only).

## Out of scope

- Gameplay rule changes; the two-pass architecture; bake changes.
- The 30 fps tuning pass itself (constants `kDistantMaxDist2` / fog ratios /
  `kCullMargin`) — that is the follow-up, driven by the Ares report after
  this plan lands.
- Textured distant pass, RSPQ block reuse (`rspq_block_begin_reuse`),
  placeholder nesting.

### Inc 3 — Verification + docs (S) — UPDATED 2026-08-14

**On-device finding (D8): async-RSP matrix race — FIXED with a per-frame
`rspq_wait()`.**

First Ares visual check of the block build showed the world "twitching like
crazy and splitting the models" (cells drawn with inconsistent transforms).
Root cause, traced through libdragon preview (`rspq.c`, `rsp_queue.inc`,
`rdpq.c`) + tiny3d (`t3d.c`, `matrixStack.rspl`):

- Blocks replay IN-ORDER via `RSPQ_CMD_CALL`/`RET` (pointer-stack in DMEM) —
  the plumbing is correct.
- But tiny3d's matrix/vertex commands carry **RDRAM addresses** and the RSP
  **DMAs the data at command-execution time** (`T3D_CMD_MATRIX_STACK`,
  `T3D_CMD_VERT_LOAD`).
- The game has NO CPU/RSP sync per frame (no `rspq_wait`/`rdpq_sync` in the
  loop; `rdpq_detach_show()` does not wait). With blocks, the CPU emits a
  frame in ~0.1 ms while the RSP takes ms → the CPU races ahead until the
  ring (2×512 words, `RSPQ_DRAM_LOWPRI_BUFFER_SIZE`) is full → the RSP lags
  ~2+ frames.
- The lagged RSP DMAs single-buffered matrices — viewport `_matCameraFP` /
  `_matProjFP` (our viewport is `t3d_viewport_create()` = the DEPRECATED
  non-buffered path!) and per-cell `matrix_fp_` — while the CPU is rewriting
  them for later frames → torn matrices → cells at inconsistent offsets →
  twitching + splitting.
- The legacy path never hit this: CPU-side emission was slow enough that the
  RSP always stayed caught up (the ring never filled).

Fix (this increment): `rspq_wait()` at the end of every frame in
`rom_main.cpp` (after `scene_mgr.Render()`). The wait guarantees the RSP has
finished frame N's commands — including all matrix DMAs — before Update N+1
rewrites the single-buffered matrices. Cost is negligible on real HW (the RSP
work is unavoidable; the CPU was already ring-blocked in Ares), and it is the
same rule tiny3d documents for `t3d_viewport_create_buffered` ("allows you to
change matrices over time without running into race-conditions with the RSP").

Validation: Ares relaunch after rebuild — twitching/splitting gone; blocks
still active (`block=yes`); counters unchanged.

### D8 follow-ups (only if the wait ever shows up as a cost)

- Multi-slot per-cell matrices (`matrix_fp_` ring of 3–4) + buffered viewport
  (`t3d_viewport_create_buffered(6)` — 2 attaches/frame cycle slots twice) to
  allow CPU/RSP overlap again; slot count must exceed max lag =
  ring_words / frame_words (~2.3 frames today), so this is fragile as frames
  shrink — the wait is the robust option.
