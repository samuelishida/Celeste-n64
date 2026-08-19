# Streaming & memory architecture (post streaming-memory-opt)

This doc pins the streaming + memory model after the
`.plans/streaming-memory-opt/` increments (Inc 1, 3, 4; Inc 2 skipped). It is
the "how it works now" reference for the open-world renderer's memory and
streaming behavior on the N64.

## Target hardware: 8 MB (Expansion Pak required)

The ROM targets the N64 with the **Expansion Pak (8 MB RDRAM)**.
`rom_main.cpp` calls `assert_memory_expanded()` at boot, so it fails early
with a clear error screen if the pak is absent. The heap is the full 8 MB
arena (measured `[memory] total` ≈ 5.48 MB heap arena at boot; `used` ≈ 5.39
MB, ~90 KB free). The whole-map interconnected renderer's streaming budget
assumes this headroom — the 4 MB base-RDRAM target was dropped.

## Frame model: RSP-bound

The frame is **RSP-bound**, not CPU-bound: `rom_main.cpp` calls `rspq_wait()`
after `Render()` (required with RSPQ blocks — the CPU would otherwise race 2+
frames ahead of the RSP and read torn single-buffered matrices). Consequence:
**frame-time gains come from reducing RSP command count or RDRAM/TMEM DMA
traffic**, not from CPU-side refactors. This is why the streaming increments
target RSPQ blocks (Inc 3) and TMEM sprite uploads (Inc 4) rather than CPU
loops.

## Two-pass z-split renderer

`OpenWorldRenderer::Render` computes the near-draw set **once**
(`CollectNearDrawSet` over the resident ring via `CellAabbInNearCone`), passes
it to the distant pass as a skip set (disjoint — no double-draw), then draws:

1. **Distant pass** (`DistantWorldRenderer`, Z-off, distant projection
   near≈508u ring-edge / far=map diagonal, 4-directional decimated DLOD
   silhouettes, ONE shared camera-relative matrix pushed once/frame, per-cell
   `DrawRunsDirect` — no RSPQ blocks).
2. **Low-priority** (no-op stub).
3. **Near pass** (`TileStreamer::DrawHighPriority`, Z-on, near projection
   20–800u, per-cell `TexturedRoomRenderer::Draw` with per-material TMEM sprite
   uploads).

## Incremental near ring (Inc 1)

`TileStreamer::SetCenter` no longer rebuilds the whole 9-cell resident ring on
every cell change. It computes a **ring diff** (`ResolveRingDiff`) between the
old and new center and only loads/evicts the cells that actually changed. A
straight-line move across one cell boundary loads 1–2 cells instead of a 9×
reload spike — the streaming hitch is gone. The boot center is always `out[0]`
of `ResolveDistanceRing`.

## Distant per-cell memory (Inc 3)

Distant cells allocate **zero RSPQ blocks**. `LvlRoomRenderer::DrawRunsDirect`
emits the material-sorted runs directly (`vert_load` + `tri_sync` per run)
under the one shared distant matrix, instead of capturing/running a block per
cell. The distant pass is flat-color (no TMEM sprites) and small (~720 faces),
so the ~180 blocks (~300 KB of the RSPQ pool high-water mark) bought little.
The near pass still uses blocks (`TexturedRoomRenderer::Draw`), gated by
`kEnableRspqBlocks` for A/B.

## Global near material grouping (Inc 4)

The near pass draws **per-material → per-cell** instead of per-cell →
per-material, so each TMEM sprite is uploaded **once per material** instead of
once per (material, cell). `TexturedRoomRenderer` derives a `MaterialGroup`
list (`{material_id, first_run, run_count}`) at `Load`; `TileStreamer::
DrawHighPriority` builds a per-frame `NearMaterialTriple` list (static 288-entry
buffer, 2.3 KB), sorts it by material (stable), then per material: `UploadMaterial`
once, then replays each cell's `DrawMaterialRun` under that cell's matrix.

- **Direct emission, not per-(cell, material) blocks:** the plan's prescribed
  block-per-(cell, material) mechanism was replaced by direct emission (the
  plan's documented `kEnableRspqBlocks = false` fallback), gated by
  `kEnableGlobalMaterialGrouping = true`. Rationale: the frame is RSP-bound, so
  blocks and direct emit have identical RSP cost; direct emission allocates
  **zero** near-pass RSPQ blocks (lower pool high-water, consistent with Inc 3).
- **Counter semantics:** `texture_uploads` is now per-material (was per-run);
  `near_batches` / `vert_loads` / `syncs` unchanged (one per run). Matrix
  pushes rise from per-cell to per-(cell, material) — a minor RSP cost.
- **Safety:** the near pass is fully opaque (no transparency), so reordering
  draws globally by material is safe — the depth buffer resolves order, not
  submission order. If a transparent near-pass material is ever added, this
  must be revisited.

## Measured results (map center, Ares)

| Metric | Baseline | After |
|--------|----------|-------|
| Frame time (light dir) | ~39.4–40.8 ms | ~33.1–33.4 ms (30 fps cap) |
| `texture_uploads` | 63 | 5 |
| `near_batches`/`vert_loads`/`syncs` | 132 | 132 (unchanged) |

See `docs/perf_budget.md` for the full before/after table and the memory note
(expanded-memory heap growth is pre-existing, not a regression).

## Key constants

- `kMaxRing = 9` (near resident pool), `kDistantStreamRadius = 6` (distant
  Chebyshev radius), `kLodScale = 0.25f`, `kDefaultPosScale = 32.0f`,
  `kMaxRunSpan = 70` (RSP vertex-load cap), `kEnableRspqBlocks = true`
  (file-local in `lvl_room_renderer.cpp` and `textured_room_renderer.cpp`),
  `kEnableTextures = true` (`tile_streamer.hpp`),
  `kEnableGlobalMaterialGrouping = true` (`textured_room_renderer.hpp`),
  `MaterialCatalog::kMaxMaterials = 32`, `DistantLodEntry::kMaxDirMeshes = 4`,
  `kMaxBatches = 1024`.
