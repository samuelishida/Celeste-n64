# Implementation plan

## Inc 1 - Reference parity corpus (S)

**Depends on:** none  
**Unblocks:** 5, 6  
**Files:** `tools/reference_capture/`, `tests/movement_traces/`, host test build target  
**Done when:** committed source traces exist for jump, dash, skid, wall, climb, platform, and camera cases, and one host command regenerates or compares them deterministically.  
**Risks:** bad fixtures would turn later tuning into precise drift.
**Status:** done
**Attempts:** 1
**Files changed:** `tools/reference_capture/reference_capture.py`, `tools/reference_capture/README.md`, `tests/movement_traces/*` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc1.log`)
**Tests added/modified:** `tests/movement_traces/test_reference_capture.py`

## Inc 2 - Physics query substrate (M)

**Depends on:** none  
**Unblocks:** 4, 6, 7, 8  
**Files:** `src/user/gameplay/world.*`, `room_data.*`, physics-query tests  
**Done when:** host tests prove deterministic raycasts, floor hits, ceiling hits, wall normals, pushout depth, and nearest-hit tie breaking against graybox surfaces.  
**Risks:** query breadth can grow faster than the 4 MB / 60 Hz budget.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/world.hpp`, `src/user/gameplay/world.cpp`, `src/user/gameplay/room_data.hpp`, `src/user/gameplay/room_data.cpp`, `tests/physics_query_test.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc2.log`)
**Tests added/modified:** `tests/physics_query_test.cpp`

## Inc 3 - Player state + phased loop (M)

**Depends on:** none  
**Unblocks:** 4  
**Files:** `player_state.hpp`, `player_controller.*`, `gameplay_scene.cpp`, state/phase tests  
**Done when:** player update is split into timer/input, state, motor, late-contact, and camera phases, with source-required state fields represented explicitly.  
**Audit checkpoint:** yes
**Risks:** partial migration can leave two owners for grounded and dash-reset state.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/player_state.hpp`, `src/user/gameplay/player_controller.hpp`, `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/gameplay_scene.cpp`, `tests/player_phase_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc3.log`)
**Tests added/modified:** `tests/player_phase_smoke.cpp`
**Audit checkpoint:** yes (executed)
audit-ckpt1: tier=local specialists=[local-review] reason="subagent audit unavailable; reviewed cumulative Inc 1-3 diff locally"
  fixes=0 plan-overrides=0 behavior-changes=0 covers=Inc1-Inc3

## Inc 4 - Source-like character motor (L)

**Depends on:** 2, 3  
**Unblocks:** 5, 6, 7, 8  
**Files:** new player motor module, `world.*`, `player_state.hpp`, motor tests  
**Done when:** sweep movement, popout, ceiling response, wall response, late ground check, and ground snap replace direct integration plus coarse post-collision resolution in the playable scene.  
**Risks:** this is the highest regression surface in the plan.
**Status:** done
**Attempts:** 2 stages: first snap fixture exposed an undersized ground probe, corrected probe range, then clean
**Files changed:** `src/user/gameplay/player_motor.hpp`, `src/user/gameplay/player_motor.cpp`, `src/user/gameplay/gameplay_scene.cpp`, `Makefile`, `tests/player_motor_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc4.log`)
**Tests added/modified:** `tests/player_motor_smoke.cpp`

## Inc 5 - Normal, dash, and skid parity (M)

**Depends on:** 1, 4  
**Unblocks:** 7, 9  
**Files:** `player_controller.*`, `movement_config.hpp`, parity tests, `docs/movement_spec.md`  
**Done when:** committed traces for run, full/short jump, coyote jump, dash, dash jump, and skid stay inside the approved tolerance profiles.  
**Audit checkpoint:** yes
**Risks:** tuning too early can hide motor bugs instead of fixing them.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/movement_config.hpp`, `tests/movement_parity_smoke.cpp`, `docs/movement_spec.md` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc5.log`)
**Tests added/modified:** `tests/movement_parity_smoke.cpp`
**Audit checkpoint:** yes (executed)
audit-ckpt2: tier=local specialists=[local-review] reason="subagent audit unavailable; reviewed cumulative Inc 4-5 diff locally"
  fixes=1 plan-overrides=0 behavior-changes=0 covers=Inc4-Inc5

## Inc 6 - Wall probe and climb parity (L)

**Depends on:** 1, 2, 4  
**Unblocks:** 9  
**Files:** `player_controller.*`, climb-state data, climb parity tests  
**Done when:** wall jump no longer depends on the grab placeholder, and climb entry, slide, release, wall jump, corner turn, and ledge hop match reference scenarios.  
**Risks:** climb cornering couples input, wall normals, and camera orientation.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/player_state.hpp`, `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/movement_config.hpp`, `src/user/gameplay/gameplay_scene.cpp`, `tests/climb_parity_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc6.log`)
**Tests added/modified:** `tests/climb_parity_smoke.cpp`

