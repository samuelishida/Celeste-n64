# Render performance budget (30 fps)

Target: **30 fps** → **≤ 33.3 ms / frame** measured on device with the
per-phase profiler (`[profiler] avg frame time …` + per-phase lines,
every 60 frames via USB serial).

Current candidate runs the DLOD horizon with its own projection, then clears
depth before the detailed near pass (5..800). Separate triple-buffered sky,
distant, and near viewports avoid rewriting matrices still consumed by the
RSP. Residency remains bounded at 13 cells (3×3 core plus four forward Cheb-2
cells), while near and DLOD submit only AABBs accepted by the same conservative
3D frustum predicate and margin. DLOD yields only to READY cells actually
submitted by near; pending cells remain DLOD-owned. This replaces the older
fixed all-occupied draw mask; exact-ROM visual/FPS checks are still required.
Heading-sector changes refresh the four outer residents.

Latest candidate ROM SHA-256:
`ff9b1d1017ca471c243c4d2d7e4a9d45591ff7b69913fc2e9234a4a11250bdce`
(142 host tests pass; ROM builds clean). It includes conservative 3D near
AABB culling shared with DLOD. This is an unqualified performance candidate;
device and 30-fps checks remain open. It includes exact hair-coordinate
packing and a 20-vertex/36-triangle hair sphere per node (down from 32/60);
OG hair node counts and simulation remain unchanged. CPU skinning now bypasses
weighted accumulation for the 1,011/2,492 production vertices whose single
weight is exactly 1; all other vertices keep the same weighted transform.
Dynamic-solid visibility now emits `[dsolid-visibility]` on first sample and
visibility transitions, with actor IDs, bounds, camera, and player position.
Use those records during Ares repro to distinguish platform frustum-culling
from depth/overlap artifacts.
Render telemetry also splits near material-group collection/sort from textured
draw; `near_material_groups` reports actual sort volume.
`weighted_skinning_math_smoke` and full host suite pass. It is **not
Ares-qualified**. Telemetry now also breaks update into fixed-step, actor-world,
and player-physics phases and prints simulated/rendered tick counts. The latest
predecessor capture (`fc7329…`, single-near,
before that fast path) measured 8.9–9.0 fps idle: update 46.7–48.6 ms, near
24.7–24.8 ms, skinning ~22.0 ms, hair ~2.2 ms. Older moving captures showed
6.9–10.9 fps, hair ~9–12 ms, and near ~7–49 ms. These logs identify costs but
do not measure current hash. Re-test this exact ROM in Ares before claiming
visual stability or target frame rate.

## Phase budget (per frame)

| Phase | Budget |
|-------|--------|
| distant (frustum-culled DLOD horizon, Z-on) | ≤ 6 ms |
| high_priority (near ring + dynamic solids, Z-on, coalesced) | ≤ 8 ms |
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
   - `[render-phases] distant=… high_priority=… near_group_build=…
     near_textured_draw=… streaming=… ms` — DLOD, near grouping, textured
     replay, and streaming costs. Distant excludes the skybox; streaming is
     nonzero on chunk transitions/boot or camera-sector preload refresh.
   - `[counters] distant_cells=N distant_faces=N distant_batches=N
     distant_vert_loads=N distant_syncs=N near_material_groups=N ...` — horizon
     coverage and DLOD RSP work, separated from near material-sort volume and
     batches/uploads/loads/syncs.
   - `[memory] total=N used=N free=N` — the mallinfo heap snapshot, printed
     every 3600 frames (so a short capture may contain zero of them).
3. Use the report to judge the pass:
   - `near_batches` ≈ READY submitted residents × material runs (up to 13
     cells). A READY outer resident may draw in the near pass; pending cells
     remain DLOD-owned until ready.
   - `distant_cells` is the visible horizon-cell count; only READY cells
     actually submitted by the fixed near draw ring are excluded.
   - `distant_batches`, `distant_vert_loads`, and `distant_syncs` are derived
     from each cell renderer's active precomputed draw path; they expose actual
     DLOD command volume instead of attributing it to near-pass `syncs`.
   - `texture_uploads` ≈ distinct material runs in the resident set (5–25),
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

