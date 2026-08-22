# Autonomous device verification & telemetry discipline (n64-optimization)

## Context
N64 CPU+RSP optimization of the single near-pass renderer
(`.plans/n64-optimization/`, Inc 1–7). The user was away from the PC, so every
device check had to run autonomously: flip a debug flag, build, launch Ares via
`tools/capture_baseline.sh`, parse the telemetry log, flip the flag back,
rebuild. No human in the loop.

## What surprised us
- **Docs drift from device telemetry.** `docs/perf_budget.md` and `AGENTS.md`
  both claimed a "≈ 5.48 MB arena / used ≈ 5.39 MB" heap. Every capture in the
  plan showed the real `[memory]` mallinfo arena is **2.16 MB total / 2.15 MB
  used (stationary)** and **2.29 MB / 2.23 MB (walking)**. The stale figure
  predated the current heap layout. Never cite a memory figure from a doc —
  cite the latest capture.
- **Walking arena > stationary arena is NOT a leak.** The 3×3 resident ring
  holds more cells while streaming, so the walking heap is ~130 KB larger. It
  was **identical across 5 `[memory]` reports** in the walk capture — stable,
  not growing. "Larger while moving" + "stable across reports" = expected
  ring behavior.
- **The frame is RSP-bound, confirmed by the gate.** Inc 5's measurement gate
  added a `kPhaseUpdate` profiler phase and found `update` = 0.194 ms ≈ 0.6%
  of the 33 ms frame — far below the ~1 ms threshold that would have justified
  the planned collision sqrt micro-opt. The opt was correctly skipped; the
  instrumentation is the permanent deliverable.
- **A debug "walk" flag can MASK pre-existing terrain physics.** With
  `kDebugAutoWalk` ON the player is forced to walk forward at constant speed
  in a bounded 3×3 ring, so `near_batches` sits stable at 94. With it OFF,
  the corner Start spawn (x=0.00, exactly on a cell boundary, on sloped
  terrain) lets the motor's gravity + slope-follow slide the player across
  cells, so `near_batches` climbs 56 → 426 and fps dips to 14. That climb is
  NOT autowalk and NOT a regression — `player_motor.cpp` was unmodified and
  the no-input path (`requested_velocity = player.velocity`) calls none of the
  changed math helpers. Lesson: when a debug input driver is removed, re-capture
  the no-input boot and diff the movement signature (stable-vs-climbing
  `near_batches`) before assuming a regression; a different signature +
  untouched physics files = pre-existing behavior revealed, not introduced.

## What we'd do differently
- Nothing structural — the gate design (measure first, optimize only if the
  threshold is crossed) worked exactly as intended. The only cost was writing
  the sqrt opt's plan text that never shipped.

## Reusable workflow (autonomous device verification)
1. **Walk without a controller:** flip `kDebugAutoWalk = true` in
   `src/user/gameplay/debug_flags.hpp` (consumed in `gameplay_scene.cpp`),
   `./compile-rom.sh`, then
   `tools/capture_baseline.sh madeline_cube_rom.z64 120 <label>` (3-arg
   positional signature: rom, wall_seconds, label → `build/baseline-<label>-<ts>.txt`
   + `build/raw-<same>.txt.log`).
2. **Parse the raw log** (grep `[profiler]`/`[render-phases]`/`[counters]`/
   `[memory]`/`FATAL`). The known non-fatal boot line is
   `[init] FATAL: madeline model missing at rom:/mdl/player.t3dm` — ignore it.
3. **Stationary vs walking signal:** `[counters] near_batches` = **56**
   stationary / **94** walking (ring streaming). This is the fast, decisive
   signal — `[memory]` only fires every **3600 frames** (~120 s at 30 fps), so
   a 120 s capture may contain 0–5 memory reports.
4. **Flag in/out proof:** the elf size delta. autowalk=true build = 651604
   total; autowalk=false = 651468 (136 bytes smaller — the autowalk block is
   dead-code-eliminated). Compare `text/data/bss/total` from the `[LD]` line.
5. **Flip the flag back to false and rebuild** — never ship an autowalk ROM.
6. **Ares is GUI-only** — the capture script launches it in the background,
   polls stdout, and kills the process group; there is no run-and-exit mode.
   `kDebugCameraRotateDeg` is boot-time-only, so a continuous orbit is not
   possible without controller input; orbit coverage comes from existing
   captures.

## Per-TU optimization flags (Inc 1)
Global build stays `-Os`; 6 hot TUs (`tile_streamer.o`,
`textured_room_renderer.o`, `lvl_room_renderer.o`, `open_world_renderer.o`,
`camera_controller.o`, `player_motor.o`) build at `-O2` via **target-specific**
`CXXFLAGS += -O2` overrides in the Makefile (GCC last-`-O`-wins). Verify with
`make -nB all` (NOT `make -n <file>.o`, which doesn't show the override).
`world.cpp` is deliberately NOT in the -O2 set (known `-Os`/`-ffast-math`
miscompile class; it carries `__attribute__((noinline))` workarounds).
