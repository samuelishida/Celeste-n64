# Memory budget (memory-opt Inc 5)

Single source of truth for the ROM's memory budget. Every number cites a
device capture (Ares USB-serial telemetry). The `[memory]` report fires every
3600 frames (~2 min at 30 fps); the `[budget]` warn fires with it when a
component exceeds its cap (debug-only — never a release hard-fail).

## Target hardware

N64 with the **Expansion Pak (8 MB RDRAM)** — `assert_memory_expanded()` at
boot. The heap is the newlib arena (grows via sbrk up to the pak limit);
`malloc_uncached` (memalign) draws from the SAME arena. The 4 MB base-RDRAM
target was dropped.

## Arena figures (authoritative captures)

| Capture | Arena total | Used | Free | Source |
|---|---|---|---|---|
| Inc 1 (pre-pooling) | 2,921,672 (2.79 MiB) | 2,899,576 | 22,096 | `build/baseline-inc1-20260907-002133.txt` |
| Inc 4 (post-pooling) | 5,858,072 (5.59 MiB) | 5,822,032 | 36,040 | `build/raw-baseline-inc4j-20260907-123146.txt.log` |
| Weighted player skin (2026-10-08) | 6,403,784 (6.11 MiB) | 6,369,896 | 33,888 | `build/baseline-weighted-skin-memory-retry-20261008-131312.txt` |

The weighted-skin capture is the newest stationary Ares sample and supersedes
older arena figures for this ROM. A historical static-player capture before
weighted runtime (`build/baseline-static-player-20260924-220758.txt`) measured
6,138,472 / 6,101,104 / 37,368 bytes; the newer sample is +265,312 arena and
+268,792 used bytes, but this is not a controlled before/after build pair.
The component buckets stayed unchanged and do not isolate player-skin
allocations. `mallinfo` free bytes describe current arena reserve; `sbrk` may
grow the arena, so 33,888 bytes is not total remaining Expansion Pak capacity.
Full RDRAM accounting must also include framebuffers, stack, RSP allocations,
and all other non-arena memory before claiming a total headroom figure.

For the Inc 4 ROM, the arena was ~2.9 MB higher than Inc 1 because renderer
pools preallocated per-cell objects and buffers at boot. Cell transitions
then allocated nothing from the heap; `used` stayed flat during that walk.
That ROM's rough RDRAM estimate (5.86 MB arena + 0.45 MB framebuffers + stack
≈ 6.4 MB) is historical, not a current weighted-player headroom claim.

## Component caps (the `[budget]` registry in rom_main.cpp)

| Bucket | Cap | Inc 4 device value | Notes |
|---|---|---|---|
| `frame_arena` | 64 KB | 0 | Per-frame transient arena (static storage, outside mallinfo) |
| `staging` | 128 KB | 125,867 | Reserved uncached staging pool = MaxLvlSize + margin |
| `res_render` | 4 KB | 1,768 | Live resident renderer OBJECT bytes (13-resident ring) |
| `coll_mesh` | 700 KB | 634,168 | Global CMSH file buffer + dequantized verts (boot-time, permanent) |
| `mappack` | 500 KB | 445,112 | MapSpecV2 fixed footprint including 512 MPP4 hazard slots (46 cells) |
| `pool_highwater` | 14 | 13* | Renderer-pool high-water (bounded resident capacity + 1) |
| `arena_peak` | 64 KB | 0 | Frame-arena peak since boot |

Historical Inc 4 sample had 36,040 / 5,858,072 bytes free (≈0.6%). Current
weighted-skin sample has 33,888 / 6,403,784 bytes free (≈0.53%). The arena can
still grow via `sbrk`; these ratios are allocator-reserve indicators, not the
8 MB pak's total remaining capacity. Full current RDRAM accounting remains
open.

*The 2026-10-08 weighted-skin capture predates the memory-bounded streaming
follow-up; its `pool_highwater=13` is historical, not post-change evidence.
Current source caps residency at 13 cells (all 9 drawn cells plus 4
forward-ranked preloads), and emits `[streamer-memory]` at boot with exact
textured/flat pool storage. Textured builds no longer reserve the inactive
flat pool. Re-capture at map center before updating arena/RDRAM figures or
claiming the OOM is resolved.

## Revert knobs

- **Compression (Inc 2):** `kEnableCompressedCells` in
  `src/user/gameplay/debug_flags.hpp` (N64-gated true). Flip off + re-bake
  from the uncompressed staging artifacts (`build/bake-fc-1200/staging` stays
  forever uncompressed) to revert to plain fopen loads.
- **Pooling (Inc 4):** the pools are members of `TileStreamer` (PIMPL
  `RendererPools` in `tile_streamer.cpp`). Removing them reverts to per-cell
  `new`/`malloc` (the Inc 1 arena figures).
- **Budget warn:** remove the `BudgetTracker` registration in `rom_main.cpp`
  to silence `[budget]` (it is debug-only by design).

## Warm-boot note

The `[budget]` warn may be skipped on warm reset by design (like intros) —
the report cadence is frame-count based, so a warm boot that resets the
counter may not reach 3600 frames before the session ends.

## See also

- `docs/streaming_memory.md` — streaming + frame model (links here as the
  memory authority).
- `.plans/memory-opt/plan.md` — the increment DAG that produced these numbers.
