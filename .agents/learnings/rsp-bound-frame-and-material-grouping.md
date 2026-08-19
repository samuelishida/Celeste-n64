# RSP-bound frame model & global material grouping

## Context
Streaming & memory optimization for the open-world renderer
(`.plans/streaming-memory-opt/`): incremental near-ring diff (Inc 1), drop
distant RSPQ blocks (Inc 3), global near-pass material grouping (Inc 4),
close-out measurement + docs (Inc 5). Inc 2 (indexed drawing) skipped.

## Hardest decision
Inc 4's RSPQ block barrier: the plan prescribed one RSPQ block per
(cell, material) (~92 small blocks at center) to hoist TMEM sprite uploads
across cells. I chose **direct emission** instead (the plan's documented
`kEnableRspqBlocks = false` fallback), gated by `kEnableGlobalMaterialGrouping`.
Rationale: the frame is **RSP-bound** (`rspq_wait()` after `Render()`), so
blocks and direct emit have *identical* RSP cost — blocks only save CPU time,
which has headroom. Direct emission allocates **zero** near-pass blocks
(lower pool high-water, consistent with Inc 3), and is simpler/lower-risk.

## Alternatives rejected
- Per-(cell, material) RSPQ blocks (plan's mechanism) — rejected: raises
  block count ~92 vs 9 (opposite direction from Inc 3) and buys nothing on an
  RSP-bound frame.
- Inc 2 indexed-drawing re-scope — skipped as infeasible as specified (the 4
  direction variants are face-order permutations of the same triangle set;
  a feasible indexed re-scope needs user approval).

## Least confident
The `[memory]` delta: `used` went UP ~2.2 MB (3.17M → 5.39M) because the ROM
now boots with **expanded memory** (heap `total` 3.2M → 5.48M;
`assert_memory_expanded()` warning at boot). I attributed this to a
pre-existing heap-config change, not a leak (used grew proportionally to
total). Verify this is not a real leak before shipping. Also: map-center
`texture_uploads` = 5 vs projected ~23 — the map uses a small ~5-material
palette, so the absolute number is palette-bound, not a shortfall.

## Reuse
- `src/user/gameplay/render/textured_room_renderer.hpp/.cpp` (MaterialGroup,
  EmitRunState/EmitRunGeometry, UploadMaterial, DrawMaterialRun),
  `tile_streamer.cpp` (DrawHighPriority global material loop), `rom_main.cpp`
  (rspq_wait — the RSP-bound model). Any future renderer perf work should
  assume the frame is RSP-bound and target RSP command count / RDRAM-TMEM DMA
  traffic, not CPU loops.
- When measuring `[memory]` on Ares, remember the ROM boots with expanded
  memory — compare `used` as a fraction of `total`, not absolute bytes.
