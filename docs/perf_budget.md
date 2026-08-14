# Render performance budget (30 fps)

Target: **30 fps** → **≤ 33.3 ms / frame** measured on device with the
Inc-7 per-phase profiler (`[profiler] avg frame time …` + per-phase lines,
every 60 frames via USB serial).

## Phase budget (per frame)

| Phase | Budget |
|-------|--------|
| distant (Z-off, culled, coalesced) | ≤ 12 ms |
| high_priority (near, Z-on, visible-culled, coalesced) | ≤ 8 ms |
| update (fixed-step ticks + camera + actors) | ≤ 6 ms |
| present / rest (overlays, swap, idle) | ≤ 7 ms |
| **total** | **≤ 33.3 ms** |

`low_priority` and `particles` are currently no-ops (0 ms). `texture_upload`
is a subset of `high_priority` (the textured draw incl. TMEM sprite uploads).
`streaming` accumulates only on chunk-transition frames (`SetCenter`) — it
should spike there and be ~0 normally; a large per-transition spike is a
hitch to investigate (load-once budget).

## How to measure

1. Build + boot the ROM in an emulator with USB serial output visible
   (Ares CLI launch per `AGENTS.md`, or Mupen64Plus).
2. Let it run ≥ 120 frames; the report prints every 60 frames:
   - `[profiler] avg frame time over 60 frames: X.XXX ms (Y.Y fps)` — the
     whole-frame average.
   - `[render-phases] distant=… low_priority=… high_priority=… streaming=… ms`
     — the REAL per-pass cost (Inc 1 / instrumentation). `distant` is the
     horizon pass; `streaming` is nonzero only on chunk transitions/boot.
   - `[counters] distant_cells=… near_batches=… texture_uploads=… vert_loads=…
     syncs=… distant_batches=… distant_vert_loads=… distant_syncs=…` — the
     near-pass draw counters plus the distant pass's own split (Inc 2 /
     instrumentation).
   - `[distant-cells] n=… top=(ix,iz) runs=… verts=… d2=…` — the number of
     distant cells drawn this frame and the costliest cell by draw units
     (`runs`; the RSP sync driver). `d2` is distance² (Inc 3 / instrumentation).
3. Use the report to judge the passes:
   - `distant_cells` should be ≤ ~15 while looking across the map (was 45).
   - `distant_syncs` should track `distant_cells` (one sync per drawn cell's
     active run/batch count); ~0 when facing the ground.
   - `near_batches` ≈ visible cells × material runs (1–4 cells, not 9).
   - `texture_uploads` ≈ distinct material runs in the visible ring (5–25),
     NOT per-face (~1350).
4. Walk the whole map from `cell_00_00`; confirm no hard hitches, no popping
   at LOD/frustum edges, no Z-fighting, no visible tile loading.

**Ares timing is a proxy, not proof.** Device validation uses Ares only
(user decision; the older "cross-check on Mupen64Plus" guidance is dropped).
Ares runs the ROM at ~0.1 fps under software paraLLEl-RDP + USB serial, so its
absolute ms numbers are NOT a valid timing proxy — treat Ares as the visual
smoke + counters environment, and validate frame-time conclusions on real
hardware before shipping.

**Instrumentation scope:** the `[render-phases]` / `[counters]` distant split /
`[distant-cells]` lines are measurement-only — the draw path is unchanged.
The sum of per-phase ms will NOT equal the whole-frame avg: physics/collision/
actor work runs in `Update` outside the phase markers and shows up only in the
whole-frame number. That gap is expected (not a bug).

## Baseline (pre compressed-LOD)

Recorded before the compressed distant-LOD work (`.plans/compressed-distant-lod/`).
This is the "before" column for the Inc 5 close-out table.

### On-disk distant artifacts (verified, decoded LVL2 headers)

- **45 cells** — 3,807 faces / 14,080 verts, avg 84.6 / 313, max 308.
- `*_distant.lvl` total **391,283 bytes (~8.7 KB/cell avg)**.
- near LVLs total **906,319 bytes (~20 KB/cell avg)** — a 2.32× byte ratio,
  matching the ~2.3× face reduction.
- The docs' "1,015 faces" figure is the **run** count (1,015 adjacent runs /
  303 material-sorted), not the face count. The two quantities are distinct.

### Device counters (user to fill — hold the whole-map view)

| Counter | Facing ground | Facing far map corner (tank) |
|---------|---------------|------------------------------|
| `distant_cells` | | |
| `distant_batches` | | |
| `distant_vert_loads` | | |
| `distant_syncs` | | |
| `[distant-cells] n=` | | |
| `[memory] total/used/free` | | |

### Inc 5 targets (from this baseline)

- Distant resident ≤ ~1/4 of baseline (4-direction form ≤ ~300 KB; 2-direction
  or single-mesh fallback ≤ ~1/4-1/5).
- Whole-map `distant_syncs` ≤ ~30-40/frame.
- Frame ≤ 33.3 ms (30 fps) with distant ≤ 12 ms.

## Tuning knobs

All are compile-time constants, tuned after measuring on device:

- `kCullMargin` (`src/user/gameplay/render/distant_world_renderer.cpp`): distant
  frustum cone margin. 1.15× current; too tight pops horizon cells, too wide
  draws the whole map. Adjust ±0.05 and re-check a 360° turn.