- `kMaxResident` (`src/user/gameplay/render/tile_streamer.hpp`): 13 slots —
  detailed 3×3 core plus four camera-forward Cheb-2 preload residents. Near
  and DLOD use the same conservative 3D AABB/frustum culling; READY visible
  cells are near-owned, and pending visible cells remain DLOD-owned.
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

### Distant LOD

DLOD is active. Its near plane matches the detailed pass: a ring-edge near clip
can slice a nonresident cell when camera follow leads the active cell during
fast movement. Only ready cells in the fixed detailed 3×3 draw ring are
excluded from DLOD; pending core cells and all outer preloads retain coarse
coverage. Its far plane covers the full 3D map-bounds diagonal. Keep this
ownership rule aligned with the near streamer.
Tune distant culling only against current-hash Ares captures and the ≤6 ms
phase budget.

## If a phase still dominates

- **high_priority / texture_upload still > 8 ms**: global near material
  grouping is already active. Use current-hash batch/load/sync counts to locate
  remaining cost; only then change run coalescing or texture payloads.
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
- Improve batching or lower far range only after resident coverage is visually
  validated and Ares telemetry confirms the 30 fps target.

## Final measured before/after (n64-optimization close-out)

Historical single-near-pass measurements on Ares (sole device emulator).
BEFORE = pre-plan autowalk capture
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

**Historical memory note:** these 2026-08/09 captures report `mallinfo()` for
their then-current ROM: stationary map-center `total=2,165,944`,
`used=2,146,768`, `free=19,176`; walking `total=2,288,680`,
`used=2,230,464`, `free=58,216`, stable across that walk capture. These values
are not current for the weighted-player ROM. The latest stationary Ares sample
is documented in `docs/memory_budget.md` and reports arena
`6,403,784 / 6,369,896 / 33,888` total/used/free bytes. `mallinfo` free is
current arena reserve, not full Expansion Pak headroom because `sbrk` can grow
the arena; framebuffer, stack, RSP, and other non-arena allocations also need
accounting before stating total RDRAM headroom.

**Historical reading (single-near 2026-08/09 build):** that build was
**RSP-bound at ~33 ms / ~30 fps**; the
CPU slices are small — `update` is ~0.6% of the frame (0.194 ms),
`high_priority` ~0.2%, `streaming` ~0%. The stationary→walking
`near_batches` jump (56 → 94) is the 3×3 ring streaming new cells as the
player walks; it is the expected working state, not a regression. The
before/after frame-time delta (34.5 → 33.1 ms) is within Ares run-to-run
noise; the structural wins are the trimmed draw path (no distant pass), the
per-material uploads, and the per-TU `-O2` on the hot TUs. This is not evidence
for the current weighted-player, 13-resident, two-pass ROM.

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
- **Inc 3 — Near-pass frustum culling (history):** camera-driven near culling
  was removed after screenshots showed missing terrain and cell-edge flicker.
  Current (2026-10-09) draw mask is the fixed occupied 13-resident set; DLOD
  uses 3D AABB plane culling. Current-hash Ares confirmation is pending.
- **Inc 4 — Actor query cleanup:** `ActorTypeId` enum + ctor stamps +
  RTTI-free `Get<T>`; dead templates + dead test deleted; 27/27 host.
- **Inc 5 — Collision sqrt micro-opt (measurement-gated):** the gate FAILED —
  `update` measured at 0.194–0.195 ms/frame ≪ the ~1 ms threshold, so the
  sqrt opt was skipped; the permanent deliverable is the `kPhaseUpdate`
  instrumentation (`update=` in the `[profiler]` line).
- **Inc 6 — Dead-code removal (historical):** `gameplay/arena.*` and
  `tests/runtime_smoke.cpp` deleted. The distant tier was later reintroduced
  for the current Z-split; `.dlod` + baker remain active. Historical test count
  and ELF result do not qualify current ROM.
- **Inc 7 — Metrics + close-out:** this table; stale distant-pass references
  cleaned from this doc and `AGENTS.md`; device walk capture green.
