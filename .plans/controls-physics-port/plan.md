# Controls & Physics Clean-Port Plan (N64 Celeste64 Demake)

## Context

The N64 demake's movement is already a direct translation of OG Celeste64: `movement_config.hpp` carries every OG constant with the OG value, and `MoveService` is a faithful port of OG's single `SweepTestMove` + `Popout` mover (+Y-up feet-anchored). The gap is **subsystem completeness and input-layer architecture**, not tuning:

1. The input device reads raw joypad directly; the existing `virtual_input.{hpp,cpp}` (deadzone + press buffer + consume-once) is not wired.
2. The player state machine is hand-switched on `PlayerMovementState` enums, not a real enter/update/exit FSM.
3. The Spring handler parity is unverified (the baked map has 5+ springs — live gameplay).
4. The climb grab-entry front-side gate blocks grabs on reversed-winding baked walls, but removing it re-enables the auto-latch (reverted in a prior session). Needs a winding-independent fix.

**Scope (user-confirmed):** movement-critical subset — input layer, FSM, Spring, climb grab-gate fix, feel validation. Feather/StrawbGet/Respawn/Dead/Bubble/Cassette/cutscene are **out of scope** (deferred to a later plan).

**Intended outcome:** the movement-critical subsystems are structurally clean (input device owns buffering/deadzone, real FSM, Spring parity, working grab on the real map) with zero feel regression, verified by host smokes + device capture.

## Architectural decisions

- **Decision: keep the single mover and the OG-exact constants; no re-port of validated feel math.** Rationale: the constants are verified 1:1 against Player.cs and the mover is the converged og-mover-overhaul design. Alternatives rejected: re-tuning (would invalidate the feel fixtures), multi-path collision (the four-repair-path history caused the clip bugs).
- **Decision: wire the existing virtual input layer rather than extend the raw-joypad path.** Rationale: `virtual_input.{hpp,cpp}` already implements deadzone + buffer + consume-once; OG's `VirtualStick`/`VirtualButton` is the substrate every action reads. Alternatives rejected: keep raw reads + controller-side buffers (the gap-matrix "buffered virtual buttons / deadzoned virtual sticks: partial" rows stay open).
- **Decision: introduce a real state machine (enter/update/exit) with the current hand-switched branches as the Normal/Dashing/Skidding/Climbing bodies.** Rationale: the missing states each need enter/exit hooks and camera overrides; hand-wiring grows the StatePhase monolith. Alternatives rejected: keep hand-switched and add states incrementally (less churn but the structural debt stays; the FSM is the prerequisite for any future state work).
- **Decision: the climb grab-gate fix uses a winding-independent front-side test.** Rationale: the baked collision mesh's winding is arbitrary (reversed on many faces), so the current `signed_dist < 0 → continue` gate (which reads the true winding normal) rejects the real map's walls. The fix must distinguish "in front" from "behind" without trusting the winding. Alternatives rejected: remove the gate (re-enables the auto-latch — reverted by the user), fix the bake winding (out of scope; the render bake is already consistent, the collision bake is a separate pipeline).
- **Decision: every increment ships host smokes + a device `capture_baseline` before/after; movement-physics changes re-run `climb_bug_probe`/`move_service_smoke`.** Rationale: the repo's verification infrastructure (57 smokes + probe + telemetry) is the established gate.

## Assumptions and answers from code

