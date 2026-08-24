# Streaming Experience: kill the cell-load stutter and the visible pop-in

## Context

The whole-map interconnected renderer (Forsaken City A-side, 45 cells at
`--chunk-size 1200 --scale 0.2`) streams a 3×3 resident ring around the active
cell. Perf headroom is already good (the frame is RSP-bound at ~33 ms/30 fps),
but the *streaming experience* is bad in two distinct ways:

1. **Stutter on cell load.** `TileStreamer::SetCenterImpl`
   (`src/user/gameplay/render/tile_streamer.cpp:77-231`) runs
   `TexturedRoomRenderer::Load()` **synchronously** for every new cell in ONE
   update frame: ROM file IO, vertex transform to `T3DVertPacked`, batch
   build, material sort + coalescing, material-group derivation, and RSPQ
   block precompilation. The ring-diff (streaming-memory-opt Inc 1) already
   cut this to 1–3 cells per transition, but the remaining cells still land
   all at once. Telemetry: the `update` phase AVERAGES ~0.2 ms on a walk —
 but the profiler reports 60-frame averages, so a single transition-frame
 spike is diluted below the average and cannot be confirmed (or denied)
 from on-disk baselines. Inc 1 adds per-phase PEAK tracking to the report so
 the stutter becomes directly measurable (done criteria).
2. **Visible pop-in.** The near ring is exactly 3×3 (`kMaxRing = 9`,
   `tile_streamer.hpp:47`); the moment a cell becomes resident it draws at
   100% opacity, instantly. There is no preload buffer and no fade-in. The
   whole-map fix the original used (distant DLOD horizon) was deleted 8
   commits ago and the `.dlod` files are dead on disk.

The intended outcome: geometry appears gradually and without a frame hitch —
cells are loaded *before* they can be visible (preload ring), the load work is
spread across frames (async DMA + staged parse), and the rare late load is
masked by a brief reveal animation.

## Architectural decisions

- **Decision: keep the single near pass; do NOT resurrect the distant/DLOD
  pass.** The pop+stutter fix lives entirely in the near streamer (load
  scheduling + appearance animation). Rationale: the distant pass was the
  source of the persistent visual corruption that was reverted (commits
  013d57f + 7a6ccb3); reviving it re-imports that bug class. Alternatives
  rejected: distant DLOD horizon (deleted, corrupt), z-split two-pass (reverted).
- **Decision: async cell load via libdragon `dma_read_async` tickets, pumped
  from the per-frame update.** Rationale: the SDK exposes
  `dma_read_async`/`dma_finished`/`dma_wait_finished`/`dma_get_progress`
  (libdragon preview `include/dma.h`); no async DMA is used in the repo yet.
  The N64 has no threads, so a per-frame pump is the only async mechanism.
  Alternatives rejected: threads (not a thing on N64), blocking `dfs_read`
  (the current stutter).
- **Decision: split `Load` into async-read + staged-parse.** The IO (PI bus)
  is the slow part on real hardware; the parse (vertex transform, sort,
  coalesce, RSPQ block precompile) is the CPU part. Both must leave the
  critical frame. The read is issued async; the parse runs at most one cell
  per frame when the DMA completes.
