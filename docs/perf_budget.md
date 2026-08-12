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
2. Let it run ≥ 120 frames; the profiler prints every 60:
   `[profiler] avg frame time over 60 frames: X.XXX ms (Y.Y fps)` + per-phase.
3. Also watch the draw counters (extend the profiler report or read
   `OpenWorldRenderer::Counters()` in a debug session):
   - `distant_cells` should be ≤ ~15 while looking across the map (was 45).
   - `near_batches` ≈ visible cells × material runs (1–4 cells, not 9).
   - `texture_uploads` ≈ distinct material runs in the visible ring (5–25),
     NOT per-face (~1350).
4. Walk the whole map from `cell_00_00`; confirm no hard hitches, no popping
   at LOD/frustum edges, no Z-fighting, no visible tile loading.

**Ares timing is a proxy, not proof.** Before closing the budget, cross-check
on Mupen64Plus or real hardware — the RSP/RDP timings differ.

## Tuning knobs

All are compile-time constants, tuned after measuring on device:

- `kCullMargin` (`src/user/gameplay/render/distant_world_renderer.cpp`): distant
  frustum cone margin. 1.15× current; too tight pops horizon cells, too wide
  draws the whole map. Adjust ±0.05 and re-check a 360° turn.
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