- `kDirectionCloseThreshold` (`distant_world_renderer.cpp`): direction-selection
  close threshold (120 world units ≈ 0.5 × cell size). When the camera is within
  this of a cell center, use the camera's own facing to avoid unstable
  directional selection (Lambert §12).
- **Distant projection (z-split fix):** the distant pass now uses its own
  viewport projection — `near` = just past the resident ring (`1.5 × tile_size`),
  `far` = full map diagonal (`MapFarClipDistance(world_bounds, 1.15)`). The near
  pass restores 20..800. Fog is derived from the distant far plane
  (`far*0.4` → `far*0.9`) because `t3d_fog_set_range` operates in the
  projection's depth space, not world distance. `kFogMaxMinDistance` (4000) is
  high enough to not clamp the fog onset.
- `kMaxRing` (`src/user/gameplay/render/tile_streamer.hpp`): near resident pool
  (9). Can grow now that Inc 5 freed the ~720 KB embedded batch arrays, but
  only if memory report (`[memory] used=`) has headroom; visibility culling
  keeps drawn cells low either way.
- `kEnableTextures` (`tile_streamer.hpp`): textured near pass on/off. Keep ON
  per the project decision; D3 coalescing makes it fast.
- `kPosScale` (near 32) / `kLodScale` (distant 0.25): fixed-point packing;
  don't touch without a precision reason.
- `kVerboseFrameLogging` (`src/user/gameplay/debug_flags.hpp`): false in
  release; flip true for a verbose diagnostics session (per-frame update/tick
  traces + telemetry).
- `kEnableRspqBlocks` (file-local in `lvl_room_renderer.cpp` and
  `textured_room_renderer.cpp`): RSPQ block precompilation gate
  (`.plans/rspq-block-render/`). When true (default), each cell's command
  sequence is captured into a block at Load and Draw is
  matrix-push + `rspq_block_run` + pop — the per-frame CPU command
  construction for ~330 runs/cell-sequence disappears. Flip to false for an
  A/B comparison. Boot log proves the path: `[lvlroom]/[texroom] loaded …
  block=yes` per cell. Memory: the block pool adds roughly 300-450 KB
  (measured via the `[memory]` report; ~12.6k triangle commands across 45
  distant + up to 9 near cells), offset by the ~720 KB freed by the
  compact-batch change (Inc 5 / n64-perf-fixup).
- **RSP sync is REQUIRED with blocks** (Inc 3 / D8): blocks replay
  asynchronously — tiny3d commands DMA matrices/vertices from RDRAM at
  command-execution time, and with blocks the CPU races ~2+ frames ahead of
  the RSP (ring-buffer bound). Without a per-frame `rspq_wait()` (after
  `scene_mgr.Render()` in `rom_main.cpp`) the RSP reads torn single-buffered
  matrices (viewport `_matCameraFP`/`_matProjFP`, per-cell `matrix_fp_`,
  model matrices) → world twitches and splits. Keep the wait; if it ever
  shows as a cost, multi-slot the matrices (see plan D8 follow-up) instead
  of removing the sync.

### Compressed distant-LOD knobs (`.plans/compressed-distant-lod/`)

- `--distant-budget` (bake, default 20): per-cell face budget (hard ceiling).
- `--no-directional` (bake): single 360° mesh instead of 4 silhouettes.
- `kDirectionCloseThreshold` (device): see above.
- `kCullMargin` (device): see above.

### Baseline vs final (compressed-LOD)

| Metric | Baseline (Inc 1) | Final (Inc 5) |
|--------|------------------|---------------|
| Distant on-disk | 391,283 B (`*_distant.lvl`) | ~31 KB (4-dir `.dlod`) |
| Distant faces | 3,807 | 787 (single) / ~1,466 (4-dir) |
| Distant resident | ~540-650 KB | ≤ ~300 KB (4-dir), ≤ ~1/4-1/5 (fallback) |
| Whole-map `distant_syncs` | ~1,015 runs | ≤ ~30-40 |
| Frame | 44 → 33 fps tank | ≤ 33.3 ms (30 fps) |

**Ares timing is a proxy, not proof** — validate frame-time conclusions on the
user's emulator with the serial reports.

## If a phase still dominates

- **distant still > 12 ms**: raise the cull margin reduction (tighter cone) or
  skip cells beyond a distance² threshold in `BuildDistantRenderListCulled`;
  the near ring + fog must cover the gap.
- **high_priority / texture_upload still > 8 ms**: add a global per-cell
  material sort in the NEAR pass only (Z-on → order-safe) for one sprite
  upload per material per cell instead of per run. NOT the distant pass
  (Z-off needs per-cell face order).
- **streaming spike**: keep transitions on a load-once budget; the tile
  streamer already loads outside the draw (`SetCenter` from Update, not the
  render loop). Consider pre-loading the next cell during gameplay.

## CONSIDER (post-budget, if time permits)

- Global near-pass material sort (see above) if uploads still dominate.
- Grow `kMaxRing` trading RAM for fewer stream transitions.
- Per-cell material-run cache to skip re-coalescing on SetCenter re-loads.

See `.plans/n64-perf-fixup/plan.md` for the full fixup DAG and per-increment
verification.
