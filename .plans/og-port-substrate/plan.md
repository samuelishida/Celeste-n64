# Implementation plan

## Inc 1 — Port-gap ledger + trace fixtures (S)

**Depends on:** none
**Unblocks:** 2, 3, 7
**Files:** `docs/og_port_gap_matrix.md`, `tests/movement_traces/*`, `tools/reference_capture/*`
**Done when:** the ledger maps each source dependency to `present / partial / missing`, defines trace schema + numeric tolerance, and at least one trace exists for buffered jump, wall snap, ledge assist, moving-platform jump, and actor overlap.
**Risks:** none beyond global.
**Status:** done
**Attempts:** 1
**Files changed:** `docs/og_port_gap_matrix.md`, `tests/movement_traces/buffered_jump.json`, `tests/movement_traces/wall_snap.json`, `tests/movement_traces/ledge_assist.json`, `tests/movement_traces/moving_platform_jump.json`, `tests/movement_traces/actor_overlap.json` (extra concrete files under declared glob)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc1.log`)
**Tests added/modified:** JSON fixture validation via `python3 -m json.tool`

## Inc 2 — Input compatibility layer (M)

**Depends on:** 1
**Unblocks:** 7
**Files:** new `src/user/gameplay/input/*`, `src/user/gameplay/scene/gameplay_scene.cpp`, `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/player/player_controller.{hpp,cpp}`, `tests/input_compat_smoke.cpp`, `tests/gameplay_smoke.cpp`, `Makefile`
**Done when:** host tests prove `VirtualButton` buffering + `ConsumePress`, stick circular deadzone, and take-newer axis overlap; buffered input becomes the only jump/dash buffer owner; player reads the new layer without changing current smoke behaviour.
**Risks:** axis-sign mistakes at the N64 boundary can masquerade as physics bugs.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/input/virtual_input.{hpp,cpp}`, `src/user/gameplay/scene/gameplay_scene.cpp`, `src/user/gameplay/player/player_state.hpp`, `src/user/gameplay/player/player_controller.{hpp,cpp}`, `tests/input_compat_smoke.cpp`, `tests/gameplay_smoke.cpp`, `Makefile` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc2.log`)
**Tests added/modified:** `tests/input_compat_smoke.cpp`, `tests/gameplay_smoke.cpp`

## Inc 3 — Runtime helper layer (S)

**Depends on:** 1
**Unblocks:** 4, 7
**Files:** new `src/user/gameplay/runtime/*`, `src/user/gameplay/player/player_controller.cpp`, `tests/runtime_helpers_smoke.cpp`, `Makefile`
**Done when:** shared helpers cover `Approach`, angle helpers, interval timing, and `StateMachine`; player/controller no longer carries private clones of reusable source semantics.
**Risks:** keep this movement-first; coroutine support stays deferred until a source actor actually needs it.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/runtime/math.{hpp,cpp}`, `src/user/gameplay/runtime/timing.{hpp,cpp}`, `src/user/gameplay/runtime/state_machine.hpp`, `src/user/gameplay/player/player_controller.cpp`, `tests/runtime_helpers_smoke.cpp`, `Makefile` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc3.log`)
**Tests added/modified:** `tests/runtime_helpers_smoke.cpp`

## Inc 4 — Actor/world lifecycle + traits (M)

**Depends on:** 3
**Unblocks:** 5, 7
**Files:** `src/user/gameplay/actor/*`, `src/user/gameplay/world/actor_{world,factory}.*`, `tests/actor_world_smoke.cpp`, `Makefile`
**Done when:** deferred add/remove, typed queries, overlap queries, recycling, `Pickup`/`Pushout`/`RidePlatform` traits, spawn factory use, and trait-driven spring/refill/strawberry pickup are covered by host tests; current frame order stays observable.
**Risks:** update-order regressions; preserve the documented phase contract around the new actor phase.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/actor/{actor,traits,refill_actor,spring_actor,strawberry_actor}.hpp`, `src/user/gameplay/world/actor_{world,factory}.{hpp,cpp}`, `tests/actor_world_smoke.cpp`, `Makefile` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc4.log`)
**Tests added/modified:** `tests/actor_world_smoke.cpp`

## Inc 5 — Actor-backed solid/query facade (M)

**Depends on:** 4
**Unblocks:** 6, 7
**Files:** `src/user/gameplay/world/*`, new `solid` actor files, query tests
**Done when:** solids can own faces/bounds, actor/world queries become authoritative when attached, `Room` remains only the motor-facing envelope/fallback, world queries return owner-backed ray/wall hits, and broadphase-pruned actor results match current graybox query tests.
**Risks:** this is the ownership bridge; keep motor-facing signatures stable while fixture-testing every query face.
**Status:** done
**Attempts:** 2 stages: initial facade-only bridge rejected; user selected actor/world-primary ownership and the implementation was revised
**Files changed:** `src/user/gameplay/actor/solid_actor.{hpp,cpp}`, `src/user/gameplay/world/{world.hpp,world.cpp}`, `tests/solid_actor_query_smoke.cpp`, `Makefile` (matches revised plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc5.log`)
**Tests added/modified:** `tests/solid_actor_query_smoke.cpp`

## Inc 6 — Moving-solid rider bridge (M)

**Depends on:** 5
**Unblocks:** 7
**Files:** `src/user/gameplay/world/*`, actor trait files, `tests/surface_parity_smoke.cpp`, `tests/scene_update_order_smoke.cpp`
**Done when:** moving solids propagate rider displacement and stored velocity through one explicit trait contract; scene-order tests prove the documented update phase; current moving-platform smoke behaviour stays green.
**Risks:** rider propagation is frame-order sensitive; verify before player backfill.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/actor/moving_solid_actor.{hpp,cpp}`, `tests/solid_actor_query_smoke.cpp`, `tests/surface_parity_smoke.cpp`, `Makefile` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc6.log`)
**Tests added/modified:** `tests/solid_actor_query_smoke.cpp`, `tests/surface_parity_smoke.cpp`

## Inc 7 — Player parity backfill on the new substrate (L)

**Depends on:** 2, 3, 4, 5, 6
**Unblocks:** future actor ports
**Files:** `src/user/gameplay/player/player_controller.cpp`, `src/user/gameplay/player/player_motor.cpp`, parity tests/docs
**Done when:** source-shaped fixtures cover climb snap-up, inner/outer corners, ledge avoidance, buffered wall-jump consumption, platform-velocity clamp, and spike-safe climb; no player-only shim remains for functionality now provided by the substrate.
**Risks:** the player is where many systems meet; do this after the lower layers are observable, not before.
**Status:** done
**Attempts:** 1
**Files changed:** `src/user/gameplay/player/player_{controller.cpp,motor.cpp,state.hpp}`, `src/user/gameplay/actor/{traits.hpp,spike_actor.hpp}`, `src/user/gameplay/world/actor_world.{hpp,cpp}`, `tests/climb_parity_smoke.cpp`, `tests/surface_parity_smoke.cpp` (matches plan: yes)
**Done-criteria check:** passed (evidence: `/tmp/hawk-implement-plan-verify-inc7.log`)
**Tests added/modified:** `tests/climb_parity_smoke.cpp`, `tests/surface_parity_smoke.cpp`