- **Decision: preload ring (5×5 / forward-biased) + brief reveal as fallback
  for late loads.** User-confirmed. The RESIDENT set grows to 5×5 (25 cells,
  Chebyshev-2), forward-biased for LOAD ORDER; the DRAWN set stays exactly the
  3×3 centered on the active cell (today's visibility — no change). Preloaded
  but not-yet-drawn cells are never rendered; they only exist to be READY
  before they enter the 3×3 drawn ring. This matters: drawing extra preloaded
  cells would move the visible boundary outward and re-import the screen-edge
  pop the `kEnableNearCulling` gate exists to avoid (its rationale comment,
  `tile_streamer.hpp:30-41`) — so the drawn set must stay the 3×3. A cell that
  enters the drawn 3×3 before its load finished gets a brief reveal animation
  instead of a hard pop (its scale ramps in around the cell's render origin).
- **Decision: grow the streaming heap budget (~512 KB–1 MB), measure
  `[memory]` after.** User-confirmed. The preload ring needs ~375–500 KB
  resident (25 cells × ~15–20 KB, to be MEASURED — see Inc 2) + staging sized
  from the manifest's largest cell (`MapSpecV2::MaxLvlSize()` ≈ 93 KB on the
  current manifest, plus margin). The heap grows via sbrk (no project cap;
  the n64.ld ASSERT only bounds the ROM image), so this is mostly verifying
  headroom + pre-reserving the staging pool at init.
- **Decision: per-cell reveal = matrix scale animation (epsilon→1 over
  ~0.3–0.5 s), NOT fog and NOT alpha.** Rationale (code-sourced): `rdpq_set_fog_color`
  is **global RDP state** — it fades the whole screen, not individual cells.
  Per-cell alpha would require re-emitting the RSPQ block with a different
  primColor each frame, defeating the RSPQ-block optimization. The camera-
  relative matrix is pushed **outside** the precompiled block
  (`t3d_matrix_push(matrix_fp_)` at `textured_room_renderer.cpp:408`, then
  `rspq_block_run(block_)` inside), so animating the matrix scale composes
  with the block with zero invalidation. This is the natural reveal mechanism.
- **Decision: reveal is gated behind a compile flag (`kEnableCellReveal`) so
  it can be A/B'd on device.** Rationale: the reveal is a visual-tuning knob;
  a flag flip is the rollback path.

## Assumptions and answers from code

- **Stutter root cause:** `SetCenterImpl` runs `Load()` synchronously for all
  new cells in one frame. Source: code @ `tile_streamer.cpp:134-157`.
- **Pop root cause:** 3×3 ring, instant 100% opacity on residency. Source:
  code @ `tile_streamer.hpp:47` + `DrawHighPriority` (no reveal state).
- **Async DMA exists in the SDK:** `dma_read_async` (ticket), `dma_finished`,
  `dma_wait_finished`, `dma_get_progress` (+ `dma_wait`, `dma_wait_started`).
  Source: libdragon preview `include/dma.h` — fetched and grepped this
  review pass (lines ~180/229/248/258), not assumed from memory; there is NO
  ticket cancel/free API (relevant to Inc 1's timeout design).
- **A per-frame pump hook exists:** `OpenWorldRenderer::BeginFrame/EndFrame`
  + per-frame `UpdateCamera`. Source: code @ `open_world_renderer.cpp:46-60`.
- **The frame is RSP-bound:** `rspq_wait()` after `Render()`. Source: code @
  `rom_main.cpp:97` + learning `rsp-bound-frame-and-material-grouping.md`.
- **Fog is global RDP state** (cannot do per-cell fog). Source: code @
  `rdpq.h:931` (`rdpq_set_fog_color` writes the global FOG register).
- **Camera-relative matrix is pushed outside the RSPQ block** (enables
  per-cell scale reveal). Source: code @ `textured_room_renderer.cpp:408-421`.
- **Heap grows via sbrk, no project cap.** Source: `n64.ld` ASSERT only bounds
  the ROM image (`end - __text_start <= 8MB - 64KB - 1KB`); the observed arena
  is 2.29 MB walking, well under the 8 MB RDRAM.
- **DFS files are contiguous in ROM** (so `dfs_open`'s start+len gives a
  contiguous PI range for `dma_read_async`). Source: DFS format (files packed
  sequentially); the manifest already carries `lvl_size`.
- **DragonFS API surface (libdragon preview `include/dragonfs.h`):** the DFS
  header is **`dragonfs.h`, NOT `dfs.h`** (`dfs.h` 404s in the SDK tree —
  confirmed by the include list in a fetched repo tree). `int dfs_open(const
  char* path)` → handle; `dfs_read(buf, size, count, handle)` does a blocking
  read via the handle (falls back to a CPU copy for unaligned data — i.e.
  today's `fopen/fread` path IS a PI DMA + memcpy under the hood); the header
  also documents `dfs_rom_addr`/`IODFS_GET_ROM_BASE` accessors that give the
  PI address of the file's first byte (add the file offset to it) — that is
  the exact value `dma_read_async(staging, pi_addr, size)` needs. Source:
  `dragonfs.h` fetched + grepped.
- **Ticket semantics:** `dma_read_async` returns a ticket that is **always
  non-zero on success** (only 0 = queue unavailable / DMA could not be
  dequeued per the "returns a ticket (always not 0)" doc). The "0 ticket →
  return false" error path in Inc 1 is therefore a real availability check,
  not a race. The PI read is `dma_read_async(staging, pi_addr, size)` after
  `pi_addr = dfs_rom_addr(fd) + file_offset` (0 for a full-file read). The
  `.lvl` files are packed sequentially in the DFS image, so a whole-file read
  is a single contiguous PI range (the manifest `lvl_size` is the file size).
  (The earlier "0x10000000 + offset" phrasing was an off-by-one:
  `dfs_rom_addr` already returns a PI address in the 0x10000000-0x1FFFFFFF
  range, not a file offset to add to 0x10000000.)
- **Actual .lvl sizes on the current manifest:** 45 cells, total ≈ 5.7 MB
  raw, LARGEST `cell_n01_n02.lvl` = 93 KB, median ≈ 15 KB. The plan's original
  "~60 KB, largest cell (raw)" staging assumption is WRONG — a 60 KB staging
  buffer would overflow on `cell_n01_n02.lvl` (93 KB). Staging is therefore
  sized from the manifest's `lvl_size` max (Inc 2) and the fallback constant
  (Inc 1) is set to ≥ the largest observed cell (120 KB).
- **Pop-masking strategy:** preload ring + brief reveal. Source: user-confirmed.
- **Heap budget stance:** grow ~512 KB–1 MB, measure `[memory]`. Source:
  user-confirmed.

## Risks accepted

- **Async DMA + staged parse is the largest change** (Inc 1). Mitigation:
  keep the synchronous `Load` as a fallback (center cell at boot, queue full);
  the parse is spread one cell per frame so the per-frame cost is bounded.
- **Preload ring raises resident memory ~375–500 KB.** Mitigation: Inc 2
  verifies/grows the heap first; `[memory] free` must stay > 0 across a full
  walk. Accept; revisit if the arena fragments.
- **Scale-up reveal may look odd for terrain** (geometry grows from a point).
  Mitigation: brief (0.3–0.5 s), only on late loads (preloaded cells skip it),
  gated behind `kEnableCellReveal` for A/B. Accept; revisit if it reads as a
  bug.
- **DFS→PI address resolution for `dma_read_async`** is a real integration
  point (the .lvl is a `rom:/` DFS path, not a raw PI address). Mitigation:
  `pi_addr = dfs_rom_addr(dfs_open(path))` (verified accessor in
  `dragonfs.h`; returns the file's first byte in PI space directly) then
  `dma_read_async(staging, pi_addr, lvl_size)`. Verify on device.
- **Staging buffer overflow (review MUST-FIX):** the original staging size
  ("~60 KB, largest cell (raw)") is wrong — the largest real cell is 93 KB
  (`cell_n01_n02.lvl`). Fix: size from `MapSpecV2::MaxLvlSize()` + margin
  (Inc 2 pool, ≥ ~120 KB) with a 120 KB fallback constant in Inc 1; refuse
  (fall back to sync `Load`) any file bigger than the pool; host test
  cross-checks `MaxLvlSize()` vs the baked `filesystem/` tree.
- **Toolchain is missing** (`/tmp/n64-bootstrap` was cleared by /tmp cleanup).
  Mitigation: re-run `./setup.sh` before any build. This is a build blocker,
  not a design risk.

## Increment DAG

- Inc 1 — Async + staged cell load (M) — depends on: none — unblocks: 3, 4
- Inc 2 — Streaming heap budget (S) — depends on: none — unblocks: 3
- Inc 3 — Preload ring 5×5/forward-biased (M) — depends on: 1, 2 — unblocks: 4
- Inc 4 — Cell reveal animation (M) — depends on: 1, 3 — unblocks: none

## Increments

### Inc 1 — Async + staged cell load (M)
**Status: done** (2026-08-24)
**Depends on:** none
**Unblocks:** 3, 4
**Done criteria:** a full `kDebugAutoWalk` capture shows no unbounded transition-frame
spike — `[render-phases] streaming=` stays flat and the per-frame `update`
PEAK stays bounded (≤ ~4× the walk baseline, vs today's synchronous
multi-in-one-frame `Load` burst); host tests pass. (Review fix: the profiler
reports 60-frame AVERAGES — a single-frame 1.1 ms spike is diluted below an
average and the old "update stays ~0.2 ms" criterion was unmeasurable with it.
Inc 1 adds `update_max_ms` peak tracking to the report so the done criterion
is verifiable from the capture.)

Split `TexturedRoomRenderer::Load` into an async-read phase (issue
`dma_read_async` into a staging buffer) and a staged-parse phase (parse the
raw bytes into the renderer when the DMA completes). Add a bounded pending-load
queue to `TileStreamer` and a per-frame pump that polls DMA tickets, parses at
most one cell per frame, and promotes a cell to the resident set only when its
renderer is READY. The center cell at boot loads synchronously (one-time cost,
not a per-transition stutter).

#### Files to touch

##### src/user/gameplay/render/textured_room_renderer.hpp
- What changes: add a `PendingLoad` struct (ticket, staging ptr, origin,
  catalog, path) and split `Load` into `LoadAsync` + `ParseStaged`.
- Function(s):
  - `bool LoadAsync(const char* lvl_path, const Vec3& render_origin, const MaterialCatalog* catalog, float pos_scale = kDefaultPosScale);` — resolves the DFS→PI address, issues `dma_read_async`, stores the pending record, returns immediately.
  - `bool ParseStaged();` — the existing `Load` body minus the `fopen`/`fread`; parses the raw bytes from the staging buffer into `verts_`/`batches_`/`runs_`/`material_groups_`/`block_`.
  - Keep the synchronous `Load` as a fallback (center at boot, queue full).
- Data shapes: `PendingLoad { uint64_t ticket; uint8_t* staging; Vec3 origin; const MaterialCatalog* catalog; float pos_scale; const V2RoomSpec* spec; uint32_t file_size; }`.
- Integration points: `TileStreamer::SetCenterImpl` calls `LoadAsync` for non-center cells; `ParseStaged` is called by the pump. New cells land in `set_.spec[]` immediately (so ring diffs and the center-never-evict rule work on them) with a null renderer slot; `DrawHighPriority` naturally skips them because both renderer pointers are null (the same path a load failure takes today — no new draw guard needed). `UpdateCamera` must not require a non-null renderer (it touches only `set_.spec[i]->world_aabb` — verified: it never derefs the renderer).
- **Async vs ring-diff classification (review gap — spec explicitly):**
  PENDING cells are already tracked as resident-set spec slots (null
  renderer), so the ring diff classifies them as **keep** — `ResolveRingDiff`
  works on specs, never renderers (verified: it compares `V2RoomSpec*`), so
  PENDING needs no special case there. `SetCenterImpl` must not re-`LoadAsync`
  a spec that is already pending or READY (dedupe check: spec pointer in
  `pending[]` → skip; renderer not null → skip). Only genuinely new specs get
  enqueued. The `rapid A→B→A→C` host test asserts exactly this no-double-
  enqueue.
- **Side-table correctness (latent bug — review SHOULD-FIX):** the current
  compaction looks up new cells in `diff.load[]` **by load-order index
  position** (`for (li) if (diff.load[li] == s)` then uses
  `loaded_flat[li]`/`loaded_tex[li]`, `tile_streamer.cpp:204-210`). A load
  FAILURE leaves a null hole at that index; if a later kept cell happens to
  probe that index via any off-by-one, it silently picks up a null renderer.
  Inc 1 must make the side tables **spec-pointer-keyed records**
  (`struct LoadRecord { const V2RoomSpec* spec; LvlRoomRenderer* flat;
  TexturedRoomRenderer* tex; }[]`) so lookup is by identity, never by load
  index. With a 25-cell capacity (Inc 3) this bug class is worse, so it is
  fixed in Inc 1, not deferred.
- Error paths: DMA read failure → drop the cell (non-center) or fall back to sync `Load` (center); parse failure → same as today (skip non-center, fatal center).

##### src/user/gameplay/render/tile_streamer.hpp
- What changes: add a bounded pending-load queue + a per-frame pump.
- Function(s):
  - `void PumpAsyncLoads();` — poll DMA tickets, parse one completed cell per frame, promote READY cells into the resident set.
  - `void SetAsyncStagingPool(uint8_t* pool, size_t pool_size);` — attach the shared staging buffer(s).
- Data shapes: `PendingLoadQueue { PendingLoad slots[kMaxPending]; int count; }` with `kMaxPending = 4`.
- Integration points: `OpenWorldRenderer::BeginFrame` calls `PumpAsyncLoads`; `SetCenterImpl` enqueues async loads.
- Error paths: queue full → fall back to sync `Load` for the overflow cell; staging pool exhausted → sync `Load`.

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: call `tile_streamer_->PumpAsyncLoads()` in `BeginFrame` (top of Update, before SetCenter issues new loads — loads issued this frame get pumped next frame, acceptable given the preload ring). BeginFrame already exists per-frame (verified at `open_world_renderer.cpp:46`); no new hook.
- **Parse cost placement (review SHOULD-FIX):** the pump runs in the Update
  phase, so a `ParseStaged` (vertex transform + sort + RSPQ precompile ≈ the
  former transition spike spread over ≤ N frames, N = cells in the
  transition) DOES land in Update. That is accepted and bounded: the stutter
  fix is "no frame carries MORE than a few parses" — verifiable via the new
  `update_max_ms` peak (done criteria), not an average. If the Ares peak is
  still uncomfortable, the pump can move to the idle gap between
  `UpdateCamera` and `Render` in the same frame (no API change — the call
  site is a one-line move).
- Integration points: none new (delegates to the streamer).
- Error paths: none new (pump defers to streamer error handling).

##### src/user/n64/profiler.hpp
- What changes: track and report per-phase MAX as well as the 60-frame
  average.
- Function(s): add `phase_peak_ms(Phase)` (peak per accumulation window);
  `rom_main.cpp`'s report line gains a per-phase `peak=` field (additive —
  existing consumers parse by prefix, the `[profiler]`/phase lines keep the
  average format).
- Data shapes: `phase_peak_ticks_[kPhaseCount]`, reset on each report refresh (same cadence as the average).
- Error paths: none.

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: implement `LoadAsync` (DFS→PI via `dfs_open` + `dfs_rom_addr` in `dragonfs.h` — NOT `dfs.h`; `dma_read_async(staging, pi_addr, file_size)` into an **uncached** staging buffer so the CPU reads RDRAM directly after DMA; the staging bytes come from the pool attached via `SetAsyncStagingPool` — Inc 1 predates Inc 2's pool, so use a single `malloc_uncached(kStagingFallbackBytes)` fallback with `kStagingFallbackBytes = 120 * 1024` (≥ the current manifest's 93 KB largest cell, verified against `filesystem/lvl/forsyken-city/`); Inc 2 swaps the fallback for the manifest-sized pool) and `ParseStaged` (the existing parse body reading from the staging buffer instead of `fread` — note: `TexturedRoomRenderer::Load` currently reads via `fopen`/`fread`, so `ParseStaged` is the parse body after `fclose`, re-pointing at staging offsets).
- Function(s): `LoadAsync`, `ParseStaged`.
- Data shapes: staging buffer is `malloc_uncached`, sized to the largest cell in the manifest (≥ 93 KB), reused across loads; one slot per in-flight read.
- Integration points: `Load` (sync) delegates to `ParseStaged` after its blocking read; `LoadAsync` + `ParseStaged` are the async path.
- Error paths: `dfs_open` fails → return false; `dma_read_async` returns 0 (queue unavailable — tickets are "always not 0" on success) → return false; `file_size > staging_size` → return false (caller falls back to sync `Load`, never overflows); parse fails → return false (same as today).
- **Staging race (CONSIDER from review):** Inc 1 uses ONE staging slot with
  `kMaxPending = 4` — a second `LoadAsync` must NOT issue while a slot's DMA
  is still in flight. Mitigation: a single `staging_busy_` flag; when busy,
  `SetCenterImpl` issues the overflow cell synchronously (the existing `Load`)
  or defers to the next pump tick. The async queue is only a convenience for
  the 1–3 cells per transition; correctness does not depend on parallel DMA.

#### Edge cases
- Center cell at boot must be READY before the first frame draws (sync load —
  boot only takes the sync path).
- **Transition center must also be READY before it draws (review SHOULD-FIX
  — the original draft covered boot only).** `SetCenter` fires on every
  transition (`gameplay_scene.cpp:636`). The new center loads async like
  everyone else, BUT the drawn 3×3 (Inc 3) always includes it, so a
  not-yet-ready center would be a 240-world-unit hole around the player.
  Mitigation: `SetCenterImpl` checks `textured_renderers_[i]` for the center
  slot — if null it issues a **synchronous** `Load` for the center in the
  same frame (the current behavior, bounded: one cell, and with the 5×5
  preload ring the center is almost always already resident, making the sync
  path a rare fallback). Teleports (`kDebugTeleportToMapCenter`) go through
  the same code and inherit the guarantee.
- A cell that becomes visible while still PENDING is not drawn (gap) — the preload ring (Inc 3) + reveal (Inc 4) handle this; Inc 1 alone accepts a rare gap.
- Rapid A→B→A→C transitions must not double-parse or leak a pending record
  (reuse the snapshot-before-rewrite pattern from `tile-streamer-compaction.md`).
- **DMA tickets CANNOT be cancelled** (libdragon `dma.h` exposes
  `dma_finished`/`dma_get_progress`/`dma_wait` — no cancel/free API).
  If the PI bus is busy or a ticket never completes in reasonable time:
  a deferred-parse model — each `PendingLoad` records its issue frame; the
  pump treats a ticket as failed after a `kPendingTimeoutFrames` (~180 =
  6 s) timeout and falls back to sync `Load`. A cell that leaves the
  resident pool while a ticket is pending: let the DMA finish (RDRAM lands
  harmlessly, ≤ 93 KB into the staging pool) and DROPPED the pending record
  on eviction (the pump skips tickets whose cell is no longer resident —
  checks the spec pointer before parsing). No leak: at most N=4 tickets in
  flight, all bounded by the timeout.
- The staging buffer is shared — only one DMA in flight at a time (serialize reads) or use a small pool; never issue a second read into a buffer still being parsed.

#### Verification
- Run: `make test` (host) + `./compile-rom.sh` (after `./setup.sh`).
- Tests to add/update: a host test for the pending-load state machine (Pattern A, header-only — a `PendingLoadQueue` with a mock "is_finished" callback, asserting issue→poll→parse→promote and no double-parse/leak). Keep `tile_streamer_diff_smoke` green.
- Done: `[render-phases] streaming=` flat across a `kDebugAutoWalk` capture; `update` ~0.2 ms with no transition spike; `make test` green.

### Inc 2 — Streaming heap budget (S)
**Status: done** (2026-08-24)
**Depends on:** none
**Unblocks:** 3
**Done criteria:** `[memory]` free stays > 0 after reserving the staging pool
and loading the CURRENT 3×3 ring (ring-independent — the 5×5 does not exist
yet; review fix: the original criteria said "with the preload ring resident",
referencing Inc 3's deliverable from an increment that does not depend on it);
the STAGING pool is pre-reserved at init from
`MaxLvlSize()` (one contiguous `malloc_uncached` block, no fragmentation);
the per-cell resident cost is MEASURED (host table) and `kStreamingBudgetBytes`
is set from the measurement; the 5×5 walk check lands in **Inc 3's**
verification (it re-confirms `[memory]` free > 0 with 25 residents live);
`make test` green.

Verify the heap can hold the preload ring (25 cells × measured per-cell cost
— the ~15–20 KB figure is an ASSUMPTION to be replaced by measurement) plus
staging `MaxLvlSize() + margin` (≥ ~120 KB on the current manifest). Pre-reserve
ONLY the staging pool at init (resident renderers come and go with ring diffs
and stay on the normal malloc path — pre-reserving them as one block would be
worse than the churn it avoids). The heap grows via sbrk (no project cap; the
n64.ld ASSERT bounds the ROM image only), so this is mostly measurement + one
reservation.

#### Files to touch

##### src/user/gameplay/world/mappack_loader.hpp
- What changes: expose the pack's largest cell so the staging pool is manifest-sized, never assumed.
- Function(s): `uint32_t MaxLvlSize() const;` on `MapSpecV2` — max over `rooms[i].lvl_size` (host-safe, pure).
- Data shapes: none.
- Integration points: `TileStreamer::ReserveStreamingBudget` uses it to size the staging pool; `LoadAsync` clamps to it.
- Error paths: a caller with `lvl_size > MaxLvlSize()` (stale manifest) is impossible by construction; `LoadAsync` still guards (`file_size > pool` → sync fallback).

##### src/user/gameplay/render/tile_streamer.hpp
- What changes: add the streaming-budget constant + an init-time staging reservation.
- Function(s): `bool ReserveStreamingBudget(uint32_t max_lvl_size);` — allocates ONE `malloc_uncached(max_lvl_size + STAGING_MARGIN)` staging pool at init (returns false on failure). The Inc 1 `kStagingFallbackBytes` constant is replaced by the pool once the spec is attached.
- Data shapes: `kStreamingBudgetBytes` ~512 KB–1 MB documented as: staging `MaxLvlSize() + STAGING_MARGIN` (~120 KB, `STAGING_MARGIN = 32 KB`) + headroom for 25 resident renderers (measured in `tests/renderer_memory_contract.cpp`, see Verification) + allocator slack. `kMaxResident = 25` is added by Inc 3.
- Integration points: `MapRuntime`/`GameplayScene::Init` passes `spec.MaxLvlSize()`; `SetAsyncStagingPool` (Inc 1) receives the pool.
- Error paths: reservation fails → log + the Inc 1 fallback (per-load `malloc_uncached(120 KB)` staging) stays active.

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: call `tile_streamer_->ReserveStreamingBudget(spec.MaxLvlSize())` at first `SetCenter` (the spec is not available in the ctor — verified: the ctor has no spec).
- Function(s): `SetCenter()` gains the reservation (idempotent — only reserves once).
- Integration points: forwards the spec's `MaxLvlSize()`.

##### src/user/rom_main.cpp
- What changes: none required (the `[memory]` report already exists at 3600-frame cadence). Optionally add `largest_free` to the report for fragmentation visibility.
- Function(s): the memory report block.
- Integration points: none.

#### Edge cases
- The arena must not fragment as cells stream in/out — the staging pool is the one big, long-lived, uncached block (pre-reserved); renderers stay on the normal malloc path.
- `[memory] free` must stay > 0 across the whole walk; if it approaches 0, shrink the resident ring in Inc 3 (e.g. a forward-biased 5×3 subset) rather than shrinking staging.
- The measurement table must be captured BEFORE the 5×5 ring ships (it measures one cell's parse footprint, ring-independent) and checked in as a comment on `kStreamingBudgetBytes`.

#### Verification
- Run: `make test` + `./compile-rom.sh` + `tools/capture_baseline.sh madeline_cube_rom.z64 120 heap`.
- Tests to add/update: (a) extend `tests/renderer_memory_contract.cpp` (existing — renderer/heap contract test) to load EVERY `filesystem/lvl/forsyken-city/*.lvl` host-side via `TexturedRoomRenderer::Load` and print a max/mean per-cell mallinfo-peak table (Pattern C, baked fixtures); (b) a host test asserting `MapSpecV2::MaxLvlSize()` equals the largest `.lvl` on disk under the baked tree (Pattern A).
- Done: the measurement table exists; `[memory] free` > 0 with the staging pool live (verified again on the Inc 3 walk); arena stable across `[memory]` reports.

### Inc 3 — Preload ring 5×5/forward-biased (M)
**Status: done** (2026-08-24)
**Depends on:** 1, 2
**Unblocks:** 4
**Done criteria:** on a full `kDebugAutoWalk` capture, a cell enters the
DRAWN 3×3 only while already READY (host + Ares: `streaming=` phase stays
flat, no late-load pops); `[memory] free` stays > 0 within the Inc 2 budget;
`make test` green.

Grow the RESIDENT set from 3×3 (9 cells) to 5×5 (25 cells, Chebyshev-2),
forward-biased for LOAD ORDER. The DRAWN set is UNCHANGED: exactly the 3×3
centered on the active cell (review MUST-FIX — the first draft drew any outer
cell whose AABB intersected the camera cone, which moves the visible boundary
outward and re-imports the screen-edge pop the `kEnableNearCulling` gate
exists to avoid; `tile_streamer.hpp:30-41`). Preloaded-but-not-drawn cells
exist purely to be READY before they enter the 3×3 — they never render. The
ring-diff now diffs the 25-cell resident ring, so a 1-step move keeps 12
overlapping residents instead of 6 (fewer parses per transition), and late
loads happen one whole cell away from the drawn boundary (more reveal runway).

#### Files to touch

##### src/user/gameplay/render/tile_streamer.hpp
- What changes: separate resident capacity from drawn capacity; extend the ring resolver.
- Function(s):
  - `constexpr int kMaxResident = 25;` (5×5) — the resident pool capacity; `kMaxRing` stays 9 (the drawn 3×3, used for the `drawn_mask` and test assertions).
  - `ResolveForwardWedge(..., int max_cheb = 2)` — already parameterized by `max_cheb` (verified: the signature has `int max_cheb = 1`); Inc 3 just passes 2. NO signature change needed.
  - `ResidentSet` fixed arrays (`spec[]`, `last_used[]`, and `ResidentVisibility::visible[]`) grow from `[kMaxRing]` to `[kMaxResident]`; `capacity` stays a field (25).
  - `RingDiffResult` arrays (`keep[]`, `load[]`, `free[]`, `tile_streamer.hpp:293-295`) grow from `[kMaxRing]` to `[kMaxResident]` — a 5×5 diff can move `load_count` up to 25 (a teleport); the 9-slot arrays would overflow (review gap — verified at the source). `load_count` is additionally capped at `kMaxLoadPerTransition = 8` in `SetCenterImpl` (overflow loads defer one frame — the 5×5 overlap means a normal 1-step move only adds 5).
- Data shapes: `renderers_[kMaxResident]` / `textured_renderers_[kMaxResident]` in `TileStreamer`; `ResidentVisibility::visible[kMaxResident]` — the mask's meaning becomes "drawn this frame" (3×3 = cheb≤1), NOT "frustum-visible" (see .cpp).
- **Gate interaction (review gap, stated explicitly):** the drawn mask is written
  UNCONDITIONALLY in `UpdateCamera` and does NOT depend on
  `kEnableNearCulling` (default OFF, `tile_streamer.hpp:41`).
  `kEnableNearCulling` gates ONLY the `CellAabbInNearCone` per-cell cull
  test (a future feature); the 3×3 drawn mask is the always-on rule. No
  flag flip, no behavior change to the gate.
- Impact on host tests (SHOULD-FIX — convention compliance): several Pattern A tests hard-code `kMaxRing` sizes — `tile_streamer_smoke.cpp`, `tile_stream_lru_contract.cpp:40` (asserts `set.capacity == kMaxRing`), `near_visibility_contract.cpp`, `tile_streamer_compaction_smoke.cpp:70`. Splitting the constants means: (a) keep `kMaxRing = 9` as the DRAWN-ring constant so 3×3-only tests keep passing untouched; (b) `tile_stream_lru_contract` must assert `capacity == kMaxResident`; (c) `ResolveForwardWedge` with `out_capacity = kMaxRing` still returns ≤ 9 for the legacy alias — verified: capacity bounds the loop, so `ResolveDistanceRing` behavior is byte-identical.
- Integration points: `SetCenterImpl` resolves the 5×5 ring; `UpdateCamera`/`DrawHighPriority` use the reshaped mask.
- Error paths: a 5×5 ring at a map edge resolves fewer than 25 cells (never fatal — `ResolveForwardWedge` already handles out-of-range neighbors); the resident pool never exceeds `kMaxResident`.

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: `SetCenterImpl` resolves the 5×5 resident ring (`ResolveForwardWedge(..., max_cheb=2)`) and diffs it; ALL fixed `kMaxRing`-sized stack arrays in `SetCenterImpl` (`ring[]`, `loaded_flat[]`, `loaded_tex[]`, the four snapshot arrays — lines 82, 132-133, 174-177) and in the eviction/compaction paths grow to `kMaxResident` (mechanical, but they are the buffer-overflow class the review flagged — `drawn_mask`/snapshots sized 9 would silently truncate 25). **`s_triples[N]` in `DrawHighPriority`** (the material-triple dedup buffer, currently `s_triples[288] = 9 cells × 32 materials`, `tile_streamer.cpp:343`) grows to `kMaxResident * 32 = 800` — with the drawn mask it actually holds ≤ 9 cells (the mask is what caps drawing), but the safety-net `break` (line 351) silently drops geometry if exceeded and the buffer is cheap stack (2.5 KB) — sized for kMaxResident so the invariant is "never" regardless of mask regressions. `UpdateCamera` rewrites the mask: `visible[i] = (chebyshev(spec[i], center) ≤ 1)` for ALL residents — preloaded outer cells get `visible=false` (never drawn), independent of `kEnableNearCulling`; the center is always visible (guard, as today). `CellAabbInNearCone` is NOT used for the drawn set (it stays gated as today for the future cull feature). `DrawHighPriority` is UNCHANGED logic (it already respects `visibility_.visible[i]`).
- Function(s): `SetCenterImpl`, `UpdateCamera` (mask rewrite + array sizes).
- Data shapes: no new fields beyond the grown arrays (the mask reuses `ResidentVisibility::visible`).
- Integration points: `OpenWorldRenderer` call sites unchanged.
- Error paths: compaction must snapshot old resident state before the in-place rewrite (the `tile-streamer-compaction.md` pattern, already in code at lines 174-183) with `kMaxResident`-sized snapshots.

#### Edge cases
- Map-edge centers resolve fewer than 25 residents (never fatal).
- A fast 180° turn into a cell outside even the 5×5 still pops — that is the Inc 4 reveal's job (it masks ANY late load, preloaded or not).
- The center cell is always drawn (mask guard) — verified: `set_.spec[0]` is always the center (`ResolveForwardWedge` puts it at `out[0]`).
- LRU eviction (currently unreachable because the ring never exceeds the pool) must now evict from the OUTER cells first and NEVER the center or a drawn 3×3 cell — `EvictCandidate`'s center-guard already exists (verified: it skips `spec[0]`); extend it to skip cheb≤1 as a safety net for the future.
- If `[memory] free` approaches 0 on the Ares walk, shrink the resident ring to a forward-biased subset (e.g. cheb≤2 front half, ~13 cells) — the drawn 3×3 is never shrunk.

#### Verification
- Run: `make test` + `./compile-rom.sh` + `tools/capture_baseline.sh madeline_cube_rom.z64 120 preload`.
- **Autowalk must actually cross a seam (review fix — verified in code):**
  `kDebugAutoWalk` limits the walk to 100 world units
  (`gameplay_scene.cpp:832`) but a cell is 240 world units (chunk_size 1200 ×
  scale 0.2) — the autowalk might not trigger ANY cell transition, so a
  "no pop across a seam" check would be vacuous. Add
  `kDebugAutoWalkDistance` (default 100.0f, `debug_flags.hpp`, same pattern
  as `kDebugAutoWalk`) and run the preload capture with 260.0 (guarantees ≥1
  boundary crossing from the manifest Start spawn — spawn-relative distance
  asserted in a host test against the manifest so it is not a magic number).
- Tests to add/update: extend `tile_streamer_diff_smoke` (existing) to assert the 5×5 diff (a 1-step move keeps 12 residents vs 6 for 3×3); a new Pattern A host test for the drawn mask (cheb≤1 → drawn; cheb=2 → resident-not-drawn, including a 4-case corner: center/edge/outer/evicted); the autowalk-distance host test above.
- Done: walking across a seam shows no pop for preloaded cells (Ares capture, ≥1 verified crossing); `[memory] free` > 0; **`[counters] near_batches` is UNCHANGED from the 3×3 baseline (~94 walking) — the mask draws exactly 3×3, so Inc 3 must not move this number; a rising near_batches is a regression signal (the drawn set grew past the 3×3)**; `make test` green.

### Inc 4 — Cell reveal animation (M)
**Status: done** (2026-08-24)
**Depends on:** 1, 3
**Unblocks:** none
**Done criteria:** on Ares, a cell that is drawn while still loading animates
in (matrix scale → 1 over ~0.3–0.5 s) instead of hard-popping — including a
cell that enters the drawn 3×3 with NO resident slot at all (a fast 180°
turn, outside the 5×5); preloaded cells (READY before drawn) snap at 100%
with zero animation; `[render-phases] high_priority` unchanged; `make test`
green.

Add per-cell reveal state. The trigger is STATE-BASED, not event-based
(review SHOULD-FIX — "becomes READY while already in the drawn set" misses
the fast-turn case where the cell was never resident and has no READY
event): the streamer tracks, per resident, `reveal_scale` (1.0 = fully
revealed) and a `revealing` flag. At the moment a resident cell first
enters the drawn 3×3 while its renderer slot was still null (or a late
`ParseStaged` lands while it is already drawn), it starts revealing from
`kRevealEpsilon`. While `revealing`, it advances ~0.3–0.5 s toward 1.0.
A cell that is READY+drawn from its first drawn frame (the normal
preloaded case) has `revealing = false` → drawn at scale 1.0, instant.
Gated behind `kEnableCellReveal`; when OFF, all cells draw at 1.0 (today's
behavior, byte-identical path).

Matrix math (review CONSIDER — exact formula, verified against Draw):
`Draw` currently builds `M = B·v + t` with basis rows `B = (X,Y,Z)·ps`
(from `camera_basis()`, `ps = pos_scale`) and translation
`t = (-(X·o), -(Y·o), -(Z·o))·ps` (the per-cell render origin `o` is baked
into `matrix_fp_` at SetCenter). Pivot-scale about the render origin `o`
by `s`: `M'(v) = s·B·v + (1-s)·B·o` — the basis rows scale by `s` and the
translation becomes `(1-s)·B·o`, i.e. `t' = -(1-s)·t` (check: `v=o → B·o`
unchanged; `v=o+d → B·o + s·B·d`, so relative distances scale, the pivot
holds). Implementation is exactly two lines in `Draw`:

    const float s = clamp(reveal_scale, kRevealEpsilon, 1.0f);
    m4::Matrix4 m = m4::fromBasisScaleTranslation(
        X * (ps * s), Y * (ps * s), Z * (ps * s),
        (1.0f - s) * (-X·o * ps),
        (1.0f - s) * (-Y·o * ps),
        (1.0f - s) * (-Z·o * ps));

(s → 1.0 degenerates to today's matrix byte-for-byte; that is the unit
test's assertion.) Vertices are int16 at 1/32-unit UV/vertex quantization,
so the scale is stepped — fine for a ramp; the matrix is float, so the
RSPQ block is untouched. The streamer needs no new API: `DrawHighPriority`
already loops per resident and passes `RevealScale(i)` as an optional
`Draw` parameter (default 1.0f) — no origin plumbing.

#### Files to touch

##### src/user/gameplay/render/textured_room_renderer.hpp
- What changes: `Draw` gains an optional reveal parameter.
- Function(s): `void Draw(const Matrix3D* m3, float reveal_scale = 1.0f);`
- Data shapes: none (stateless — the streamer owns the scale; the renderer
  stays dumb so the reveal flag is trivially A/B-able and no per-renderer
  state drifts from the streamer's bookkeeping).
- Integration points: `SetCameraPosition` is UNCHANGED (the pivot/origin is
  baked into `matrix_fp_` at SetCenter; the scale composes at Draw).
- Error paths: `reveal_scale < kRevealEpsilon` clamps to `kRevealEpsilon`
  (degenerate point); values > 1.0 clamp to 1.0 (defensive — reveals only
  ramp up).

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: `Draw` applies the reveal scale around the baked render
  origin per the matrix formula above (basis rows × s, translation corrected
  so the pivot stays at `o`).
- Function(s): `Draw`.
- Integration points: none new (the basis rows come from the existing
  `camera_basis()`/`matrix_fp_` state; the RSPQ block is untouched — the
  matrix is pushed via `t3d_matrix_push` AFTER the block is compiled, so no
  invalidation per the `rsp-bound-frame-and-material-grouping.md` learning).
- Error paths: none beyond the clamps.

##### src/user/gameplay/render/tile_streamer.hpp
- What changes: per-resident reveal bookkeeping.
- Function(s): `float RevealScale(int i) const { return reveal_scale_[i]; }`.
- Data shapes: `float reveal_scale_[kMaxResident]; bool revealing_[kMaxResident];` (all initialized revealed/1.0). `kRevealDurationSec = 0.4f`, `kRevealEpsilon = 0.03f` (both in `debug_flags.hpp` with the gate).
- Integration points: reset on eviction/compaction (the snapshot-rewrite path zeroes the new slot's reveal state); reset to revealed when a cell leaves the drawn set (cheap: the drawn-mask pass in `UpdateCamera` does it).
- Error paths: overflow impossible (fixed-size, `kMaxResident`).

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: (a) the drawn-mask pass in `UpdateCamera` (Inc 3) starts
  reveals and decays stale ones; (b) `DrawHighPriority` passes
  `RevealScale(i)` into `Draw`; (c) a late `ParseStaged` that lands while
  the cell is already drawn triggers an immediate reveal-start.
- Function(s): `UpdateCamera` (start/stop + age the reveal using the
  dt from the existing `dt` parameter), `PumpAsyncLoads` (late-land
  trigger), `DrawHighPriority` (pass the scale).
- Data shapes: delta-time from the already-threaded `dt` parameter (no new
  clock — the frame rate is locked).
- Integration points: `kEnableCellReveal` short-circuits everything
  (scale stays 1.0, no per-frame decay) so the OFF build is identical to
  today.
- Error paths: a resident slot freed mid-reveal is reset by the
  eviction/compaction path (same touch-point as the spec swap).

##### src/user/gameplay/debug_flags.hpp
- What changes: `inline constexpr bool kEnableCellReveal = true;` + `kRevealDurationSec`, `kRevealEpsilon`.
- Function(s): none.
- Integration points: `tile_streamer.cpp` reads them.

#### Edge cases
- Preloaded cells (READY before any drawn frame) must NOT animate — the
  trigger requires "first drawn frame while renderer slot was null", which
  is false for them.
- The reveal must not add per-frame RSP cost (the frame is RSP-bound) — the
  scale composes into the CPU-built matrix only; the precompiled RSPQ block
  is unchanged (verified: `t3d_matrix_push(matrix_fp_)` happens at Draw,
  after block compilation).
- A revealing cell that leaves the drawn set is reset (no wasted decay,
  no stale scale if it is later evicted and re-loaded — the re-load starts
  fresh).
- Fast 180° turns (cell never resided) still reveal — the slot is created
  by `SetCenterImpl` with null renderer + `revealing=true` at the moment it
  enters the drawn set (not at enqueue; the 5×5 outer ring stays
  `revealing=false` until first drawn).
- The scale is a pivot scale about the render origin — with
  `kRevealEpsilon = 0.03` the cell appears to grow out of its own center;
  A/B on device to confirm it reads as a reveal, not a glitch (duration is
  the tuning knob).
- `kEnableCellReveal = false` → byte-identical draw path (all scales 1.0,
  no decay loop).

#### Verification
- Run: `make test` + `./compile-rom.sh` + `tools/capture_baseline.sh madeline_cube_rom.z64 120 reveal` (plus a gate-OFF build for the A/B).
- Tests to add/update: a host test for the reveal state machine (Pattern A
  — preloaded→instant; enters drawn while null→animate; late-land while
  drawn→animate; leaves drawn→reset; gate off→always 1.0); a numerical
  unit test of the reveal matrix formula (Pattern A — `M` with s=1.0 equals
  today's matrix exactly; with s=0.5 the vertex at `o` maps to `o` (pivot
  fixed) and the vertex at `o + X` maps to `o + X/2`).
- Done: late loads animate in on Ares (no hard pop, including the 180°-turn
  case); preloaded cells instant; `[render-phases] high_priority` unchanged;
  `make test` green.

## Cross-cutting verification

After Inc 4, run a full `kDebugAutoWalk` capture and confirm all three
properties together: (1) `[render-phases] streaming=` flat (no stutter), (2)
no hard pop on preloaded cells (walking across seams), (3) late loads animate
in briefly. Also confirm `[memory] free` stays > 0 across the whole walk and
`[counters] near_batches` stays in the expected walking range (~94, per the
n64-opt-device-verify learning). The elf size delta proves `kEnableCellReveal`
is in/out of the build (per the same learning).

## Standards / common-mistakes referenced

- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — applies to:
  Inc 1 (async DMA targets IO, not CPU loops), Inc 4 (reveal must not add RSP
  cost). The frame is RSP-bound; target RSP command count / DMA traffic.
- `.agents/learnings/tile-streamer-compaction.md` — applies to: Inc 1, Inc 3
  (snapshot the old resident state before any in-place array rewrite; keep
  `tests/tile_streamer_compaction_smoke.cpp` green).
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — applies to: all
  increments (kDebugAutoWalk + `tools/capture_baseline.sh` is the walk
  mechanism; `[memory]` fires every 3600 frames; cite the latest capture, not
  the doc; elf size delta proves a debug flag is in/out).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 1 (the .lvl is
  a `rom:/` DFS path; resolve to a PI address via `dfs_open`/`dfs_rom_addr` in
  `dragonfs.h` before `dma_read_async`).
- `.agents/common-mistakes/heap-arena-and-mallinfo.md` — applies to: Inc 2
  (staging pool is ONE `malloc_uncached` block pre-reserved at init, not
  `malloc_cached` — DMA lands in uncached RDRAM and the CPU reads it directly
  — never a per-frame `malloc`/`free` churn; the ROM already asserts the
  full 8 MB via `assert_memory_expanded`, so there is no 4 MB fallback path;
  budget decisions come from `[memory]` mallinfo telemetry, not doc memory).

## Open questions (resolved by review — 2026-08-24)

### Review pass 1 (orchestrator, plan file on disk)

- **CONSIDER: per-cell resident size is an unverified assumption** —
  resolved by Inc 2's measurement (`renderer_memory_contract.cpp` loads all
  45 cells and prints a mallinfo-peak table before the 5×5 ships).
- **CONSIDER: single staging slot vs in-flight queue** — resolved: one slot
  + `staging_busy_` in Inc 1 (parallel DMA is NOT required — correctness
  does not depend on it; a future increment may revisit if telemetry shows
  parse latency matters).
- **CONSIDER: exact reveal matrix math** — resolved above (Inc 4):
  `basis *= s; t' = -(1-s)·t`, pivot at the render origin; s=1.0 must be
  byte-identical to today's matrix (unit-tested).
- **MUST (review): staging 60 KB < real max cell 93 KB** — fixed (Inc 1
  fallback 120 KB, Inc 2 manifest-sized pool).
- **MUST (review): drawn set would grow beyond the 3×3** — fixed (Inc 3:
  drawn set is exactly the 3×3 centered on the active cell; `CellAabbInNearCone`
  stays out of the drawn path).
- **SHOULD (review): latent index-lookup bug in the load side tables** —
  fixed (Inc 1 makes them spec-pointer-keyed `LoadRecord[]`).
- **SHOULD (review): reveal trigger missed the never-resident 180° turn** —
  fixed (Inc 4: state-based trigger on first-drawn-frame while slot null).
- **SHOULD (review): SDK header is `dragonfs.h`, not `dfs.h`** — fixed
  (Inc 1 uses `dfs_open` + `dfs_rom_addr` from `dragonfs.h`).
- **SHOULD (review): host tests hard-code `kMaxRing`** — fixed (Inc 3 keeps
  `kMaxRing = 9` as the drawn-ring constant; lists every test to update).
- **SHOULD (review): `ResidentSet`/stack arrays sized 9 would truncate 25**
  — fixed (Inc 3 grows all fixed arrays to `kMaxResident`; `ResolveForwardWedge`
  already takes `max_cheb`, no signature change).

### Review pass 2 (subagent context-gatherer, 2026-08-24)

Raw observations from the independent subagent were re-verified against the
code and classified; EVERYTHING is resolved in the plan:

- **SHOULD: Inc 2 done criteria referenced Inc 3's 5×5 ring** (an increment
  that does not depend on it) — fixed: Inc 2 criteria are ring-independent
  (3×3 + staging pool); the 5×5 walk re-check moves to Inc 3's verification.
- **SHOULD: Inc 1 done criterion "update stays ~0.2 ms" was unmeasurable** —
  the profiler reports 60-frame AVERAGES (verified `profiler.hpp` +
  `rom_main.cpp:63`); a single-frame spike is diluted below the average.
  Fixed: per-phase PEAK tracking added to the profiler (new file spec in
  Inc 1); the criterion is now "update PEAK bounded ≤ ~4× baseline".
- **SHOULD: the "~1.1 ms transition spike" was not verifiable in on-disk
  baselines** (same 60-frame-average reason) — fixed: the Context no longer
  claims telemetry confirms the spike; it states the average and the
  measurement gap, and Inc 1 closes the gap.
- **SHOULD: ParseStaged lands in the Update phase — "update flat" was in
  tension with "parse one cell per frame"** — fixed: explicitly accepted as
  bounded (no frame carries more than a few parses; peak-verifiable), with a
  documented one-line move option to the UpdateCamera→Render idle gap.
- **SHOULD: `kEnableNearCulling` gate interaction unspecified** — fixed:
  the drawn mask is written UNCONDITIONALLY and independent of the gate;
  the gate only controls the future `CellAabbInNearCone` cull (stated in
  Inc 3).
- **SHOULD: `near_batches ~94` expectation inconsistent with a larger drawn
  set** — fixed: the subagent's premise (drawn set grows) was already
  corrected in pass 1; Inc 3 now asserts near_batches UNCHANGED as a
  regression canary.
- **SHOULD: reveal reset covered eviction but not "leaves drawn set, stays
  resident"** — was already covered (Inc 4: the drawn-mask pass in
  `UpdateCamera` resets reveals on leave-drawn, plus eviction reset);
  subagent missed it.
- **SHOULD: transition-center readiness was boot-only** — fixed: the new
  center loads synchronously if not already resident (Inc 1 edge case;
  `gameplay_scene.cpp:636` fires SetCenter on every transition).
- **SHOULD: `kDebugAutoWalk` (100 units) may never cross a 240-unit cell
  boundary** — verified in code (`gameplay_scene.cpp:832`); fixed:
  `kDebugAutoWalkDistance` flag (default 100.0), captures run at 260.0
  (≥1 verified crossing, spawn-relative distance host-tested).
- **GAP: `RingDiffResult` keep/load/free arrays sized 9** (verified
  `tile_streamer.hpp:293-295`) — fixed: grown to `kMaxResident` + a
  `kMaxLoadPerTransition = 8` cap with one-frame deferral.
- **GAP: `s_triples[288]` (9×32) safety-net break would silently drop
  geometry** (verified `tile_streamer.cpp:343-351`) — fixed: grown to 800.
- **GAP: async-load vs ring-diff classification underspecified (PENDING
  cells)** — fixed: pending cells are resident spec slots, so the diff
  classifies them as keep (the diff works on specs, verified); SetCenterImpl
  dedupes pending/READY specs before enqueuing.
- **GAP: DMA tickets cannot be cancelled** (verified `dma.h` — no
  cancel/free API) — fixed: deferred-parse timeout model (`kPendingTimeout
  Frames` ≈ 6 s → sync fallback) + pending-record drop on eviction (DMA
  completion lands harmlessly; at most 4 tickets in flight, bounded).
- **Wrong/unverified: per-cell 15–20 KB is the AVERAGE, not the worst case
  (max cell 93 KB)** — was already resolved in pass 1 (Inc 2 MEASURES the
  resident cost host-side; staging sized from the manifest MAX, not the
  mean). The subagent independently re-confirmed the 93 KB max.
- **Unverified: `dma_read_async` API availability** — now VERIFIED this pass
  (fetched + grepped `dragonfs.h`/`dma.h` from the libdragon preview tree);
  the Assumptions section records the fetch as the source.
- **Verified correct by the subagent (kept as-is):** matrix-scale-reveal
  composes with the precompiled block (zero invalidation); heap grows via
  sbrk with no project cap (n64.ld ASSERT bounds the ROM image only, ~2.1–3.1
  MB observed); 45 cells; RSP-bound ~33 ms; 3×3 diff loads 3 cells on a
  1-step move, 5×5 keeps 20 overlap.

No open questions remain (both passes fully resolved in-file).

## Out of scope

- Resurrecting the distant/DLOD horizon pass (deleted; the `.dlod` files stay
  dead on disk).
- The z-split two-pass renderer (reverted).
- Gameplay-feel work (Milestone 1: coyote time, jump buffering, dash tuning) —
  this plan is purely the streaming/render experience.
- New entity types, B-side transitions, scene scripting (README "next build
  step").
- The legacy single-room `.lvl` path (map-pack mode is the target; the legacy
  path is preserved as a fallback but not extended).
