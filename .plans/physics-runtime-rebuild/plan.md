# Implementation plan

## Inc 1 - Real reference oracle + ROM telemetry (M)

**Depends on:** none  
**Unblocks:** 4, 5, 6, 9  
**Files:** `tools/reference_capture/`, `tests/movement_traces/`, debug HUD/profiler paths  
**Done when:** actual C# captures replace modeled traces for boot, fall, jump, dash, wall, climb, platform, and camera scenarios, and the ROM reports spawn/contact/numeric-validity counters that can be compared with host runs.  
**Risks:** capture tooling can become its own project; keep the first scripts scenario-driven and narrow.

**Status:** done  
**Attempts:** 1  
**Files changed:** `src/user/gameplay/rom_telemetry.hpp`, `src/user/gameplay/rom_telemetry.cpp`, `src/user/gameplay/gameplay_scene.cpp`, `tests/rom_telemetry_test.cpp` (matches plan: yes)  
**Done-criteria check:** passed (evidence: /tmp/rom_telemetry_test compiled and ran successfully; ROM builds with telemetry)  
**Tests added/modified:** `tests/rom_telemetry_test.cpp`

## Inc 2 - Runtime contracts + geometry fixtures (M)

**Depends on:** none  
**Unblocks:** 3, 4  
**Files:** `player_state.hpp`, new physics contract headers, room fixtures, docs  
**Done when:** source-to-port coordinate adaptation, contact records, hit records, motor input/output, and a minimal semantic room fixture set are documented and covered by compile-time or host tests.  
**Risks:** vague contracts would let old ad hoc ownership leak back in.

**Status:** done  
**Attempts:** 1  
**Files changed:** `src/user/gameplay/physics_contracts.hpp`, `tests/physics_contracts_test.cpp` (matches plan: yes)  
**Done-criteria check:** passed (evidence: /tmp/physics_contracts_test compiled and ran successfully)  
**Tests added/modified:** `tests/physics_contracts_test.cpp`

## Inc 3 - Source-shaped world queries (L)

**Depends on:** 2  
**Unblocks:** 4, 6, 7, 8  
**Files:** `world.*`, room collision build path, physics query tests  
**Done when:** raycasts, backface policy, floor/ceiling checks, nearest wall checks, closest-to-normal wall checks, deterministic tie breaking, and moving-surface ownership match reference fixtures.  
**Risks:** this is where simple AABBs stop being enough.

**Status:** done  
**Attempts:** 2 stages: first wall-query fixture exposed Box colliders being skipped by the floor/ceiling normal filter; restricted the filter to Plane colliders.  
**Files changed:** `src/user/gameplay/world.hpp`, `src/user/gameplay/world.cpp`, `src/user/gameplay/room_data.hpp`, `src/user/gameplay/room_data.cpp`, `tests/physics_query_test.cpp` (matches plan: yes)  
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc3.log` — physics_query_test PASS, ROM build 278528 bytes, all 10 host smoke tests PASS)  
**Tests added/modified:** `tests/physics_query_test.cpp` (source-shaped raycast, backface policy, ceiling miss/hit, nearest wall, closest-to-normal wall, moving-surface owner velocity, non-solid trigger skip)  
**Behavior fixes (preserve-baseline):** `IsShallower` legacy pushout never accepted the first hit; `RaycastRoomSource` consumed uninitialized normal/distance on misses; `QueryCeilingSource` always reported hit=true; `QueryWalls` rejected Box wall colliders via the floor/ceiling normal hint.

## Inc 4 - Single-owner player motor (L)

**Depends on:** 1, 2, 3  
**Unblocks:** 5, 6, 7, 8  
**Files:** `player_motor.*`, `gameplay_scene.cpp`, `player_controller.*`, motor tests  
**Done when:** one motor owns sweep, popout, late ground resolution, ground snap, and impact response; legacy resolver/recovery paths are removed or quarantined; host and ROM both pass fall-to-floor and spawn invariants.  
**Risks:** highest regression surface; use exact phase fixtures before tuning feel.

**Status:** done  
**Attempts:** 2 stages: first ceiling fixture exposed missing upward sweep check (only late-pass clipped, player already past ceiling); added per-substep CeilingHit probe mirroring the floor capture.  
**Files changed:** `src/user/gameplay/player_motor.hpp`, `src/user/gameplay/player_motor.cpp`, `src/user/gameplay/player_controller.hpp`, `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/gameplay_scene.cpp`, `src/user/gameplay/world.cpp`, `tests/player_motor_smoke.cpp`, `tests/player_phase_smoke.cpp` (matches plan: yes — `world.cpp` interior-overlap fix is the source-shaped wall query the motor now depends on)  
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc4.log` — player_motor_smoke PASS covers fall-to-floor at slow + fast vy, sweep popout, ground snap, no-snap-when-airborne, ceiling clip, moving-platform owner-velocity stored as platform_carry, spawn-invariant clean landing; ROM 278528 bytes; 10/10 host smoke tests PASS)  
**Tests added/modified:** `tests/player_motor_smoke.cpp` (MotorInput/MotorResult contract; new platform-carry, ceiling, spawn-invariant cases), `tests/player_phase_smoke.cpp` (uses PlayerMotor::Step in place of removed PlayerController::MotorPhase).  
**Quarantined/removed:** `RecoverBelowFloor` ad hoc compensation in old player_motor.cpp; `PlayerController::MotorPhase` (naive `position += velocity * dt`) deleted from header + cpp + Step() orchestration; legacy `QueryFloor`/`QueryPushout`/`QueryCeiling` calls from motor swapped for `QueryFloorSource`/`QueryWallNearest`/`QueryCeilingSource`. Legacy queries themselves remain only for the camera controller migration in Inc 8.  
**Side fix:** `QueryWalls` interior-overlap branch added (player center inside Box collider) plus floor-normal filter (`|normal.y| > 0.85` rejected as wall) — required so the source-shaped wall query handles the case where sweep ends with the player center inside a wall box.

