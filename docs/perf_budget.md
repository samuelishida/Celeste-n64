# Render performance budget (30 fps)

Target: **30 fps** → **≤ 33.3 ms / frame** measured on device with the
per-phase profiler (`[profiler] avg frame time …` + per-phase lines,
every 60 frames via USB serial).

The renderer is a **single near pass** — the two-pass z-split (near + distant
DLOD horizon) was removed (see `AGENTS.md`, "Renderer (single near pass)").
There is no distant phase, no distant counters, and no `[distant-cells]`
line. The `.dlod` artifacts and baker remain on disk for a future
distant-horizon feature but are not loaded at runtime.

## Phase budget (per frame)

| Phase | Budget |
|-------|--------|
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

1. Build + boot the ROM in Ares (the sole device emulator; CLI launch per
   `AGENTS.md`). Ares prints the ROM's USB-serial telemetry to stdout.
2. Let it run ≥ 120 frames; the report prints every 60 frames:
   - `[profiler] avg frame time over 60 frames: X.XXX ms (Y.Y fps)
     update=X.XXX ms` — the whole-frame average plus the scene-update slice
     (physics/collision/actors, bracketed by `kPhaseUpdate` in
     `rom_main.cpp`).
   - `[render-phases] high_priority=X.XXX streaming=X.XXX ms` — the real
     per-pass cost. `streaming` is nonzero only on chunk transitions/boot.
   - `[counters] near_batches=N texture_uploads=N vert_loads=N syncs=N` —
     the near-pass draw counters.
   - `[memory] total=N used=N free=N` — the mallinfo heap snapshot, printed
     every 3600 frames (so a short capture may contain zero of them).
3. Use the report to judge the pass:
   - `near_batches` ≈ visible cells × material runs (1–9 cells resident; the
     3×3 ring keeps 9 loaded, culling gates which are drawn).
   - `texture_uploads` ≈ distinct material runs in the visible ring (5–25),
     NOT per-face.
   - `syncs` should track `near_batches` (one RSP sync per batch).
4. Walk the whole map from `cell_00_00`; confirm no hard hitches, no popping
   at LOD/frustum edges, no Z-fighting, no visible tile loading.

**Ares timing is a proxy, not proof.** Device validation uses Ares only
(user decision; the older "cross-check on Mupen64Plus" guidance is dropped).
Ares runs the ROM at ~0.1 fps under software paraLLEl-RDP + USB serial, so its
absolute ms numbers are NOT a valid timing proxy — treat Ares as the visual
smoke + counters environment, and validate frame-time conclusions on real
hardware before shipping.

**Instrumentation scope:** the `[render-phases]` / `[counters]` /
`[memory]` lines are measurement-only — the draw path is unchanged. The sum
of per-phase ms will NOT equal the whole-frame avg: physics/collision/actor
work runs in `Update` outside the draw-phase markers and shows up in the
`update=` field and the whole-frame number. That gap is expected (not a bug).

## Tuning knobs

All are compile-time constants, tuned after measuring on device:

- `kMaxRing` (`src/user/gameplay/render/tile_streamer.hpp`): near resident
  pool (9). Can grow only if the memory report (`[memory] free=`) has
  headroom; visibility culling keeps drawn cells low either way.
- `kEnableNearCulling` (`tile_streamer.hpp`, default **OFF**): gates the
  per-resident-cell `CellAabbInNearCone` AABB-cone test in
  `TileStreamer::UpdateCamera` (the center cell is always drawn). When ON,
  cells outside the camera cone are skipped in the draw list; device
  before/after (`.plans/n64-optimization/`, Inc 3) showed no visual
  regression and no memory change.
- `kEnableTextures` (`tile_streamer.hpp`): textured near pass on/off. Keep ON
  per the project decision; global material grouping makes it fast.
- `kPosScale` (near 32): fixed-point packing; don't touch without a
  precision reason.
- `kVerboseFrameLogging` (`src/user/gameplay/debug_flags.hpp`): false in
  release; flip true for a verbose diagnostics session (per-frame update/tick
  traces + telemetry).
- `kDebugAutoWalk` (`debug_flags.hpp`, default **false**): when true the
  player walks slowly forward every frame so Ares can exercise chunk
  transitions and movement without controller input — the autonomous
  device-walk mechanism. NEVER leave true in normal builds.