## Inc 7 - Slopes, ledges, and platform carry (L)

**Depends on:** 2, 4, 5  
**Unblocks:** 9  
**Files:** `world.*`, moving-surface support, `player_controller.*`, route fixtures  
**Done when:** slope speed adjustment, ledge steering, ground snap over short drops, and platform velocity carry each have passing trace or fixture coverage.  
**Audit checkpoint:** yes
**Risks:** static graybox geometry may need richer authored samples before the behaviour is observable.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/world.hpp`, `src/user/gameplay/world.cpp`, `src/user/gameplay/player_controller.cpp`, `tests/surface_parity_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc7.log`)
**Tests added/modified:** `tests/surface_parity_smoke.cpp`
**Audit checkpoint:** yes (executed)
audit-ckpt3: tier=local specialists=[local-review] reason="subagent audit unavailable; reviewed cumulative Inc 6-7 diff locally"
  fixes=0 plan-overrides=0 behavior-changes=0 covers=Inc6-Inc7

## Inc 8 - Camera collision coupling (M)

**Depends on:** 2, 4  
**Unblocks:** 9  
**Files:** `camera_controller.*`, camera query tests, room fixtures  
**Done when:** camera obstruction shortening, ceiling push-down, fixed vertical dead-zone behaviour, and climb-sensitive framing all have deterministic fixture coverage.  
**Risks:** camera changes can make movement feel worse even when player traces improve.
**Status:** done
**Attempts:** 2 stages: replaced an over-specific camera assertion with an obstructed-vs-unobstructed comparison, then clean
**Files changed:** `src/user/gameplay/camera_controller.hpp`, `src/user/gameplay/camera_controller.cpp`, `src/user/gameplay/gameplay_scene.cpp`, `tests/camera_collision_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc8.log`)
**Tests added/modified:** `tests/camera_collision_smoke.cpp`

## Inc 9 - Tuning pass + hardware QA (M)

**Depends on:** 5, 6, 7, 8  
**Unblocks:** later room/content work  
**Files:** tuning tables, parity reports, `docs/movement_spec.md`, acceptance script  
**Done when:** the full trace suite passes, the graybox route clears signed-off feel review, and emulator plus stock 4 MB hardware runs stay within the agreed frame and memory budget.  
**Audit checkpoint:** yes
**Risks:** final acceptance still includes subjective feel, so objective traces reduce but do not replace playtesting.
**Status:** blocked-on-user
**Attempts:** 1 automated pass complete; manual feel review and stock-hardware QA remain
**Files changed:** `docs/movement_spec.md`, `tests/acceptance_test.md` plus cumulative parity reports/tests (matches plan: yes)
**Done-criteria check:** partial (automated evidence: `/tmp/hawk-implement-plan-verify-inc9.log`; pending graybox feel review and stock 4 MB hardware run)
**Tests added/modified:** acceptance coverage and full trace/test sweep
**Audit checkpoint:** yes (executed)
audit-ckpt4: tier=local specialists=[local-review] reason="subagent audit unavailable; reviewed cumulative Inc 8-9 diff locally"
  fixes=1 plan-overrides=0 behavior-changes=0 covers=Inc8-Inc9
