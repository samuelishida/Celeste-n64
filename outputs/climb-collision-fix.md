# Climb & collision bug hunt — final brief

**Slug:** climb-collision-fix
**Date:** 2026-09-07
**Autoresearch loop:** 5 iterations, converged (see `autoresearch.jsonl`).

## Problem

The user reported (device-side, Ares): "no functional grab mechanics, Madeline
just latches to the wall; when latching it's super buggy — the wall keeps
pushing her back, going up makes it push her down, the model wiggles around;
sometimes on a few walls she clips through and gets stuck inside the
geometry."

## Method

Built `tests/climb_bug_probe.cpp` — a host probe that drives the REAL
controller + mover (the `climb_mantle_smoke` harness) against synthetic walls
with the failure modes: vertical (baseline), overhang (leans toward the
player, normal.y < 0), lean-away (normal.y > 0), thin (2 units thick). Per
wall it asserts: sustained climb, no down-push while climbing up, no wiggle,
no clip/stuck. Metric: failing assertions (lower is better). Baseline: 6/16
failures. Final: 16/16 PASS.

## Root causes (all confirmed by the probe)

1. **Climb moved straight up in world Y** — OG Celeste64 moves along the
   wall's UP direction (`-wallUp * inputY`, Player.cs:1476-1480). On sloped
   walls the head/waist enter the wall plane and the mover's popout fires
   with full-3D pushes (including Y) → down-push, wiggle, ejection.
2. **The popout's head wall query + ceiling check fought the climb** — the
   head sits 10 units above the center at a different distance from a sloped
   plane. Fix: `MoveService::SweepMoveClimb` (waist-only popout during climb).
3. **The entry snap's 3-unit/axis cap broke the anchor on sloped walls** — the
   snap distance is legitimately > 3 units there (6.06 for a 15° lean), leaving
   the body embedded in the wall plane.
4. **The corner system fired on plain up-climbs** — the `move_side` gate used
   the raw camera-relative XZ length (nonzero for stick-up: the stick Y maps
   to the camera's forward XZ), and the corner rays normalized the tiny
   projection BEFORE the length check, so a unit vector passed and the outer
   corner re-anchored the body through the wall (the z=14.19 → z=8.39
   teleport).
5. **The grip query accepted walls the player was behind** (overhang) — the
   entry stored the flipped normal and the facing gate released her the next
   tick. Fix: OG's front-side gate (`distanceToPlane < 0 → continue`,
   World.cs:519).
6. **The popout's signed pushout pushed bodies through walls** (wrong
   approach, reverted) — OG pushes along the direction from the closest point
   to the body (`pushout = (radius - dist) * diff.Normalized()`,
   World.cs:527), never through the face.

## Changes

- `src/user/gameplay/player/player_controller.cpp` — wallUp climb movement,
  entry snap without the cap, corner lateral gate + pre-normalization
  projection checks.
- `src/user/gameplay/player/player_movement.cpp/.hpp` — new
  `MoveService::SweepMoveClimb` (waist-only popout during climb); MovePhase
  routes Climbing through it.
- `src/user/gameplay/world/world.cpp/.hpp` — wall query `orient_to_point`
  parameter: front-side gate for the grip (true), OG pushout semantics for
  the popout (false).
- `tests/climb_bug_probe.cpp` (new), `tests/climb_no_clip_smoke.cpp` (fixed
  reversed-winding fixture), `tests/run_host_tests.sh` (probe wired in).

## Verification

- **Host:** probe 16/16 PASS; `make test` 57 passed / 0 failed.
- **Build:** ROM builds (elf text 437,592; z64 1,343,488 B).
- **Device:** `build/raw-baseline-climbfix-20260907-140725.txt.log` — 167/167
  counters at baseline-identical near_batches=56 resident=13, `[memory]
  used=5,822,032` (flat), no exceptions. The player was stationary during the
  capture (no [climb] lines); the physics fixes are host-verified, and a
  manual play test in Ares is the remaining feel check.
- **Verification: BLOCKED (partial)** — the bundled researcher/verifier/
  reviewer subagents could not run (this session is not under tmux; spawns
  fail with "Subagents require tmux"). The loop ran in lead-owned direct
  mode with the probe as the deterministic benchmark.

## Unresolved

- Manual play test in Ares to confirm the feel (the original complaint was
  device-side).
- The overhang-from-front climb leaves the head ~1.6 units inside the sloped
  plane (a visual artifact on rare sloped walls; the body is at the correct
  anchor). The OG never has sloped walls (axis-aligned boxes), so this is a
  demake-specific edge case.
