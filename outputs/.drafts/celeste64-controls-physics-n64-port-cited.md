# Celeste64 Controls & Physics → N64: Spec and Clean-Port Plan

**Slug:** `celeste64-controls-physics-n64-port`
**Date:** 2026-09-07
**Mode:** lead-owned direct research (subagent + web_search blocked; repo-local + web_fetch evidence)

---

## Executive summary

The N64 demake's movement is already a **direct translation** of OG Celeste64, not a re-derivation: `movement_config.hpp` carries every OG constant with the OG value (run 64, accel 500, friction 800, gravity 600, jump 90, dash 140, coyote 0.12, buffers 0.10, climb 40, etc. — all confirmed against [Player.cs constants](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)), and `MoveService` is a faithful port of OG's single `SweepTestMove` + `Popout` mover with the documented +Y-up feet-anchored conversion. The gap is not tuning — it is **subsystem completeness and input-layer architecture**:

1. **Input layer:** OG's Foster `VirtualStick` (0.35 deadzone) + `VirtualButton` (0.1 s buffer, consume-once) is the substrate every action reads ([Controls.cs](https://github.com/EXOK/Celeste64/blob/main/Source/Data/Controls.cs)). The port has a `virtual_input.hpp/cpp` implementation but the live `InputSystem` reads raw joypad directly — the virtual layer is not wired, and jump/dash buffering is re-implemented inside the controller instead of in the input device.
2. **State machine:** OG uses a real `StateMachine<States,Events>`; the port hand-switches on `PlayerMovementState` enums. This is the biggest structural debt — every new state (Feather, Spring, StrawbGet, Respawn, Dead, Bubble, Cassette) currently means more hand-wired branches.
3. **Missing locomotion states:** Feather (FeatherStart/Feather), Spring handler parity, StrawbGet, Respawn/Dead, Bubble, Cassette, cutscene, NPC interaction, breakable blocks, spike/DeathBlock overlap checks, the ledge-avoid sweep (d=4) in `StNormalUpdate`, moving-solid rider propagation, and the presentation layer (dust, footstep audio, dash trails, hair color).
4. **Climb parity is deep but carries documented deviations** (facing-dot gate M2, mantle 2-tick confirm, entry probe radius 5 vs OG 4, waist-only climb popout, neutral walljump split) — each is a deliberate, documented choice; the plan should keep them unless a feel test says otherwise.
5. **Verification infrastructure is strong** (57 host smokes + `climb_bug_probe` + device `capture_baseline` + `[climb]`/`[memory]` telemetry) — every increment can ship with host + device evidence.

The port plan below is a dependency-ordered DAG of increments that closes the gap **cleanly** (small, testable, host-smoke + device-capture per increment), preserving the gameplay/ROM separation and the single-mover architecture.

---

## Part 1 — The control & physics spec (authoritative target)

### 1.1 Control scheme (N64 physical mapping)

