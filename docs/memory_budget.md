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

The Inc 4 arena is ~2.9 MB higher because the renderer pools (memory-opt
Inc 4) preallocate the per-cell renderer objects + their per-cell buffers at
boot. In exchange, cell transitions allocate NOTHING from the heap — `used`
is flat across a walk (the pools are fixed; the transient asset-load buffers
are malloc'd + freed and reuse the same blocks). Total RDRAM ≈ 5.86 MB arena
+ 0.45 MB framebuffers (320×240×16bpp×3) + stack ≈ 6.4 MB — ~1.6 MB headroom
on the 8 MB pak.

## Component caps (the `[budget]` registry in rom_main.cpp)

| Bucket | Cap | Inc 4 device value | Notes |
|---|---|---|---|
| `frame_arena` | 64 KB | 0 | Per-frame transient arena (static storage, outside mallinfo) |
| `staging` | 128 KB | 125,867 | Reserved uncached staging pool = MaxLvlSize + margin |
| `res_render` | 4 KB | 1,768 | Live resident renderer OBJECT bytes (13-resident ring) |
| `coll_mesh` | 700 KB | 634,168 | Global CMSH file buffer + dequantized verts (boot-time, permanent) |
| `mappack` | 500 KB | 422,580 | MapSpecV2 struct footprint (45 cells) |
| `pool_highwater` | 26 | 13 | Renderer-pool high-water (capacity = kMaxResident + 1) |
| `arena_peak` | 64 KB | 0 | Frame-arena peak since boot |

Headroom target: > 1 % of the arena free at the stationary sample point
(Inc 4: 36,040 / 5,858,072 ≈ 0.6 % — the arena can still grow via sbrk; the
binding constraint is the 8 MB pak, not the arena).

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