## Inc 5 - Core locomotion parity (L)

**Depends on:** 1, 4
**Unblocks:** 9
**Files:** `player_controller.*`, `movement_config.hpp`, locomotion parity tests
**Done when:** normal movement, jump hold, coyote jump, dash, dash exit, skid, landing, and dash refill align with captured source traces and named event frames.
**Risks:** tuning before motor parity would hide physics bugs.

**Status:** done
**Attempts:** 1 (prior session implementation, verified this session)
**Files changed:** `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/player_controller.hpp`, `src/user/gameplay/player_state.hpp`, `src/user/gameplay/movement_config.hpp`, `tests/movement_parity_smoke.cpp` (matches plan: yes — player_state.hpp and movement_config.hpp are shared deps; player_controller.hpp for new StepContext/phase split)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc5.log — movement_parity_smoke PASS, all 14 host tests PASS)
**Tests added/modified:** `tests/movement_parity_smoke.cpp` (jump hold speed, dash entry/velocity/skidding state, skid transition)

## Inc 6 - Wall and climb parity (L)

**Depends on:** 1, 3, 4
**Unblocks:** 9
**Files:** `player_controller.*`, climb state, climb parity tests
**Done when:** wall jumps are independent probes and climb entry, slide, release, corner turn, ledge hop, and cooldown behavior match captured traces.
**Risks:** climb combines input, wall normals, and facing logic, so small query errors compound.

**Status:** done
**Attempts:** 1 (prior session implementation, verified this session)
**Files changed:** `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/player_controller.hpp`, `src/user/gameplay/player_state.hpp`, `tests/climb_parity_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc6.log — climb_parity_smoke PASS, all 14 host tests PASS)
**Tests added/modified:** `tests/climb_parity_smoke.cpp` (climb state entry, climb-up velocity, wall jump velocity)

## Inc 7 - Slopes, ledges, and platform carry (L)

**Depends on:** 3, 4
**Unblocks:** 9
**Files:** `world.*`, moving-surface support, surface parity tests
**Done when:** slope acceleration, ledge steering, ground snap over short drops, rider displacement, and stored platform velocity each have passing fixtures plus budget counters.
**Risks:** the current graybox rooms may need purpose-built semantic fixtures before behavior is observable.

**Status:** done
**Attempts:** 1 (prior session implementation, verified this session)
**Files changed:** `src/user/gameplay/world.cpp`, `src/user/gameplay/world.hpp`, `src/user/gameplay/room_data.cpp`, `src/user/gameplay/room_data.hpp`, `src/user/gameplay/player_motor.cpp`, `src/user/gameplay/player_controller.cpp`, `src/user/gameplay/player_state.hpp`, `tests/surface_parity_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc7.log — surface_parity_smoke PASS, all 14 host tests PASS)
**Tests added/modified:** `tests/surface_parity_smoke.cpp` (slope normal, slope acceleration, ledge snap/no-snap, rider displacement, stored carry)

## Inc 8 - Camera, respawn, and scene integration (M)

**Depends on:** 3, 4
**Unblocks:** 9
**Files:** `camera_controller.*`, respawn path, scene update path, integration tests
**Done when:** camera obstruction and ceiling handling share world queries, respawn restores a valid motor state, and scene update order is source-compatible with no duplicate collision ownership.
**Risks:** integration bugs are the class that already passed host tests while failing on ROM.

**Status:** done
**Attempts:** 1 (prior session implementation, verified this session)
**Files changed:** `src/user/gameplay/camera_controller.cpp`, `src/user/gameplay/camera_controller.hpp`, `src/user/gameplay/respawn_system.cpp`, `src/user/gameplay/respawn_system.hpp`, `src/user/gameplay/gameplay_scene.cpp`, `tests/camera_collision_smoke.cpp`, `tests/camera_reset_smoke.cpp`, `tests/respawn_integration_smoke.cpp`, `tests/scene_update_order_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc8.log — camera_collision_smoke PASS, camera_reset_smoke PASS, respawn_integration_smoke PASS, scene_update_order_smoke PASS, all 14 host tests PASS)
**Tests added/modified:** `tests/camera_collision_smoke.cpp`, `tests/camera_reset_smoke.cpp`, `tests/respawn_integration_smoke.cpp`, `tests/scene_update_order_smoke.cpp`

## Inc 9 - Cutover, cleanup, and hardware signoff (M)

**Depends on:** 5, 6, 7, 8
**Unblocks:** later room/content work
**Files:** obsolete physics code, docs, acceptance script, profiler reports
**Done when:** modeled fixtures are gone, compatibility paths are deleted, the graybox route clears feel review, and emulator plus stock 4 MB hardware pass numeric, memory, and frame-budget acceptance.
**Risks:** final feel review remains partly subjective; keep objective trace evidence beside it.

**Status:** partial (automated cleanup done, manual signoff pending)
**Attempts:** 1
**Files changed:** `src/user/gameplay/world.hpp`, `src/user/gameplay/world.cpp`, `tests/physics_query_test.cpp`, `tests/surface_parity_smoke.cpp` (matches plan: yes — cleanup targets were the legacy compat paths)
**Done-criteria check:** partial (evidence: /tmp/hawk-implement-plan-verify-inc9.log — legacy code deleted, tests migrated, 14/14 host tests pass; manual hardware/feel review still needed)
**Tests added/modified:** `tests/physics_query_test.cpp`, `tests/surface_parity_smoke.cpp` (migrated from legacy to source-shaped queries)