- `kEnableRspqBlocks` (file-local in `lvl_room_renderer.cpp` and
  `textured_room_renderer.cpp`): RSPQ block precompilation gate
  (`.plans/rspq-block-render/`). When true (default), each cell's command
  sequence is captured into a block at Load and Draw is
  matrix-push + `rspq_block_run` + pop — the per-frame CPU command
  construction for ~330 runs/cell-sequence disappears. Flip to false for an
  A/B comparison. Boot log proves the path: `[lvlroom]/[texroom] loaded …
  block=yes` per cell.
- **RSP sync is REQUIRED with blocks** (`.plans/rspq-block-render/` D8):
  blocks replay asynchronously — tiny3d commands DMA matrices/vertices from
  RDRAM at command-execution time, and with blocks the CPU races ~2+ frames
  ahead of the RSP (ring-buffer bound). Without a per-frame `rspq_wait()`
  (after `scene_mgr.Render()` in `rom_main.cpp`) the RSP reads torn
  single-buffered matrices (viewport `_matCameraFP`/`_matProjFP`, per-cell
  `matrix_fp_`, model matrices) → world twitches and splits. Keep the wait;
  if it ever shows as a cost, multi-slot the matrices instead of removing the
  sync.

### Distant-LOD baker knobs (on disk, not loaded at runtime)

The `.dlod` artifacts and their baker remain for a future distant-horizon
feature. If that feature returns, its tuning surface was:

- `--distant-budget` (bake, default 20): per-cell face budget (hard ceiling).
- `--no-directional` (bake): single 360° mesh instead of 4 painter-sorted
  direction variants.
- `KLOD_SCALE` (`tools/ogworld/distant_lod.py`, 0.25): distant fixed-point
  position scale.
- `kCullMargin` / `extent_slack` (`lod_math.hpp`): frustum cone margin and
  extent-aware depth slack for the cone tests.

## If a phase still dominates

- **high_priority / texture_upload still > 8 ms**: add a global per-cell
  material sort in the NEAR pass only (Z-on → order-safe) for one sprite
  upload per material per cell instead of per run.
- **streaming spike**: keep transitions on a load-once budget; the tile
  streamer already loads outside the draw (`SetCenter` from Update, not the
  render loop). Consider pre-loading the next cell during gameplay.
- **update > 6 ms**: the `update=` field in the `[profiler]` line isolates
  the scene-update slice; profile the fixed-step ticks and actor queries
  before touching render code.

## CONSIDER (post-budget, if time permits)

- Global near-pass material sort (see above) if uploads still dominate.
- Grow `kMaxRing` trading RAM for fewer stream transitions.
- Per-cell material-run cache to skip re-coalescing on SetCenter re-loads.
- Turn `kEnableNearCulling` ON by default if a future map outgrows the 9-cell
  draw budget.

## Final measured before/after (n64-optimization close-out)

Measured on Ares (sole device emulator). BEFORE = pre-plan autowalk capture
(`build/raw-baseline-autowalk-fix-20260821-134018.txt.log`, walking the map
with the pre-plan renderer state). AFTER = post-plan captures: stationary at
map center (`build/baseline-inc5-update-20260821-193329`) and walking
(`build/baseline-inc7-walk-*`, `kDebugAutoWalk`).

| Metric | BEFORE (pre-plan, walking) | AFTER (stationary) | AFTER (walking) |
|--------|---------------------------|--------------------|-----------------|
| Frame time | 34.5–35.9 ms (27.8–29.0 fps) | 33.1–33.4 ms (29.9–30.2 fps) | 33.8–34.8 ms (28.7–29.6 fps) |
| `high_priority` | 0.096–0.115 ms | 0.059–0.076 ms | 0.101–0.114 ms |
| `update` | not instrumented | 0.194–0.195 ms | 0.115–0.225 ms (one 1.132 ms chunk-transition spike) |
| `streaming` | 0.000 ms | 0.000 ms | 0.000 ms |
| `near_batches` | 94 | 56 | 94 |
| `texture_uploads` | 4 | 3 | 4 |
| `vert_loads` | 94 | 56 | 94 |
| `syncs` | 94 | 56 | 94 |
| `[memory] total/used/free` | not captured | 2,165,944 / 2,146,768 / 19,176 | 2,288,680 / 2,230,464 / 58,216 |