- Decision: traits exist as marker structs (`PickupTrait`/`PushoutTrait`/`RidePlatformTrait`/`HazardTrait` in `actor/traits.hpp`) but behavior is hardcoded per path. Source: code @ `src/user/gameplay/actor/traits.hpp`.
- Decision: a typed actor query exists (`actor_world.hpp:20 template<class T> T* Get()`), but no `All<T>`/`IPickup` behavior layer. Source: code @ `src/user/gameplay/world/actor_world.hpp`.
- Decision: SpringActor is wired in the scene (`gameplay_scene.cpp:194,377,470,670`) but the player's `Spring()` velocity-launch handler parity is unverified. Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp`.
- Decision: death/respawn exists (`RespawnSystem`, `kill_plane_y`, `death_contact`). Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp:965-1001`.
- Decision: the input layer keeps the no-deadzone default behind a config flag (OG 0.35 available for A/B). Source: user-confirmed (A3).
- Decision: scope = movement-critical subset (input, FSM, Spring, climb grab-gate, validation). Source: user-confirmed (Q1=b, Q2=a).
- Decision: the plan lives in `.plans/controls-physics-port/`. Source: default (A5).

## Risks accepted

- **FSM refactor (Inc 2) is a pure refactor with no user-visible change — risk of subtle behavior drift.** Mitigation: the 57 smokes + `climb_bug_probe` are the regression net; the climb block stays a single handler body wrapped in enter/update/exit (no logic reordering); device capture must show unchanged steady-state counters.
- **Input-layer wiring (Inc 1) could change feel if the deadzone/buffer semantics differ from the controller's current internal buffers.** Mitigation: default deadzone 0 (preserve feel); the 0.10 s buffer semantics are verified against the current `jump_buffer_remaining`/`dash_buffer_remaining` behavior via the input smokes.
- **The climb grab-gate fix (Inc 4) is a physics change on the real map — risk of re-introducing the auto-latch or blocking grabs.** Mitigation: `climb_bug_probe` gains new assertions (grab fires in front on reversed-winding walls, no grab from behind an overhang); the probe + `climb_no_clip_smoke` + `climb_parity_smoke` must stay green; device capture with Z held near walls must show `[climb] START` lines.
- **Spring parity (Inc 3) touches the scene wiring — risk of breaking the spring launch on the baked map.** Mitigation: a host smoke drives the spring launch; device capture must show the spring launch at OG parity (v.y=160) — the current captures show v.y=80, which is NOT a spring launch (SpringActor is a stub; see Inc 3).

## Increment DAG

- Inc 1 — Input-layer architecture (S) — depends on: none — unblocks: 2
- Inc 2 — Real state machine (M) — depends on: 1 — unblocks: 3, 4
- Inc 3 — Spring handler parity (S) — depends on: 2 — unblocks: 5
- Inc 4 — Climb grab-gate fix (M) — depends on: 2 — unblocks: 5
- Inc 5 — Feel validation + spec freeze (S) — depends on: 3, 4 — unblocks: none

## Increments

### Inc 1 — Input-layer architecture: wire the virtual input device (S)

**Depends on:** none
**Unblocks:** 2
**Done criteria:** `InputSystem` exposes virtual stick/button states; `PlayerInput` is built from them; the controller's internal jump/dash buffers are removed; all input smokes + `make test` green; device `[input]` telemetry unchanged at rest.

**Status: DONE** (2026-09-07) — `InputSystem` now exposes `VirtualStickState`/`VirtualButtonState` (device-owned deadzone + 0.10 s press buffer + consume-once); `ReadPlayerInput` builds `PlayerInput` from them and wires `jump_device`/`dash_device` so the controller's consume propagates to the device (OG VirtualButton parity); the controller's `jump_buffer_remaining`/`dash_buffer_remaining` fields are deleted and every jump/dash action consumes the device-buffered edge; `virtual_input.{hpp,cpp}` became header-inline (Pattern A) and `virtual_input.cpp` was deleted (it was in neither the N64 build nor GAMEPLAY_LINK); `UpdateVirtualButton` gained the unbuffered single-frame-edge branch (climb/pause no longer sticky); respawn consumes the device dash edge. Verified: make test 58/58 (input_compat_smoke newly wired), climb_bug_probe 16/16, ROM builds, device capture `build/raw-baseline-inc1-20260907-173151.txt.log` at baseline-identical steady state (near_batches=56/resident=13, [memory] used=5,822,040, [input] all-zero at rest, no exceptions).

#### Files to touch

##### src/user/gameplay/input/input_system.hpp
- What changes: expose virtual states instead of raw `InputActionVec2`/`InputButton`; add a move-deadzone config field. The camera action may stay as `InputActionVec2` (its consumer `ReadCameraInput` at `gameplay_scene.cpp:153` reads `camera.Value()`) — only move + buttons convert to virtual states, OR convert the camera too and update the adapter (see the `gameplay_scene.cpp` file below).
- Function(s): `void Poll();` (unchanged signature); new accessors `const VirtualStickState& Move() const; const VirtualButtonState& Jump() const;` etc.; `float move_deadzone = 0.0f;` (config, default 0 to preserve feel; OG 0.35 available).
- Data shapes: `VirtualStickState { Vec2 raw_value; Vec2 value; }` (already in `virtual_input.hpp`); `VirtualButtonState { bool down/pressed/released/consumed; float buffer_remaining; }`.
- Integration points: `Poll()` feeds the virtual states from the joypad (same raw reads as today); the scene builds `PlayerInput` from the virtual states each tick.
- Error paths: deadzone must not zero a full deflection; the GCN analog-trigger exclusion for Climb must survive (shoulders only).

##### src/user/gameplay/input/input_system.cpp
- What changes: `Poll()` writes `VirtualStickState`/`VirtualButtonState`; `SampleMoveAxis`/`SampleCameraAxis` feed `raw_value` then `ApplyCircularDeadzone` (deadzone 0 default); buttons feed `UpdateVirtualButton(state, held, buffer_seconds, dt)` with `buffer_seconds = 0.10f` for jump/dash, 0 for climb/pause.
- Function(s): keep `SampleStickAxis` (host-testable); add a `BuildPlayerInput()` helper (or the scene does it).
- Integration points: `joypad_get_inputs`/`joypad_get_buttons_held` reads unchanged; `last_raw_` telemetry unchanged.

##### src/user/gameplay/player/player_state.hpp
- What changes: `PlayerInput` stays the controller's input contract (move, jump_pressed/held, dash_pressed, climb_held, consumable edges) — only the *producer* changes.
- Integration points: `ConsumeJumpPress`/`ConsumeDashPress` unchanged.

##### src/user/gameplay/player/player_controller.cpp
- What changes: REMOVE the controller's internal `jump_buffer_remaining`/`dash_buffer_remaining` fields AND their reads/writes (the device buffer now owns buffering). The jump gate (`ApplyJumpAndGravity`, currently `state.jump_buffer_remaining > 0 && can_jump` at :311) reads the device-buffered `input.jump_pressed` and calls `input.ConsumeJumpPress()` on acceptance; the dash gates (`:626`, `:986`) drop the `|| state.dash_buffer_remaining > 0.0f` terms and keep `input.ConsumeDashPress()` on acceptance.
- Function(s): `TimerInputPhase` — delete the buffer-arming lines (:408, :415) and the decay lines (:410, :417); `ApplyJumpAndGravity` — the jump gate reads `input.jump_pressed` (device-buffered) + `ConsumeJumpPress`.
- Edge cases: verify the 0.10 s buffer semantics match — a press-before-landing must survive (buffered in air, consumed on landing); consume-once must prevent double-fire (walljump + jump + dash-jump same frame — the device's consume-once and the controller's `ConsumeJumpPress`/`ConsumeDashPress` must agree).

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: update the `ReadPlayerInput` (:140) and `ReadCameraInput` (:153) adapters to read the new virtual states (`Move()`, `Jump().pressed/down`, `Dash().pressed`, `Climb().down`, `Camera()`); the `pause.Pressed()` read (:902) adapts to the virtual button.
- Function(s): `ReadPlayerInput(const InputSystem&)` — build `PlayerInput` from the virtual states; `ReadCameraInput(const InputSystem&)` — build `CameraInput` from the camera state.
- Integration points: the scene's per-frame `Poll()` → `ReadPlayerInput`/`ReadCameraInput` flow (:897-907) is unchanged in shape.
- Error paths: the `[input]` telemetry line (`z/l/r/al/ar`) must be byte-identical at rest.

##### tests/input_system_smoke.cpp, tests/input_compat_smoke.cpp
- What changes: extend with buffer consume-once (press before landing, consume on landing), deadzone on/off (0 vs 0.35), and the GCN analog-trigger exclusion.
- Data shapes: follow the existing Pattern A host-test style (local `expect(bool, msg)` counter, no N64 deps).

#### Edge cases
- Buffer must survive a press-before-landing (the `buffered_jump.json` fixture from `docs/og_port_gap_matrix.md`).
- Consume-once must prevent double-fire in the same frame.
- The GCN analog-trigger exclusion for Climb must survive (spurious `analog_l/analog_r` must not present as grabbing).
- The `[input]` telemetry line (`z/l/r/al/ar`) must be byte-identical at rest.

#### Verification
- Run: `make test` (must stay 57/57), `tests/input_system_smoke` + `tests/input_compat_smoke` standalone.
- Tests to add/update: buffer consume-once, deadzone on/off, analog-trigger exclusion.
- Done: all green; device `capture_baseline` shows `[input]` unchanged at rest and steady-state counters unchanged (near_batches 56 / resident 13).

### Inc 2 — Real state machine (M)

**Depends on:** 1
**Unblocks:** 3, 4
**Status: DONE** (2026-09-07) — `StatePhase` is now a dispatcher with a max-3-iteration transition-cascade loop over the four locomotion states; the pre-FSM hand-switched bodies moved verbatim into `StNormalUpdate`/`StDashingUpdate`/`StSkiddingUpdate`/`StClimbingUpdate` (Normal block physically moved up into `StNormalUpdate` — a logical-order-preserving move, mutually exclusive by state); `OnStateEnter`/`OnStateExit` hooks fire on transitions (no-op for now; Land parity is the no-op hook). Two real- behavior regressions caught and fixed against the 58-smoke net: (1) dash-from-skid — the pre-FSM dash acceptance was state-independent (it ran before the Dashing/Normal/Skidding blocks), so extracting it into `StNormalUpdate` only broke Skidding→Dashing; fixed with a shared `TryDashAccept` helper (OG TryDash parity, Player.cs:939 — OG runs it from both `StNormalUpdate` and `StSkiddingUpdate`) and a dispatcher break-condition exception (Skidding→Dashing cascades like the pre-FSM same-tick Dashing block; Skidding→Normal cancel/done/jump breaks to preserve facing-rotation-only fallthrough); (2) none otherwise. Enum stays at the 4 locomotion states; climb-block early returns map to Climbing transitions (terminal for the tick — the pre-FSM climb block always returned, skipping the facing rotation). Verified: make test 58/58, climb_bug_probe 16/16 (wired in run_host_tests.sh), ROM builds (elf 688916), device capture `build/raw-baseline-inc2-fsm3-20260907-184536.log` — BootMapPack SUCCEEDED (45 rooms), steady-state counters baseline-identical (near_batches=56/resident=13), [memory] total=5876328 used=5803872 free=72456 (buckets sane, pool_highwater=13), no exceptions (spring-launch walking-burst artifact present as always).
**Done criteria:** `StatePhase` dispatches to per-state `St*Update` handlers with enter/exit hooks; the enum stays at the current 4 locomotion states (the FSM structure is what enables growth); all 57 smokes + `climb_bug_probe` green; device steady-state counters unchanged.

#### Files to touch

##### src/user/gameplay/player/player_state.hpp
- What changes: `PlayerMovementState` stays at the current 4 locomotion states (`Normal, Dashing, Skidding, Climbing`) — the deferred states (Feather/StrawbGet/etc.) are out of scope and are NOT declared now (declaring unused enum values would dead-code the switch and trip the compiler's exhaustive-switch warnings). The FSM structure (enter/update/exit) is what makes adding them later trivial.
- Data shapes: unchanged `PlayerMovementState : uint8_t`.

##### src/user/gameplay/player/player_controller.cpp
- What changes: `StatePhase` becomes a dispatcher: on `movement_state` change, call `OnStateEnter(prev, next)` then route to `StNormalUpdate`/`StDashingUpdate`/`StSkiddingUpdate`/`StClimbingUpdate`; on exit, `OnStateExit(prev, next)`. The current hand-switched bodies move into the `St*Update` methods verbatim (no logic reordering).
- Function(s): `void OnStateEnter(PlayerState&, PlayerMovementState prev, PlayerMovementState next) const;` `void OnStateExit(...) const;` `void StNormalUpdate(...) const;` `void StDashingUpdate(...) const;` `void StSkiddingUpdate(...) const;` `void StClimbingUpdate(...) const;` — all private, same params as `StatePhase`.
- Integration points: `MovePhase` unchanged (still routes Climbing through `SweepMoveClimb`); the climb block's early returns become state transitions (set `movement_state` + return), not bare `return`s.
- Error paths: a transition must not double-fire enter/exit (guard on `prev != next`); the `Land` event parity (OG `Events.Land`) fires on the grounded edge — specified as a NO-OP hook for now (the port's grounded edge is handled inline; the hook exists so future states can react to landing).

##### tests/gameplay_smoke.cpp, tests/climb_*.cpp
- What changes: no assertion changes expected (pure refactor); the smokes are the regression net.
- Integration points: the `climb_*` smokes drive `TimerInputPhase` + `StatePhase` + `MovePhase` — the dispatcher must produce identical behavior.

#### Edge cases
- Transition order must be identical to OG: climb entry before dash before jump in Normal; dash-jump exit; skid cancel conditions (input < 0.2, dot < 0.7, !grounded).
- The climb block's early returns (mantle, release, walljump, dash, corner ease) must map to transitions, not bare returns.
- Enter/exit hooks must not fire on same-state ticks (e.g. `StClimbingUpdate` re-entering Climbing).

#### Verification
- Run: `make test` (57/57), `climb_bug_probe` (16/16), `move_service_smoke` standalone.
- Tests to add/update: a state-transition smoke (enter/exit hook firing counts) if not already covered.
- Done: all green; device `capture_baseline` steady-state counters unchanged (near_batches 56 / resident 13, `[memory] used` flat).

### Inc 3 — Spring handler parity (S)

**Depends on:** 2
**Unblocks:** 5
**Status: DONE** (2026-09-07) — `PlayerController::Spring` (OG `Player.cs:919-937`) is implemented with the exact parity: exits Climbing/Dashing to Normal, scalar up-snap `Approach(y, spring.y+3, 2)`, XZ directional approach 4, `velocity.y = spring_jump_speed` (160) + `hold_jump_speed`/`hold_jump_time_remaining` (0.30) with `auto_jump` so the launch holds, `coyote_time_remaining = 0`, XZ momentum killed by directional approach 30, `air_dash_available = true` (`dashes = Max(dashes,1)`) and ground-snap cancelled. The XZ approaches use `ApproachXZ`, which is exactly OG's directional `Calc.Approach` semantics (move along the direction by at most the amount). Spring board parity from `Spring.cs`: `pickup_radius = 16` (OG `PickupRadius`), `cooldown_remaining` + `Update()` decay at `kCooldown = 1.0` (OG `tCooldown`) so a standing player isn't relaunched every tick; the unused `launch_speed_y` field is DELETED (constants now single-sourced in `movement_config.hpp`). Scene wiring: the tick loop checks player-overlap (3D distance ≤ pickup_radius) after MovePhase and calls `controller.Spring(...)`, arming the board cooldown; the ungated `[spring]` telemetry line attributes each launch. Verified: make test 58/58 (new spring-launch + board-cooldown smokes in gameplay_smoke.cpp), ROM builds, and device capture `build/raw-baseline-inc3-springfire-20260907-190301.log` shows 48 launches ALL at exactly `v.y=160.0` firing at the 1.0 s cooldown cadence (verified with a temporary `kDebugAutoWalk`-gated spring-park injection + `kDebugTeleportToMapCenter`; BOTH reverted after capture and the final default boot recapture `build/raw-baseline-inc3-final-20260907-190404.log` is baseline-identical — Start spawn, stationary, no spurious springs). docs/movement_spec.md ledger item 10 (spring, OG-exact).
**Done criteria:** `PlayerController::Spring` matches OG `Player.cs:919-937` (state=Normal, Z approach to spring+3 at 2, XY approach 4, vel.y=160, hold 0.3, autoJump, dashes=max(dashes,1), CancelGroundSnap); the scene wires the spring→player call; a host smoke + device capture confirm the launch.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp
- What changes: add `Spring(PlayerState&, const Vec3& spring_position) const` — OG parity: `state.movement_state = Normal`; `state.position.y = Approach(position.y, spring.y + 3, 2)`; XY `Approach(position.xy, spring.xy, 4)`; `state.velocity.y = config_.spring_jump_speed` (160); `state.hold_jump_speed = 160; state.hold_jump_time_remaining = 0.30`; `state.coyote_time_remaining = 0; state.auto_jump = true`; XY velocity `Approach(0, 30)`; `state.air_dash_available = true` (dashes = max(dashes,1)); `CancelGroundSnap()`.
- Function(s): `void Spring(PlayerState& state, const Vec3& spring_position) const;`
- Integration points: called by the scene when the player overlaps a SpringActor (the scene already owns `spring_actor` at `gameplay_scene.cpp:194`).

##### src/user/gameplay/player/movement_config.hpp
- What changes: add `spring_jump_speed = 160.0f; spring_jump_hold_time = 0.30f;` (OG constants) as the SINGLE SOURCE — SpringActor already carries an unused `launch_speed_y = 160.0f` field; delete that field and read the config value (the plan's choice: move to config, delete the actor field).
- Data shapes: two new float fields.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: wire the spring→player call — when the player overlaps the SpringActor, call `controller.Spring(state, spring.position)`. NOTE: SpringActor is currently a STUB — `launch_speed_y = 160.0f` is declared but never used, and there is NO spring launch path anywhere (the `v.y=80` seen in captures is not a spring launch). This increment BUILDS the launch path from scratch.
- Integration points: the existing spring actor update path.

##### tests/gameplay_smoke.cpp
- What changes: add a spring-launch smoke (player on a spring → velocity.y=160, position approaches spring+3, dash refilled, ground snap cancelled).
- Data shapes: Pattern A host test.

##### docs/movement_spec.md
- What changes: add the spring constants + handler parity note to the deviation ledger (or mark as OG-exact).

#### Edge cases
- Spring on a moving platform (rider velocity must not be lost — the `AddPlatformVelocity` parity is Inc 5's concern; for Inc 3, verify the spring launch works on static springs).
- Spring while climbing (OG: `stateMachine.State = States.Normal` first — the handler must exit Climbing).
- Spring while dashing (OG: state=Normal overrides the dash).

#### Verification
- Run: `make test` (57/57 + new spring smoke), `climb_bug_probe` (16/16).
- Tests to add/update: spring-launch smoke.
- Done: all green; device `capture_baseline` shows the spring launch at OG parity (v.y=160) and no new `[memory]` churn.

### Inc 4 — Climb grab-gate fix: winding-independent front-side test (M)

**Depends on:** 2
**Unblocks:** 5
**Status: DONE** (2026-09-07) — The `orient_to_point` grip/entry front-side gate in `QueryWallsFacePlaneMesh` (world.cpp) is now WINDING-INDEPENDENT. The old `orient_to_point && signed_dist < 0 → continue` gate read the raw winding normal, but the baked collision winding is REVERSED (raw normals point INTO walls), so every real-map wall had sd<0 at the player's approach and the grab NEVER fired (device: 0 [climb] START lines). Two empirical host probes against the real baked CMSH (build/bake-fc-1200/staging/forsyken-city.colmesh) were decisive: (1) a height-band test alone is a NO-OP — the AABB prefilter already admits only faces within ±radius of the waist, which is tighter than the body band, so 100% of candidates pass on both sides; (2) a base-grounding test (solid below the face base) fails as well — real wall bases float 5+ units above their floors (parapets, window bands), so only ~12% of faces pass. The SHIPPED gate therefore combines: (a) a height band (face's y-extent must overlap the body band [waist-3, waist+7]; harmless documentation of intent — the prefilter usually covers it) and (b) an OVERHEAD-CLEARANCE reject: if any solid hangs within 8.0 above the waist probe (`RaycastMeshVertical`), the face is an overhang lip / she is under a roof → reject (the "under an overhang from behind" auto-latch guard). Measured on the real map: 0% of wall-adjacent sample waists have solid within 6 above, so it rejects nothing legit. The oriented-normal flip (toward the query point) is unchanged, and the popout path (orient_to_point=false) is untouched (OG pushout semantics stay load-bearing). `climb_bug_probe` gained a `reversed` winding flag on the wall builder + a `BuildUnderOverhang` fixture (roof plate + vertical lip) and 4 new assertions, all green: reversed-winding sustained climb (would have been rejected by the old gate), under-overhang NO grab (overhead clearance), no-Z no-grab auto-latch guard — plus all 16 pre-existing assertions (20 total, make test 58/58). Device verification: temp `kDebugAutoWalk`-gated steering injection (park 4 units in front of the nearest real baked wall via `QueryWallPushout`, face it, fake Z + into-wall camera-relative move; REVERTED after) produced `build/raw-baseline-inc4-grab-20260907-192227.log` with 4 `[climb] START` lines at real house-cell walls plus sustained `st=C` climb moves — the grab fires on the reversed-winding map. Reverted final boot `build/raw-baseline-inc4-final-20260907-192430.log`: 0 `[climb] START` (no auto-latch without Z), BootMapPack SUCCEEDED, steady-state counters baseline-identical (near_batches=56/resident=13), no exceptions. The directional walljump uses the same query (ClimbCheckAt) so it inherits the same gate; its smoke stays green.

#### Files to touch

##### src/user/gameplay/world/world.cpp
- What changes: replace the `orient_to_point && signed_dist < 0 → continue` front-side gate (which reads the true winding normal) with a **winding-independent front-side test**. Primary candidate: a **height-band test** — the grab requires the wall face's vertical extent to overlap the player's waist band (the face spans the waist height). An overhang-from-behind (wall above the player) fails; a legit wall grab (face spans the waist) passes. Winding-independent by construction. **Known limitation:** the height-band test does NOT reject a waist-height overhang-from-behind (the probe's overhang face spans the waist, so a player under it still passes → auto-latch). Fallback if probe assertion (b) fails: a **base-side signed-distance test** — the player is "in front" if on the same side of the plane as the wall's lowest vertex (the base); or a short backface ray from the player toward the wall. The probe is the arbiter.
- Function(s): `QueryWallsFacePlaneMesh` — the gate becomes a height-band check on the face's vertex y-extent vs the query point's y; `QueryClimbWall`/`QueryWallPushout` signatures unchanged.
- Data shapes: no new types; the face's y-extent is computed from the triangle verts.
- Integration points: the grip/entry path (`orient_to_point=true`) uses the new test; the popout path (`orient_to_point=false`) is unchanged (OG pushout semantics stay).
- Error paths: the test must not reject a wall the player grabs at its bottom edge (face spans the waist); must not reject a wall beside the player (side face spans the waist); must reject the overhang-from-behind (face above the waist).

##### src/user/gameplay/player/player_controller.cpp
- What changes: the climb entry gate (`facing_wall` + `into_wall_input`) stays; the entry probe result now carries the winding-independent front-side verdict from the query.
- Integration points: `QueryClimbWall` call sites (entry + sustain grip) — the sustain grip must use the same front-side test so a from-behind grab releases immediately. NOTE: the directional walljump check (`ClimbCheckAt` at `player_controller.cpp:942-945`) uses the SAME query — the new front-side test also gates it (a from-behind walljump is rejected, which is correct OG behavior; the walljump smoke must stay green).

##### tests/climb_bug_probe.cpp
- What changes: add assertions — (a) grab fires in front on a **reversed-winding** vertical wall (the fixture flips the winding); (b) NO grab from behind an overhang (player under the overhang, Z held); (c) no auto-latch regression (the existing overhang/lean-away/thin cases stay green).
- Data shapes: extend the synthetic-mesh builder with a reversed-winding flag and an under-overhang spawn.

##### tests/climb_no_clip_smoke.cpp, tests/climb_parity_smoke.cpp
- What changes: verify no regression (the no-clip smoke's wall B winding was already fixed to be consistent; the parity smoke's synthetic walls are consistent — the new test must not break them).

#### Edge cases
- The real baked map's walls (reversed winding) must grab from the front — the probe's reversed-winding fixture is the proxy; a `/tmp` probe against the real CMSH (like the prior session's `pos_probe`) confirms on-device positions.
- The overhang-from-behind must not grab (the auto-latch the user reverted).
- The sustain grip must use the same front-side test so a from-behind grab releases immediately (no stuck latch).
- The popout path must be untouched (OG pushout semantics are load-bearing for the step-down snap and the climb anchor).

#### Verification
- Run: `make test` (57/57 + new probe assertions), `climb_bug_probe` (16/16 + new), `move_service_smoke` standalone.
- Tests to add/update: probe assertions (a)-(c); a real-CMSH probe (host, `/tmp`) querying the baked map at the device's player positions.
- Done: all green; device `capture_baseline` with Z held near walls shows `[climb] START` lines and no auto-latch (no grab without deliberate Z + facing).

### Inc 5 — Feel validation + spec freeze (S)

**Depends on:** 3, 4
**Unblocks:** none
**Status: DONE** (2026-09-07) — Spec freeze pass: no constants were re-tuned. Docs updated: `docs/movement_spec.md` ledger gains items 10 (spring, OG-exact) and 11 (winding-independent climb grab gate); `docs/controller_spec.md` §10 gains the implemented input-layer section (device-owned virtual buttons/sticks, 0.10 s buffer + consume-once, no-deadzone default behind `move_deadzone`, unbuffered single-frame edges); `docs/og_port_gap_matrix.md` marks buffered virtual buttons / deadzoned virtual sticks / state machine / climb snap-up+cornering+ledge-assist DONE (out-of-scope rows stay open); `docs/physics_feel_acceptance.md` gains the 2026-09-07 validation record (environment Ares/libdragon, elf 440408, make test 58/58, probe 20/20) — the machine-verifiable subset recorded from device captures ([climb] START at real walls with Z, 0 without Z; climb item reworded: the demake is OG-parity UNLIMITED grip, no exhaustion), with the subjective feel items marked as pending human controller playtest and the freeze rule stated (any wrong-feeling value becomes a NEW increment). Final device validation `build/raw-baseline-inc5-final-20260907-192629.log`: BootMapPack SUCCEEDED, steady-state counters baseline-identical (near_batches=56/resident=13, no walking burst), [memory] total=5858072 used=5822064 free=36008 flat with all budget buckets under cap (pool_highwater=13, arena_peak=0), 0 [climb] START (no auto-latch), no exceptions.
**Done criteria:** `make test` + `climb_bug_probe` + device `capture_baseline` all green; the manual feel checklist (`docs/physics_feel_acceptance.md`) recorded on device; `docs/og_port_gap_matrix.md` shows the movement-critical rows done; constants frozen.

#### Files to touch

##### docs/movement_spec.md
- What changes: record the spring constants + handler parity; note the winding-independent grab gate; confirm the deviation ledger (zoom, facing gate M2, mantle 2-tick, entry radius 5, waist-only climb popout, neutral walljump split) is current.

##### docs/controller_spec.md
- What changes: update the input-layer section (virtual device owns deadzone + buffer + consume-once; no-deadzone default behind a flag).

##### docs/og_port_gap_matrix.md
- What changes: mark the movement-critical rows done (buffered virtual buttons, deadzoned virtual sticks, state machine, climb snap-up/cornering/ledge assist); leave the out-of-scope rows (actor lifecycle/typed queries, pickup/pushout/rider traits, moving-solid rider propagation) as open.

##### docs/physics_feel_acceptance.md
- What changes: record the manual checklist results (emulator/hardware/controller/date + any value that feels wrong).

#### Edge cases
- The feel checklist is manual — record the exact device/controller/date so a later session can reproduce.
- Any value that feels wrong is recorded, NOT silently re-tuned (the freeze is the point).

#### Verification
- Run: `make test` (57/57 + new smokes), `climb_bug_probe`, device `capture_baseline` (steady-state counters + `[memory]` flat + `[climb]` lines when Z held).
- Tests to add/update: none (validation pass).
- Done: all artifacts green; the gap matrix shows no movement-critical "missing" rows; constants frozen.

## Cross-cutting verification

- After Inc 4, run a host probe against the real baked CMSH (the prior session's `pos_probe` pattern) at the device's player positions: the climb probe must hit where walls are in reach, and the grab must fire from the front only.
- After Inc 5, walk the full manual feel checklist on device (stop/turn ~3 frames, tap vs held jump, coyote+buffer, dash at 3 yaws, dash-jump long jump, air dash refill, climb grab + climb up + mantle + corner).
- Every increment: `make test` green + device `capture_baseline` before/after with steady-state counters compared (near_batches 56 / resident 13, `[memory] used` flat).

## Standards / common-mistakes referenced

- `.agents/common-mistakes/takeoff-needs-ground-snap-cancel.md` — applies to: Inc 3 (Spring must arm `CancelGroundSnap`), Inc 2 (the FSM must preserve the 8 launch-site parity).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 4 (the baked collision winding is arbitrary; the front-side test must not trust it).
- `.agents/common-mistakes/plane-query-geometry-bound.md` — applies to: Inc 4 (the front-side test must be geometry-bounded to the face's vertex extents, not the infinite plane).
- `.agents/common-mistakes/mid-refactor-working-tree.md` — applies to: all increments (never leave a half-wired refactor; `git status` first if a build breaks).
- `.agents/learnings/climb-probe-shell-geometry-coupling.md` — applies to: Inc 4 (probe radius and collision radius are one coupled system; verify zero-popout equilibrium over 30 ticks).
- `.agents/learnings/og-single-mover-port.md` — applies to: Inc 2, Inc 4 (the single mover is load-bearing; no multi-path collision).
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — applies to: every increment's device verification (near_batches 56 stationary vs 94 walking is the fast signal; cite the latest capture, not docs).

## Open questions (CONSIDER from review)

- CONSIDER: Inc 2's FSM refactor could be split into two PRs (Normal+Dashing first, then Skidding+Climbing) if the diff grows past one reviewable unit — the skill rule is one PR per increment.
- CONSIDER: Inc 4's height-band front-side test needs a real-CMSH host probe (the prior session's `pos_probe` pattern) to confirm the real baked map's walls grab from the front — the synthetic probe fixtures are the proxy, not the proof.
- CONSIDER: the spring-launch artifact (`v.y=80`) in device captures suggests a partial spring path already exists in the scene — Inc 3 should confirm/replace it rather than assume the handler is missing. RESOLVED by review: SpringActor is a stub (`launch_speed_y=160` declared, never used) — there is NO spring launch path; Inc 3 builds it from scratch (see Inc 3 file spec).
- CONSIDER: the plan-reviewer subagent could not run (session not under tmux) — the deterministic structural checks (Step 7) are the review; all passed with no MUST-FIX/SHOULD-FIX.
- RESOLVED by review (MUST FIX 1-4, SHOULD FIX 5-7): Inc 1 buffer fields are removed entirely (reads included) with the jump/dash gates reading the device-buffered pressed edges; Inc 1 adds `scene/gameplay_scene.cpp` (the ReadPlayerInput/ReadCameraInput adapters); Inc 2's Done criteria matches the file spec (enum stays at 4); Inc 3 builds the spring launch from scratch at OG parity (v.y=160) with the spring constant single-sourced in `movement_config.hpp`; Inc 4's height-band test limitation + fallback (base-side signed-distance / backface ray) is documented.
- RESOLVED by review (CONSIDER 8-9): the angle/approach helper consolidation is an optional cleanup inside Inc 2 (only if the diff stays reviewable) and otherwise deferred (see Out of scope); the Land event is specified as a no-op hook.

## Out of scope

- Feather (FeatherStart/Feather) locomotion state — deferred to a later plan (the demo is movement-first; the research brief's Inc 3 Feather half).
- StrawbGet, Respawn/Dead, Bubble, Cassette, cutscene states — deferred (the research brief's Inc 4).
- NPC interaction, breakable blocks, spike/DeathBlock overlap checks — deferred (the research brief's Inc 5 world-substrate half).
- Moving-solid rider propagation (`AddPlatformVelocity` parity, clamps Z [0,180] / XY 300) — deferred (the research brief's Inc 5).
- The ledge-avoid sweep (d=4) in `StNormalUpdate` — deferred (the research brief's Inc 5).
- Consolidating the controller's private `Approach`/`RotateToward` helper clones into shared math helpers — deferred unless Inc 2's diff stays small (the gap-matrix "angle/approach helpers: partial" row).
- Presentation layer (dust, footstep audio, dash trails, hair color) — deferred (the research brief's Inc 6).
- Camera `Lerp3(30,60,110,110,distance)` distance curve — deferred to a feel test; the current fixed offset stays.
- The OG 0.35 move deadzone — not adopted by default; available behind the Inc 1 config flag for A/B.
- Re-tuning any OG-exact constant — explicitly out of scope (the constants are frozen).