Derived from OG [ControlsConfig.cs](https://github.com/EXOK/Celeste64/blob/main/Source/Data/ControlsConfig.cs) defaults + the existing `input_system.cpp` mapping (user-confirmed):

| Action | OG default (ControlsConfig.cs) | N64 mapping (current) | Status |
| --- | --- | --- | --- |
| Move | Left joystick, deadzone 0.35 | Left stick, `kStickMax=80`, **no deadzone** | gap: deadzone |
| Camera | Right joystick, deadzone 0.35 | C-Stick + C-Buttons, `90` | gap: deadzone |
| Jump | South/North (A/B on Nintendo) | A (+ GCN Y) | ok |
| Dash | West/East (X/Y on Nintendo) | B (+ GCN X) | ok |
| Climb | L/R shoulders + L/R triggers (0.4) | Z / L / R shoulders only | ok (deliberate: GCN analog triggers excluded) |
| Pause | Start/Guide/Back | Start | ok |

**Spec decisions:**
- **Move deadzone:** OG applies a 0.35 circular deadzone in the virtual stick. The port deliberately has none (pre-overhaul feel). The spec should keep the port's no-deadzone move (N64 sticks are coarse; a deadzone would eat the small deflections the analog-magnitude map needs) but **document it as a deviation** and gate it behind a config flag so a feel test can A/B it.
- **Consume-once edges:** jump/dash presses must be consumable (already implemented via `PlayerInput::ConsumeJumpPress/ConsumeDashPress`). The spec requires the *input device* to own the 0.10 s press buffer (OG `VirtualButton`), not the controller — see Inc 1.
- **Climb is a held state** (`Down`), not an edge — already correct.

### 1.2 Camera-relative movement

OG `RelativeMoveInput` ([Player.cs:597](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)): zero input → zero; `forward = (Camera.LookAt - Camera.Position).XY().Normalized()` (fallback `targetFacing`); `side = rotate(forward, 90°)`; `input = -Move.Value.Normalized()`; snap to `UnitY` when `dot(input, UnitY) >= 0.985`; result `forward*input.Y + side*input.X`.

The port implements this in `TimerInputPhase` (`RelativeMoveInput(input.move, camera_forward, state.target_facing)`). **Spec: keep, but verify the 0.985 forward-snap and the `-Move` negation are present** (the port's `SampleStickAxis` already negates X; the Y sign must be checked against OG's `-Move.Value`).

### 1.3 Movement constants (single source of truth)

`movement_config.hpp` is already the single source (MovementProfile deleted). **Spec: freeze these as the canonical values** — they are OG-exact (verified against [Player.cs constants](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)):

- Ground: `run_speed=64`, `acceleration=500`, `past_max_deceleration=60`, `friction=800`, `rotate_threshold=12.8`, `rotate_speed=9.42`, `rotate_speed_above_max=3.77`, `skid_dot_threshold=-0.7`, `skidding_start_accel=300`, `skidding_accel=500`, `end_skid_speed=51.2`, `skid_jump_speed=120`, `skid_jump_hold_time=0.16`, `skid_jump_xy_speed=89.6`, `no_skid_jump_time=0.10`.
- Air: `air_accel_mult_min=0.5`, `air_accel_mult_max=1.0`, `air_friction_mult=0.1`, `gravity=600`, `max_fall_speed=-120`, `half_gravity_threshold=100`.
- Jump: `jump_speed=90`, `jump_hold_time=0.10`, `jump_xy_boost=10`, `coyote_time=0.12`, `jump_buffer_time=0.10`.
- Dash: `dash_speed=140`, `dash_end_speed_multiplier=0.75`, `dash_duration=0.20`, `dash_reset_cooldown=0.20`, `dash_cooldown=0.10`, `dash_rotate_speed=1.885`, `dash_hitstop_time=0.02`, `dash_jump_speed=40`, `dash_jump_hold_speed=20`, `dash_jump_hold_time=0.30`, `dash_jump_xy_boost=16`, `no_dash_jump_time=0.10`, `dash_buffer_time=0.10`.
- Wall/climb: `wall_jump_speed_x=83.2`, `wall_jump_speed_y=90`, `climb_speed=40`, `climb_hop_up_speed=80`, `climb_hop_forward_speed=40`, `climb_hop_no_move_time=0.25`, `climb_cooldown=0.30`, `climb_check_dist=4`, `climb_pushout_dist=3`, `climb_corner_ease_time=0.20`, `wall_probe_radius=5` (entry visibility, documented deviation).
- Spring: `spring_jump_speed=160`, `spring_jump_hold_time=0.30` (OG constants; the port's SpringActor exists but the player handler parity must be verified — see Inc 3).
- Feather: `feather_start_time=0.4`, `feather_fly_speed=100`, `feather_start_speed=140`, `feather_turn_speed=4.71`, `feather_accel=60`, `feather_duration=2.2`, `feather_exit_xy_mult=0.5`, `feather_exit_z_speed=60` (NOT yet in the port — Inc 3).

### 1.4 State machine (target structure)

OG states ([Player.cs](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)): `Normal, Dashing, Skidding, Climbing, StrawbGet, FeatherStart, Feather, Respawn, Dead, StrawbReveal, Cutscene, Bubble, Cassette`. Only Normal/Dashing/Skidding/Climbing/FeatherStart/Feather have locomotion.

**Spec: introduce a real state machine** (enter/update/exit per state) matching OG's structure, with the port's current hand-switched branches as the Normal/Dashing/Skidding/Climbing bodies. This is the structural prerequisite for the missing states. The `PlayerMovementState` enum grows to the full OG set; `LocomotionState` stays as the animation/telemetry projection.

### 1.5 Collision (single mover)

OG: `SweepTestMove(velocity*dt, tNoMove<=0)` → substeps of 2.0 with `Popout(resolveImpact)` after each, then one idle `Popout(false)` ([Player.cs:642](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)); `Popout` order = ground → ceiling → wall (waist then head, radius 3); `GroundCheck` ray down 5.01 from feet+5 with `FloorNormalCheck`; `CeilingCheck` ray up 11 from feet+1; `GroundSnap` ray down 5 gated by `tGroundSnapCooldown` (0.10, armed by `CancelGroundSnap` at every launch site).

The port's `MoveService` implements all of this 1:1 (+Y-up). **Spec: keep the single mover; the only collision deviations are the climb-specific `SweepMoveClimb` (waist-only popout) and the entry snap without the 3-unit/axis cap — both documented, both verified by `climb_bug_probe`.**

### 1.6 Camera

OG: orbit 4 rad/s (invert X), zoom 1.0 normalized-distance/s clamped [0,1] with 1/3 detent snap, follow `1-Pow(0.01,dt)`, vertical origin `1-Pow(0.001,dt)`, FOV `ClampedMap(speed, 76.8, 120, 1, 1.2)` approached at `dt/4`, distance offset `Lerp3(30,60,110,110,distance)` ([Player.cs:263-283, 470-478, 551](https://github.com/EXOK/Celeste64/blob/main/Source/Actors/Player.cs)).

Port: orbit 4, follow 0.01, vertical 0.001, FOV map 76.8→120 → 1→1.2 — all OG-exact. **Deviations (deliberate, documented):** `zoom_rate=1.8` (vs 1.0) and `zoom_snap_speed=1.0` (vs 0.5) for the stick medium; near plane 5 (renderer constraint). **Spec: keep the deviations, but add the OG `Lerp3` distance curve if the current fixed offset feels wrong on device** (verify against `camera_min_distance_smoke`).

### 1.7 Feel acceptance (manual gate)

`docs/physics_feel_acceptance.md` is the manual ROM checklist (stop/turn ~3 frames, tap vs held jump, coyote+buffer, dash at 3 yaws, dash-jump long jump, air dash refill, climb). **Spec: every increment that touches feel must re-run this checklist on device before being marked done.**

---

## Part 2 — Clean-port plan (DAG of increments)

Conventions: each increment ships host smokes + a device `capture_baseline` before/after; no movement-physics changes to the single mover without a `climb_bug_probe`/`move_service_smoke` run; gameplay/ROM separation preserved.

### Inc 1 — Input-layer architecture: wire the virtual input device (S)

**Goal:** the input device owns deadzone + press buffering + consume-once, matching OG's `VirtualStick`/`VirtualButton`; the controller reads actions, not raw flags.

- Files: `input/input_system.{hpp,cpp}`, `input/virtual_input.{hpp,cpp}`, `player/player_state.hpp` (PlayerInput), `player/player_controller.cpp` (jump/dash buffer reads), `tests/input_system_smoke.cpp`, `tests/input_compat_smoke.cpp`.
- Work: `InputSystem` exposes `VirtualStickState move/camera` + `VirtualButtonState jump/dash/climb/pause`; `Poll()` feeds them (deadzone 0.35 configurable, default 0 to preserve feel); `PlayerInput` is built from the virtual states each tick (jump/dash `pressed` = consume-once edge with 0.10 s buffer); the controller's internal `jump_buffer_remaining`/`dash_buffer_remaining` are removed in favor of the device buffer (verify the 0.10 s semantics match — OG buffer is on the device).
- Edge cases: buffer must survive a press-before-landing (jump buffered in air, consumed on landing — the `buffered_jump.json` fixture from the gap matrix); consume-once must prevent double-fire (walljump + jump + dash-jump same frame); the GCN analog-trigger exclusion for Climb must survive.
- Verification: `input_system_smoke` + `input_compat_smoke` extended (buffer consume-once, deadzone on/off); `make test` green; device capture shows `[input]` unchanged at rest.

### Inc 2 — Real state machine (M)

**Goal:** replace hand-switched `PlayerMovementState` branches with enter/update/exit per state, matching OG's `StateMachine<States,Events>` structure.

- Files: `player/player_state.hpp` (enum grows to full OG set), `player/player_controller.cpp` (StatePhase dispatches to per-state handlers), new `player/player_states.hpp/cpp` (or keep in controller), `tests/gameplay_smoke.cpp`, `tests/climb_*` (must stay green).
- Work: extract the current Normal/Dashing/Skidding/Climbing bodies into `StNormalUpdate`/`StDashingUpdate`/`StSkiddingUpdate`/`StClimbingUpdate` equivalents with enter/exit hooks; add `Land` event parity (OG `Events.Land`); keep `LocomotionState` as the projection.
- Edge cases: state transitions must be order-identical to OG (climb entry before dash before jump in Normal; dash-jump exit; skid cancel conditions); the climb block's early returns must map to state transitions, not `return`s.
- Verification: all 57 smokes + `climb_bug_probe` green; device capture steady-state counters unchanged (near_batches 56 / resident 13).

### Inc 3 — Missing locomotion states: Spring + Feather (M)

**Goal:** port the two remaining locomotion states (Spring handler parity, FeatherStart/Feather).

- Files: `player/player_controller.cpp` (Spring/Feather handlers), `movement_config.hpp` (feather constants), `actor/spring_actor.cpp` (verify the player hook), `tests/gameplay_smoke.cpp` (spring launch, feather flight), `docs/movement_spec.md` (constants).
- Work: `Spring()` parity (state=Normal, Z approach to spring+3 at 2, XY approach 4, vel.Z=160, hold 0.3, autoJump, dashes=max(dashes,1), CancelGroundSnap); FeatherStart (0.4 s, XY approach 0 at 200, dash allowed) + Feather (rotate-toward `input*100` at `Tau*0.75`/`60`, Z approach `(featherZ-Position.Z)*40` at 600, duration 2.2, exit `vel.XY*=0.5, vel.Z=60, tHoldJump=0.1, autoJump`).
- Edge cases: feather wall-bump (reflect + 0.5 s cooldown) needs the wall query; feather exit must not snap to ground (CancelGroundSnap); spring on moving platform.
- Verification: new host smokes (spring launch velocity, feather flight + exit); `make test` green; device capture.

### Inc 4 — Interaction states: StrawbGet, Respawn/Dead, Bubble, Cassette, cutscene (M)

**Goal:** the non-locomotion states that suspend physics, with camera overrides.

- Files: `player/player_controller.cpp`, `player/player_state.hpp`, `scene/gameplay_scene.cpp` (death/respawn wiring), `world/collectible.cpp` (strawberry), `tests/gameplay_smoke.cpp`.
- Work: StrawbGet (velocity zero, camera override, 2.0 s + 1.2 s routine); Respawn/Dead (camera stored forward/distance, death plane + spike/DeathBlock overlap checks at waist); Bubble/Cassette (suspend); cutscene (suspend + camera override).
- Edge cases: death plane parity (OG `World.DeathPlane`; the port uses `respawn_fall_height=-200` — verify the world's death plane value); spike/DeathBlock overlap needs the actor query layer (gap matrix: "actor lifecycle/typed queries missing").
- Verification: host smokes (death → respawn reset, strawberry pickup state); device capture.

### Inc 5 — World/actor substrate: typed queries, riders, breakables, ledge-avoid (L)

**Goal:** close the gap-matrix dependencies that the states above lean on.

- Files: `world/world.hpp/cpp` (typed actor queries, `IHavePushout`/`IPickup`/`IRidePlatforms` traits), `actor/moving_solid_actor.cpp` (rider propagation), `actor/spring_actor.cpp`, `player/player_controller.cpp` (ledge-avoid sweep d=4 in Normal, breakable-block popout branch), `tests/*`.
- Work: typed `World.All<T>()`-style queries over the fixed `Room` arrays; pushout/pickup/rider traits; moving-solid velocity storage + `AddPlatformVelocity` parity (clamps Z [0,180], XY 300); the ledge-avoid sweep (OG's "TODO: Solve this way better" block, d=4); breakable-block popout (velocity > 90 → break).
- Edge cases: rider velocity must add once on takeoff (the `moving_platform_jump.json` fixture); ledge-avoid must not fight the ground snap; breakable popout must not fire on climb.
- Verification: new host smokes (rider takeoff, ledge-avoid, breakable); `make test` green; device capture.

### Inc 6 — Presentation layer: dust, footstep audio, dash trails, hair color (S)

**Goal:** the feel signals that make the movement read correctly (optional for gameplay, cheap).

- Files: `player/player_controller.cpp` (dust/footstep triggers), `render/` (trails), `audio/` (footstep, jump, dash, grab, let-go, wall-slide sfx), `tests/` (trigger-count smokes).
- Work: footstep interval 0.3 s at speed > 10; dust on ground + skid + climb-up; dash trail creation (2 per dash); hair color states (dash count); the sfx calls at the OG sites.
- Edge cases: audio must not allocate per frame; trails must be pooled (the renderer-pool pattern from memory-opt).
- Verification: host smokes (trigger counts); device capture (no new [memory] churn).

### Inc 7 — Feel validation + spec freeze (S)

**Goal:** close the loop — run the full acceptance checklist, freeze constants, update docs.

- Files: `docs/movement_spec.md`, `docs/controller_spec.md`, `docs/og_port_gap_matrix.md` (mark items done), `docs/physics_feel_acceptance.md` (record results).
- Work: run `make test` + `climb_bug_probe` + device `capture_baseline`; manual feel checklist on device; record any value that feels wrong; freeze.
- Verification: all artifacts green; the gap matrix shows no remaining "missing" rows; provenance written.

### Dependency graph

```
Inc 1 (input) ──┐
Inc 2 (FSM) ────┼──> Inc 3 (Spring/Feather) ──> Inc 4 (interaction states) ──> Inc 5 (world substrate) ──> Inc 6 (presentation) ──> Inc 7 (validate)
                └── (Inc 5 unblocks parts of Inc 4: actor queries)
```

- Inc 1 and Inc 2 are independent (input layer vs state machine) and can be done in either order; Inc 2 first is recommended (the FSM is the structural debt).
- Inc 3 depends on Inc 2 (new states need the FSM).
- Inc 4 depends on Inc 3 + partially on Inc 5 (actor queries for spike/DeathBlock).
- Inc 5 is the substrate; it can start in parallel with Inc 3 but must land before Inc 4's overlap checks.
- Inc 6 depends on Inc 2 (state hooks) and can run in parallel with Inc 3-5.
- Inc 7 is last.

---

## Caveats and open questions

- **Caveat (evidence):** the Foster `VirtualStick`/`VirtualButton` internals (exact deadzone shape, buffer decay) are not in the checkout — the port's `virtual_input.cpp` is the working interpretation. The 0.35 deadzone and 0.1 s buffer values are confirmed from [Controls.cs](https://github.com/EXOK/Celeste64/blob/main/Source/Data/Controls.cs); the exact circular-deadzone math is inferred (marked as inference).
- **Caveat (device):** the grab-entry front-side gate tension (reversed-winding baked walls vs auto-latch) is unresolved and parked — the plan keeps the current gate; a winding-independent front-side test is a candidate follow-up (Inc 5 or a dedicated climb increment).
- **Open question:** should the port's no-deadzone move stay (N64 stick coarseness) or adopt OG's 0.35? Recommend keep + config flag + A/B feel test (Inc 1).
- **Open question:** the `Lerp3(30,60,110,110,distance)` camera distance curve — verify on device whether the current fixed offset needs it (Inc 7).
- **Open question:** Feather/Spring/StrawbGet are not in the current ROM's scope (the demo is movement-first). The plan includes them as the "clean port" target; the user can trim Inc 3-4 if the demo scope is narrower.

## Sources

- Repo-local (primary): `Celeste64-og/Source/Actors/Player.cs`, `Celeste64-og/Source/Data/Controls.cs`, `Celeste64-og/Source/Data/ControlsConfig.cs`, `Celeste64-og/Source/Graphics/Camera.cs`, `Celeste64-og/Source/Scenes/World.cs`; `src/user/gameplay/player/*`, `src/user/gameplay/input/*`, `src/user/gameplay/physics/*`; `docs/controller_spec.md`, `docs/movement_spec.md`, `docs/og_port_gap_matrix.md`, `docs/physics_feel_acceptance.md`, `docs/feel_fixture_taxonomy.md`; `tests/run_host_tests.sh`.
- Online (verified reachable): [https://github.com/EXOK/Celeste64](https://github.com/EXOK/Celeste64) (canonical repo, MIT Source), [https://maddymakesgamesinc.itch.io/celeste64](https://maddymakesgamesinc.itch.io/celeste64) (controller recommended, v1.1.1), [https://raw.githubusercontent.com/EXOK/Celeste64/main/Source/Data/ControlsConfig.cs](https://raw.githubusercontent.com/EXOK/Celeste64/main/Source/Data/ControlsConfig.cs) (default bindings).
- Rejected: [https://exok.itch.io/celeste64](https://exok.itch.io/celeste64) (404 — wrong URL; the GitHub README points to maddymakesgamesinc.itch.io).
- Blocked: web_search (no credentials), subagent spawn (no tmux), Foster VirtualStick/VirtualButton source (not in checkout; not fetched).