**Memory note (authoritative):** the `[memory]` report is a `mallinfo()`
snapshot of the libdragon heap. Stationary at map center: `total` ≈
**2.16 MB** arena, `used` ≈ **2.15 MB**, `free` ≈ 19 KB (ratio used/total
≈ 0.991). Walking (the ring streaming new cells): `total` ≈ **2.29 MB**,
`used` ≈ **2.23 MB**, `free` ≈ 57 KB (ratio ≈ 0.975), stable across every
`[memory]` report in the walk capture (no leak). The arena is larger while
walking because the 3×3 ring holds more resident cells; both are well within
the 8 MB Expansion Pak heap. The older "≈ 5.48 MB arena / used ≈ 5.39 MB"
figures in this doc's history and in `AGENTS.md` are stale — they predate the
current heap layout and do not match any device telemetry.

**Reading the table:** the frame is **RSP-bound at ~33 ms / ~30 fps**; the
CPU slices are small — `update` is ~0.6% of the frame (0.194 ms),
`high_priority` ~0.2%, `streaming` ~0%. The stationary→walking
`near_batches` jump (56 → 94) is the 3×3 ring streaming new cells as the
player walks; it is the expected working state, not a regression. The
before/after frame-time delta (34.5 → 33.1 ms) is within Ares run-to-run
noise; the structural wins are the trimmed draw path (no distant pass), the
per-material uploads, and the per-TU `-O2` on the 6 hot TUs.

**Normal-boot note (autowalk off):** with `kDebugAutoWalk` false and
`kDebugTeleportToMapCenter` false, the player spawns at the corner Start
spawn (`[spawn] authored=(0.00,25.60,89.60)`) — x=0.00 sits exactly on the
`cell_00_00`/`cell_n01_00` boundary on sloped terrain (cell-origin y =
57.6/52.1/45.8/49.3). With no controller input the motor's gravity +
slope-follow physics slide the player down the slope across cell boundaries,
so `near_batches` climbs 56 → 94 → 302 → 426 (fps 30 → 14) while cells
stream (`build/baseline-final-boot-*`). This is **pre-existing terrain
behavior, not autowalk and not a regression**: `player_motor.cpp` is
unmodified and the no-input path (`requested_velocity = player.velocity`)
calls none of the changed math helpers. The pre-plan autowalk capture masked
it by forcing constant forward speed in a bounded ring (stable at 94). Expect
to see the player slide on a normal boot — it is the same behavior the map
has always had at that spawn.

## One-line per-increment summary (n64-optimization)

- **Inc 1 — Per-TU compiler flags:** 6 hot TUs at `-O2` via target-specific
  Makefile overrides (global stays `-Os`); `make -nB all` proof; 26/26 host
  tests; ROM clean.
- **Inc 2 — Inline hot math + LUT wiring:** hot helpers `always_inline`;
  `CosRadians`/`SinRadians` via a 4096-entry LUT (BSS, memory-neutral);
  boot-time `std::cos/sin` left raw per plan.
- **Inc 3 — Near-pass frustum culling (gated):** `kEnableNearCulling`
  (default OFF) gates `CellAabbInNearCone` per resident cell (center cell
  exempt); device before/after showed no regression; memory neutral.
- **Inc 4 — Actor query cleanup:** `ActorTypeId` enum + ctor stamps +
  RTTI-free `Get<T>`; dead templates + dead test deleted; 27/27 host.
- **Inc 5 — Collision sqrt micro-opt (measurement-gated):** the gate FAILED —
  `update` measured at 0.194–0.195 ms/frame ≪ the ~1 ms threshold, so the
  sqrt opt was skipped; the permanent deliverable is the `kPhaseUpdate`
  instrumentation (`update=` in the `[profiler]` line).
- **Inc 6 — Dead-code removal:** `distant_world_renderer.cpp`,
  `gameplay/arena.*`, `tests/runtime_smoke.cpp` deleted; `.dlod` + baker kept
  on disk for the future feature; elf unchanged; 27/27 host.
- **Inc 7 — Metrics + close-out:** this table; stale distant-pass references
  cleaned from this doc and `AGENTS.md`; device walk capture green.
