# N64 Celeste64 — Player Movement Controller Fidelity

## Context

The user wants a faithful N64-native recreation of the **Celeste 64 player
movement controller** (`EXOK/Celeste64/Source/Actors/Player.cs`), per
`docs/controller_spec.md` (73 sections). The objective is **gameplay
fidelity**, not source-code fidelity.

Investigation (2026-08-11, three parallel `Explore` passes) shows the current
controller is **already acceleration-based with OG Celeste64 values** — it is
*not* a naive `velocity = input * speed`. The `PlayerController` (rules) /
`PlayerMotor` (collision) split is clean, the `MovementConfig`/`MovementProfile`
carry the OG tuning table, and the `feel_spec/` host tests are the acceptance
layer. This plan is the output of the `plan-large` workflow and has been
stress-tested with the `review-plan` skill; the review findings have been
incorporated below. The gap to "faithful OG" is a set of **missing fidelity
features** plus some **dead/duplicated infrastructure**:

- **Missing features:** skid state (§16), slope speed multiplier (§42), ledge
  assist (§43), one-way platforms (`MAT_ONEWAY` defined but unused), ice
  (`MAT_ICE` defined but unused; also no `IceBlock` brush class registered),
  dash-jump fidelity (§31).
- **Dead config:** `MovementProfile profile_` is never wired from `config_` in
  the `PlayerController` constructor — it is always the hardcoded defaults.
- **Duplication:** math helpers (`RotateTowardXZ`, `NormalizeXZ`, `Approach`,
  `AnalogMagnitude`, `RelativeMoveInput`, …) are redefined per-file
  (`player_controller.cpp`, `camera_controller.cpp`, `runtime/math`).
- **Test room:** `assets/og_converted/maps/first-room.map` has floor/gap/climb-wall/kill-drop
  but no ceiling/slope/spikes; the spec (§65) wants a 16-element test map.

This plan **closes the fidelity gaps incrementally** on the verified working
controller. It does **not** rewrite the controller, does **not** introduce
fixed-point, does **not** rewrite collision to a swept-capsule, and does **not**
restructure to C files — all four were confirmed with the user.

## Architectural decisions

- **Decision: gap-close, not rewrite.** The controller already implements the
  momentum model (§71). We add the missing OG behaviors incrementally.
  Alternatives rejected: full rewrite (discards verified working code, high
  risk).
- **Decision: keep float, no fixed-point.** The entire codebase is
  `Vec3{float}`; the N64 VR4300 has an FPU; spec §4 explicitly allows "use the
  existing representation consistently." Alternatives rejected: 16.16 fx32
  (cross-cutting, no precedent, no gameplay benefit).
- **Decision: extend the existing substep motor, no swept-capsule rewrite.**
  `PlayerMotor` already does swept substeps (sweep_step=5), ground snap, wall
  pushout, ceiling clamp. We add slope/one-way/ice/ledge-assist on top.
  Alternatives rejected: analytic swept-capsule (high risk, touches the whole
  motor).
- **Decision: keep the C++ `PlayerController`/`PlayerMotor` split.** It already
  matches the spec's separation of responsibilities (§68). Alternatives
  rejected: C-file split per §67 (churn, no gameplay change).
- **Decision: consolidate duplicated math helpers into one shared module.**
  `runtime/math` already exists; move the file-local helpers there and have
  `player_controller.cpp`/`camera_controller.cpp` use it. This is the
  foundation increment that makes later feature work clean.
- **Decision: wire `MovementProfile` from `MovementConfig`.** Remove the dead
  `profile_` default path so tuning is single-source. The `MovementConfig` is
  the source of truth; `MovementProfile` becomes a derived view.
- **Decision: test room = code fixtures + extend `first-room.map`.** Host tests
  use `room_data.cpp` fixtures (slope/ledge/platform already exist); the ROM
  test room extends `first-room.map` with ceiling/slope/spikes/one-way/ice.
  Alternatives rejected: brand-new 16-element map (bake pipeline cost, winding
  risk), code-fixtures-only (no ROM coverage).
- **Decision: keep the port Y-up.** The spec's "Z up" is the C# source
  convention; the port is Y-up and load-bearing (collision, camera, map grid).
  `SrcToPort`/`PortToSrc` adapters handle boundaries only.

## Assumptions and answers from code

- **Decision: the controller is already acceleration-based with OG values.**
  Source: code @ `player_controller.cpp:549-604` (MoveTowardXZ toward
  `desired_speed`, rotate-based turning above `rotate_threshold`),
  `movement_config.hpp` (OG tuning table).
- **Decision: `profile_` is dead config.** Source: code @
  `player_controller.hpp:19-37` (constructor only sets `config_`),
  `movement_config.hpp` (`MovementProfile` defaults).
- **Decision: math helpers are duplicated per-file.** Source: code @
  `player_controller.cpp:100-130` (file-local `RotateTowardXZ`,
  `AnalogMagnitude`, `RelativeMoveInput`), `camera_controller.cpp` (own
  `Approach`/`Lerp`/`NormalizeXZ`), `runtime/math.hpp` (shared `Approach`,
  `RotateTowardXZ`).
- **Decision: skid is entirely absent.** Source: code @ `rg "skid"` → no
  matches in `src/user/gameplay`; `docs/movement_spec.md` lists it as a
  non-goal.
- **Decision: `MAT_ONEWAY` and `MAT_ICE` are defined but unused.** Source: code
  @ `coll_mesh.hpp` (bitfield), `player_motor.cpp` (`FaceIsClimbable`/
  `FaceIsDeath` only; no one-way/ice consumption).
- **Decision: `PlayerState` has no surface-material flags.** Ice and one-way
  state must be derived from the motor's last ground face or held in a new
  `MotorResult` field, not a `PlayerState` field, to avoid duplicating ground
  truth. Source: code @ `player_state.hpp:86` (no `on_ice`/`on_oneway` field).
- **Decision: `MAT_ICE` has no registered brush class.** Source: code @
  `tools/ogmap_lib/__init__.py` (`CLASS_REGISTRY` has no `IceBlock`/`MaterialClass.ICE`).
- **Decision: the motor is axis-separated substep, not a swept capsule.**
  Source: code @ `player_motor.cpp` (per-substep floor/ceiling/wall),
  `coll_mesh.hpp` (`SweepSphereMesh` is 8-sample).
- **Decision: the test room lacks ceiling/slope/spikes.** Source: code @
  `assets/og_converted/maps/first-room.map` (floor, gap, climb wall, kill-drop).
- **Decision: host tests have no runner; each embeds its g++ command.**
  Source: code @ `tests/*.cpp` header comments; `-Isrc/user`; `feel_spec/` is
  the acceptance layer.
- **Decision: the Makefile `src =` list is explicit.** Source: code @
  `Makefile:186-219` — new `.cpp` files must be appended. `runtime/math.cpp` is
  currently missing from this list.
- **Decision: default ROM boots map-pack, not single-room.** Source: code @
  `gameplay_scene.cpp:217` (Forsaken City map-pack path overrides the
  single-room `lvl_path`). `first-room` must be exercised by host fixture or
  temporary override.

## Risks accepted

- **Skid state could destabilize working movement.** Mitigation: gate skid
  entry strictly (§16 `SKID_DOT_THRESHOLD = -0.7`), keep it a sub-state of
  `Normal` (not a new `PlayerMovementState`), and keep the existing
  `feel_spec` tests green. Revisit if it fights the rotate-based turning.
- **One-way platforms require the motor to distinguish above/below.** The
  current floor probe may not handle "land from above, pass through from
  below." Mitigation: implement one-way as a floor-only material check in the
  motor's ground probe **and filter `MAT_ONEWAY` from the ceiling/wall probes**,
  with dedicated host tests. Accept; revisit if the substep model can't
  express it cleanly.
- **Slope speed multiplier could fight ground snap / slope-follow.** The motor
  already re-probes floors on slopes. Mitigation: apply the multiplier only in
  the controller's ground-movement branch, driven by `ground_normal`, and
  clamp to [0.75, 1.25] per §42.
- **Ledge assist could cause unwanted steering.** Mitigation: only steer when
  there is input, search ±17°, and never move the player without input (§43).
- **Extending `first-room.map` risks bake winding/axis issues.** Mitigation:
  follow `.agents/common-mistakes/og-map-polygon-winding.md` and
  `level_bake_report_smoke.py` (duplicate_vertex_faces=0, reversed_winding=0).
- **Perf pass could regress feel.** Mitigation: perf is a separate final
  increment; no-alloc audit and LUT are additive, and `feel_spec` re-runs after.

## Increment DAG

- Inc 1 — Foundation: math consolidation + profile wiring (S) — depends on: none — unblocks: 2,3,4,5,6,7,8,9,10
- Inc 2 — Skid state (M) — depends on: 1 — unblocks: 9
- Inc 3 — Slope speed multiplier (M) — depends on: 1 — unblocks: 9
- Inc 4 — Ledge assist (M) — depends on: 1 — unblocks: 9
- Inc 5 — One-way platforms (M) — depends on: 1 — unblocks: 8, 9
- Inc 6 — Ice material (S) — depends on: 1 — unblocks: 8, 9
- Inc 7 — Dash-jump fidelity (M) — depends on: 1 — unblocks: 9
- Inc 8 — Test room extension (M) — depends on: 5, 6 — unblocks: 9
- Inc 9 — feel_spec acceptance expansion (M) — depends on: 2,3,4,5,6,7 — unblocks: 10
- Inc 10 — N64 perf pass (M) — depends on: 9 — unblocks: none

> **DAG note (from review):** Inc 8 ships `MAT_ONEWAY`/`MAT_ICE` into the map, so it
> must depend on Inc 5/6 (which implement those materials) — otherwise its "traverse
> all" done criteria can't be met. Inc 9 (feel_spec acceptance) does not touch the
> ROM map, so it drops the Inc 8 dependency and runs in parallel with Inc 8.

## Review-plan findings incorporated

The `review-plan` stress-test (via an `Explore` subagent) identified the
following issues, which have been patched into the plan above:

1. **Mapping table values were wrong** — `jump_speed` no longer maps to
   `wall_jump_vertical_speed`; `dash_long_jump_speed` is described as a
   config default, not a hard 40. Profile-only fields must keep current
   defaults.
2. **`runtime/math.cpp` missing from Makefile** — added to Inc 1 with a
   verification `rg` command.
3. **One-way must filter ceiling and wall probes too** — Inc 5 now requires
   filtering `MAT_ONEWAY` in floor, ceiling, and wall probes.
4. **Ice has no registered brush class** — Inc 6 now requires adding an
   `IceBlock` → `MaterialClass.ICE` entry in `tools/ogmap_lib/__init__.py`.
5. **Subnormal/NaN sanitization gap** — noted in Inc 1 edge cases; the
   implementer must preserve current flushing behavior.
6. **`PlayerState` has no surface-material fields** — clarified that ice/one-way
   state lives in `MotorResult`, not `PlayerState`.
7. **Default ROM boots map-pack, not `first-room`** — Inc 8 now requires a
   host fixture test or temporary single-room override to exercise the
   extended room.
8. **`PlayerController::Step`/`StatePhase` signature change** — the new
   `const Room*` parameter must be defaulted to `nullptr` to keep existing
   host tests compiling.

## Increments

### Inc 1 — Foundation: math consolidation + profile wiring (S)
**Depends on:** none
**Unblocks:** 2,3,4,5,6,7,8,9,10
**Status: done** (2026-08-11)
**Done criteria:** `player_controller.cpp` and `camera_controller.cpp` use the
shared `runtime/math` helpers (no file-local duplicates); `MovementProfile` is
derived from `MovementConfig`; all existing `feel_spec` + `movement_contracts`
tests still pass.

#### Files to touch

##### src/user/gameplay/runtime/math.hpp / math.cpp
- What changes: add the missing shared helpers and reconcile names with the
  existing exports. The current `runtime/math.hpp` already exposes `Approach`,
  `AngleApproach`, `AngleXZ`, `DirectionFromAngle`, `ApproachXZ`, and
  `RotateTowardXZ`. Reuse those names; do not add `MoveToward`,
  `MoveTowardXZ`, or `ApproachAngle` duplicates.
- Function(s): `Vec3 NormalizeXZ(const Vec3&, const Vec3& fallback)`,
  `float LengthXZ(const Vec3&)`, `float DotXZ(const Vec3&, const Vec3&)` (if not
  already present), `float AnalogMagnitude(float raw_length)`,
  `Vec3 RelativeMoveInput(const Vec2&, const Vec3& camera_forward, const Vec3& fallback)`.
  Keep the existing `Approach` / `ApproachXZ` / `AngleApproach` / `AngleXZ` /
  `DirectionFromAngle` / `RotateTowardXZ` names.
- Integration points: `player_controller.cpp`, `camera_controller.cpp` call
  these instead of their file-local copies.
- Error paths: `NormalizeXZ` must fall back to `fallback` on zero-length
  (preserve current behavior).

##### src/user/gameplay/player/player_controller.cpp
- What changes: delete file-local math helpers; include `runtime/math.hpp`.
- Function(s): unchanged public API.
- Integration points: `StatePhase`/`TimerInputPhase` use shared helpers.
- Error paths: none new.

##### src/user/gameplay/player/camera_controller.cpp
- What changes: delete file-local `Approach`/`Lerp`/`NormalizeXZ`; use shared.
  Because the unified `NormalizeXZ` takes a `fallback` argument, update every
  camera caller to pass one (e.g. `{0,0,1}`).
- Function(s): unchanged public API.
- Integration points: camera step uses shared helpers.
- Error paths: none new.

##### src/user/gameplay/player/player_controller.hpp
- What changes: wire `profile_` from `config_` in the constructor; add a
  `MovementProfile BuildProfile(const MovementConfig&)` helper (or a
  `MovementConfig::ToProfile() const` method). Keep `Step()` / `StatePhase()`
  public signatures backwards-compatible by defaulting the new `const Room*`
  to `nullptr` in Inc 4.
- Function(s): `PlayerController(MovementConfig config)` now sets both
  `config_` and `profile_`.
- Integration points: `StatePhase` reads `profile_` (already does).
- Error paths: `MovementProfile` must remain a pure derived view — no new
  fields that can drift from `MovementConfig`.

##### src/user/gameplay/player/movement_config.hpp
- What changes: add `MovementProfile ToProfile() const` to `MovementConfig`
  (or a free function), mapping each `MovementConfig` field to the
  `MovementProfile` field it feeds. Do **not** change existing default values
  in `MovementProfile`; only override fields that have a `MovementConfig`
  source.
- Function(s): `MovementProfile MovementConfig::ToProfile() const`.
- Integration points: `PlayerController` constructor.
- Error paths: every `MovementProfile` field must be populated (no stale
  defaults).

##### Makefile
- What changes: **add `src/user/gameplay/runtime/math.cpp` to the `src =`
  list** (line ~186-210). It is currently NOT compiled by the ROM build, so
  once `player_controller.cpp`/`camera_controller.cpp` call the shared
  helpers, the ROM link would fail with undefined symbols.
- Function(s): n/a.
- Integration points: `src =` list.
- Error paths: missing entry → link failure; must be added in Inc 1.
- **Verify:** after the change, `rg -n "runtime/math.cpp" Makefile` returns a
  hit in the `src =` block.

##### Mapping table for `MovementConfig::ToProfile()`
The two structs are NOT a trivial 1:1 map. Resolve each field explicitly:

| MovementConfig | MovementProfile | Note |
|---|---|---|
| `run_speed` | `run_max_speed` | direct |
| `acceleration` | `ground_acceleration`, `air_acceleration`, `air_turn_acceleration` | one config → three profile fields |
| `friction` | `ground_deceleration` | direct |
| `past_max_deceleration` | `past_max_decel` | direct |
| `air_accel_mult_min/max` | (used inline in StatePhase) | keep reading from config_ |
| `gravity` | (used inline) | keep reading from config_ |
| `max_fall_speed` | (used inline) | keep reading from config_ |
| `half_gravity_threshold` | (used inline) | keep reading from config_ |
| `jump_speed` | (used inline for normal jump) | keep reading from config_ |
| `jump_hold_time` | (used inline) | keep reading from config_ |
| `jump_xy_boost` | (used inline) | keep reading from config_ |
| `dash_speed` | `dash_speed` | direct |
| `dash_duration` | `dash_active_time` | direct (0.20) |
| `dash_cooldown` | `dash_cooldown` | direct (0.10) |
| `dash_reset_cooldown` | (used inline) | keep reading from config_ |
| `dash_jump_speed` | `dash_long_jump_speed` | direct (config default 40) |
| `dash_jump_hold_speed` | (used inline) | keep reading from config_ |
| `dash_jump_hold_time` | (used inline) | keep reading from config_ |
| `dash_jump_xy_boost` | (used inline) | keep reading from config_ |
| `wall_jump_speed_x` | `wall_jump_horizontal_speed` | direct (83.2) |
| `wall_jump_speed_y` | `wall_jump_vertical_speed` | direct (config default 90) |
| `climb_speed` | `climb_speed` | direct |
| — | `rotate_threshold` | profile-only; keep default 12.8 |
| — | `rotate_speed` | profile-only; keep default Tau·1.5 |
| — | `rotate_speed_above_max` | profile-only; keep default Tau·0.6 |
| — | `dash_rotate_speed` | profile-only; keep default Tau·0.3 |
| — | `coyote_time` | profile-only; keep default 0.12 |
| — | `jump_buffer_time` | profile-only; keep default 0.08 |
| — | `dash_hitstop_time` | profile-only; keep default 0.02 |
| — | `neutral_wall_jump_*` | profile-only; keep defaults |

Profile-only fields keep their hardcoded defaults; the mapping only wires the
fields that have a `MovementConfig` source. This removes the dead `profile_`
default path without adding new config surface.

**Important:** the current `MovementProfile` defaults are already correct
(e.g. `dash_long_jump_speed` remains at its current default unless the
mapping overrides it). The mapping only overrides fields that have a matching
`MovementConfig` source; all other fields keep their current defaults. The
implementer must not change the current profile defaults during this mapping.

#### Edge cases
- `AnalogMagnitude` mapping (0.4→0.3, 0.92→1.0) must be preserved exactly.
- `RelativeMoveInput` must keep the `right = {forward.z, 0, -forward.x}`
  convention (Y-up).
- Removing file-local helpers must not change numeric results — the shared
  versions must be behavior-identical. **Note:** `player_controller.cpp`'s
  `NormalizeXZ` takes a `fallback` param; `camera_controller.cpp`'s does not.
  The unified `runtime/math` signature wins: `NormalizeXZ(const Vec3&, const
  Vec3& fallback)`. Camera callers must be updated to pass a fallback (e.g.
  `{0,0,1}`). **Reconcile names against the existing `runtime/math.hpp` exports**
  (`Approach`, `AngleApproach`, `AngleXZ`, `DirectionFromAngle`, `ApproachXZ`,
  `RotateTowardXZ`) — reuse existing names; do not re-add `MoveToward`,
  `MoveTowardXZ`, or `ApproachAngle`.
- **Subnormal/NaN sanitization:** the file-local `MoveTowardXZ` flushes
  subnormals; the shared `ApproachXZ` currently does not. Either add
  `FlushSubnormalBits` to `Approach`/`ApproachXZ` or keep a sanitization
  wrapper in `player_motor.cpp` / `player_controller.cpp` after calls. The
  math consolidation must not change runtime numerical behavior.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/jump_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/runtime/math.cpp -o /tmp/jump_smoke && /tmp/jump_smoke` (and the dash/locomotion smokes, `movement_contracts_test`, `movement_parity_smoke`, `climb_parity_smoke`, `camera_space_math`). **Every host g++ command that compiles `player_controller.cpp` must also link `src/user/gameplay/runtime/math.cpp`** (this applies to all Inc 2-7 smoke commands too).
- Tests to add/update: none new; all existing must pass unchanged.
- Done: all host tests green; `rg "RotateTowardXZ|NormalizeXZ|AnalogMagnitude"` in `player_controller.cpp`/`camera_controller.cpp` shows only shared-module usage; `./compile-rom.sh` links cleanly.

### Inc 2 — Skid state (M)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** running fast and pushing opposite direction enters a skid
sub-state that preserves momentum, decelerates, then accelerates toward the new
direction; a skid jump is possible; `feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_state.hpp
- What changes: add a `skidding` bool (or a `SkidState` sub-struct) to
  `PlayerState`; add `skid_dot_threshold`, `skid_start_accel`,
  `skid_accel`, `end_skid_speed` to `MovementConfig`/`MovementProfile`.
- Function(s): new fields only.
- Data shapes: `bool skidding = false;` + config floats.
- Integration points: `StatePhase` reads/writes.
- Error paths: skid must clear on ground loss or direction agreement.

##### src/user/gameplay/player/player_controller.cpp
- What changes: in the grounded high-speed branch (§48), when
  `Dot(Normalize(input), Normalize(velocity)) <= -0.7` and
  `speed > rotate_threshold`, enter skid: preserve momentum, decelerate at
  `skid_start_accel` (300) then `skid_accel` (500), allow skid jump, exit when
  speed < `end_skid_speed` (51.2) or input agrees with velocity.
- Function(s): `void EnterSkid(...)`, `void UpdateSkid(...)` (file-local).
- Integration points: called from the grounded branch of `StatePhase`.
- Error paths: skid must not fire when grounded is false; must not fight the
  rotate-based turning path.

##### src/user/gameplay/player/movement_config.hpp
- What changes: add `skid_dot_threshold = -0.7f`, `skid_start_accel = 300.0f`,
  `skid_accel = 500.0f`, `end_skid_speed = 51.2f` (MaxSpeed*0.8).
- Function(s): fields only; `ToProfile()` maps them.
- Integration points: `StatePhase`.
- Error paths: none.

#### Edge cases
- Skid jump (§16 "allow skid jump") must consume the jump press once.
- Skid must not trigger on a gentle direction change (only ≤ -0.7 dot).
- Skid must clear when the player leaves the ground.
- **Guard zero-velocity normalize:** `Dot(Normalize(input), Normalize(velocity))`
  divides by `LengthXZ(velocity)`; a standing player (velocity ≈ 0) would NaN.
  Add a `speed_xz > kEpsilon` guard before the skid check.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/locomotion_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/runtime/math.cpp -o /tmp/loco && /tmp/loco`; add a `skid_smoke.cpp` host test.
- Tests to add/update: new `tests/feel_spec/skid_smoke.cpp` (run right fast, push left → skidding true, momentum preserved, no instant inversion; skid jump works).
- Done: skid test passes; `feel_spec` + `movement_contracts` still green.

### Inc 3 — Slope speed multiplier (M)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** ground movement speed is scaled by `ground_normal` (flat 1.0,
downhill ≤1.25, uphill ≥0.75, clamped); `feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp
- What changes: in the grounded movement branch, compute a slope multiplier
  from `state.contact.ground_normal` and the desired movement direction; scale
  `desired_speed` by it (clamped [0.75, 1.25]).
- Function(s): `float SlopeSpeedMultiplier(const Vec3& ground_normal, const Vec3& move_dir)` (file-local).
- Integration points: grounded branch of `StatePhase`.
- Error paths: flat ground (`{0,1,0}`) must give exactly 1.0; must not affect
  air movement.

##### src/user/gameplay/player/movement_config.hpp
- What changes: add `slope_downhill_mult = 1.25f`, `slope_uphill_mult = 0.75f`.
- Function(s): fields only.
- Integration points: `StatePhase`.
- Error paths: none.

#### Edge cases
- Downhill vs uphill determined by `dot(ground_normal, move_dir)` sign.
- Must not apply when `ground_normal` is near-vertical (wall) — guard with a
  minimum `ground_normal.y`.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/locomotion_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/runtime/math.cpp -o /tmp/loco && /tmp/loco`; add a `slope_smoke.cpp` host test using `BuildSlopeFixtureRoom`.
- Tests to add/update: new `tests/feel_spec/slope_smoke.cpp` (flat=1.0, downhill>1.0, uphill<1.0, clamped).
- Done: slope test passes; `feel_spec` + `movement_contracts` still green.

### Inc 4 — Ledge assist (M)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** when input direction has no floor ahead, the desired
movement steers toward the nearest valid floor direction within ±17°; never
moves the player without input; `feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp
- What changes: before applying ground movement, sample the floor ahead in the
  input direction; if no floor, search ±17° for a valid floor and steer
  `move_input` toward it. If no `Room` is passed (`nullptr`), ledge assist is
  skipped.
- Function(s): `Vec3 LedgeAssist(const Room&, const Vec3& position, const Vec3& move_dir, float search_deg)` (file-local; needs a floor probe — see below).
- Integration points: grounded branch of `StatePhase`, before `MoveTowardXZ`.
- Error paths: no input → no assist; no floor found in ±17° → no steering.

##### src/user/gameplay/player/player_motor.hpp / .cpp
- What changes: expose a floor-ahead probe the controller can call (or pass the
  `Room` into the controller's `Step`). Currently `PlayerController::Step`
  does not take a `Room`. Add an optional `const Room*` to `Step`/`StatePhase`
  (default `nullptr` keeps existing host tests and call sites compiling). The
  new signature for `Step` should look like:
  ```cpp
  void Step(PlayerState& state, const PlayerInput& input,
            const Vec3& camera_forward, float delta_seconds,
            const Room* room = nullptr) const;
  ```
  `StatePhase` gets a matching default.
- Function(s): `bool FloorAhead(const Room&, const Vec3& position, const Vec3& dir, float dist)`.
- Integration points: `StatePhase` calls it when a `Room` is available.
- Error paths: `nullptr` `Room` → ledge assist disabled; host tests that don't
  pass a Room are unaffected.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: **thread the same active `Room` that the motor uses into the
  `StatePhase` call.** The scene invokes `TimerInputPhase`/`StatePhase`/`LateContactPhase`
  directly (not `Step`), so the optional `const Room*` must be passed here or
  ledge assist would stay permanently disabled in the ROM. Use the same room
  instance the motor is already using to avoid inconsistent collision sources.
- Function(s): pass the active room as the new `const Room*` argument to
  `StatePhase` at the call site (~line 632).
- Integration points: `StatePhase` call in the fixed-tick loop.
- Error paths: if the active room is unavailable, pass `nullptr` (ledge assist
  off) rather than a dangling pointer.

#### Edge cases
- Must not fire when grounded is false.
- Must not override a valid floor directly ahead.
- Search must be symmetric (±17°) and clamped.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/locomotion_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/player/player_motor.cpp src/user/gameplay/world/world.cpp src/user/gameplay/world/room_data.cpp src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp src/user/gameplay/runtime/math.cpp -o /tmp/loco && /tmp/loco`; add a `ledge_smoke.cpp` host test using `BuildLedgeFixtureRoom`.
- Tests to add/update: new `tests/feel_spec/ledge_smoke.cpp` (walk toward a gap → steers to floor; no input → no move).
- Done: ledge test passes; `feel_spec` + `movement_contracts` still green; ROM boots with ledge assist active (pass the Room).

### Inc 5 — One-way platforms (M)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** `MAT_ONEWAY` faces act as floors when landing from above but
are pass-through from below; `feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_motor.cpp
- What changes: in the **floor probe**, treat `MAT_ONEWAY` faces as valid floors
  only when the player is above and moving down (or landing); in the **ceiling
  probe**, explicitly reject `MAT_ONEWAY` faces so the player can jump up
  through them; in the **wall probe**, ignore `MAT_ONEWAY` faces so they never
  act as a wall.
- Function(s): `bool FaceIsOneWay(const Room&, int face_id)`; extend floor,
  ceiling, and wall probe filters.
- Integration points: `PlayerMotor::Step` ground/ceiling/wall probes.
- Error paths: one-way must not block upward movement; must not be a wall; must
  not break `MAT_SOLID` behavior.

##### src/user/gameplay/player/player_motor.hpp
- What changes: add a `one_way_land_tolerance` config field if needed.
- Function(s): config field only.
- Integration points: `PlayerMotorConfig`.
- Error paths: none.

#### Edge cases
- Landing on a one-way from above must set `grounded` and allow dash refill.
- Jumping up through a one-way must not collide.
- Must not break `MAT_SOLID` behavior.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/player_motor_smoke.cpp src/user/gameplay/player/player_motor.cpp src/user/gameplay/world/world.cpp src/user/gameplay/world/room_data.cpp src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp src/user/gameplay/runtime/math.cpp -o /tmp/motor && /tmp/motor`; add a `oneway_smoke.cpp` host test.
- Tests to add/update: new `tests/feel_spec/oneway_smoke.cpp` (land from above → grounded; pass through from below → not blocked).
- Done: one-way test passes; `feel_spec` + `movement_contracts` still green.

### Inc 6 — Ice material (S)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** `MAT_ICE` faces reduce ground friction (low-friction slide);
`feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_motor.cpp
- What changes: expose the ground face material (or `on_ice`/`on_oneway` flags)
  in `MotorResult` so the controller can reduce friction and the motor can
  filter one-way. `PlayerState` does not store surface material; `MotorResult`
  is the ground truth.
- Function(s): `bool FaceIsIce(const Room&, int face_id)`, `bool FaceIsOneWay(...)`;
  add `bool on_ice` / `bool on_oneway` to `MotorResult`.
- Integration points: `PlayerMotor::Step` sets the flags from the ground face.
- Error paths: `on_ice` / `on_oneway` must be false when not grounded.

##### tools/ogmap_lib/__init__.py
- What changes: register a new brush class that produces `MAT_ICE`. Add an
  `IceBlock` (or similar) class mapping to a new `MaterialClass.ICE`.
- Function(s): `CLASS_REGISTRY` entry.
- Integration points: map authoring pipeline; `first-room.map` entity list.
- Error paths: if `MaterialClass.ICE` is not produced, the motor will never
  see ice.

##### src/user/gameplay/player/player_controller.cpp
- What changes: when `motor_result.on_ice`, use a low-friction multiplier
  (e.g. `0.1x` ground friction) and reduced acceleration.
- Function(s): `ice_friction_mult = 0.1f` config field.
- Integration points: grounded branch of `StatePhase` reads the ice flag from
  the motor result.
- Error paths: ice must not affect air movement.

#### Edge cases
- Ice must not prevent stopping entirely (still decelerates, just slower).
- Ice must not affect jump/dash.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/locomotion_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/player/player_motor.cpp src/user/gameplay/world/world.cpp src/user/gameplay/world/room_data.cpp src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp src/user/gameplay/runtime/math.cpp -o /tmp/loco && /tmp/loco`; add an `ice_smoke.cpp` host test.
- Tests to add/update: new `tests/feel_spec/ice_smoke.cpp` (on ice → decelerates slower than on solid).
- Done: ice test passes; `feel_spec` + `movement_contracts` still green.

### Inc 7 — Dash-jump fidelity (M)
**Depends on:** 1
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** dash-jump (§31) matches OG: ends dash, applies vertical
velocity, preserves horizontal momentum, consumes/resets dash, enables
variable jump; `feel_spec` still passes.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp
- What changes: audit and correct the existing `StartDashJump` path against
  §31/§72 (`dash_jump_speed` from config, `dash_jump_hold_speed` from config,
  `dash_jump_hold_time` from config, `dash_jump_xy_boost` from config). Verify
  it ends dash, preserves momentum, and enables variable jump. The values
  currently match the spec (40 / 20 / 0.30 / 16); do not overwrite them.
- Function(s): `void StartDashJump(...)` (existing, ~line 160).
- Integration points: dash-end branch of `StatePhase`.
- Error paths: dash-jump must not double-consume the jump press; must not
  refill dash mid-air.

**Concrete deltas to verify/correct (from §31/§72):**
1. **Trigger:** dash-jump fires only when the dash started on the ground
   (`dashed_on_ground`) and jump is pressed within the dash window
   (`no_dash_jump_remaining <= 0`). Air dashes do NOT dash-jump.
2. **End dash:** entering dash-jump must leave `PlayerMovementState::Dashing`
   back to `Normal` immediately (no residual dash timer).
3. **Vertical:** `velocity.y = config_.dash_jump_speed`; hold at
   `config_.dash_jump_hold_speed` for `config_.dash_jump_hold_time` while jump held.
   The current defaults are 40 / 20 / 0.30.
4. **Horizontal momentum:** preserve the dash's horizontal velocity (do NOT
   zero it); add `config_.dash_jump_xy_boost` (default 16) along facing if there is input.
5. **Dash consumption:** the dash charge is already spent by the dash; do not
   refund it. `dash_reset_cooldown` still gates the landing refill.
6. **Variable jump:** the dash-jump must use the same hold-jump sustain path as
   a normal jump (so early release = shorter dash-jump).

##### src/user/gameplay/player/movement_config.hpp
- What changes: confirm `dash_jump_*` fields exist and are wired via
  `ToProfile()` (they do; verify).
- Function(s): fields only.
- Integration points: `StatePhase`.
- Error paths: none.

#### Edge cases
- Dash-jump from ground vs air must differ (ground dash-jump only).
- Must preserve dash momentum horizontally.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/feel_spec/dash_smoke.cpp src/user/gameplay/player/player_controller.cpp src/user/gameplay/runtime/math.cpp -o /tmp/dash && /tmp/dash`; add a `dash_jump_smoke.cpp` host test.
- Tests to add/update: new `tests/feel_spec/dash_jump_smoke.cpp` (dash on ground + jump → dash-jump, momentum preserved, variable height).
- Done: dash-jump test passes; `feel_spec` + `movement_contracts` still green.

### Inc 8 — Test room extension (M)
**Depends on:** 5, 6
**Unblocks:** 9
**Status: done** (2026-08-11)
**Done criteria:** `first-room.map` gains ceiling, slope, downhill/uphill,
spike pit, one-way platform, and ice floor; the room is exercised either by
host fixture tests or by a temporary single-room boot path in the ROM;
`level_bake_report_smoke.py` reports 0 winding errors.

#### Files to touch

##### tools/ogmap_lib/__init__.py
- What changes: verify `CLASS_REGISTRY` has a `OneWayBlock` entry and add a
  new `IceBlock` entry mapping to a new `MaterialClass.ICE`. If `OneWayBlock`
  is not registered yet, add it alongside `IceBlock`.
- Function(s): registry entries.
- Integration points: `.map` parser.
- Error paths: missing brush class produces missing material faces; no
  `MaterialClass.ICE` registration means the motor will never see ice.

##### assets/og_converted/maps/first-room.map
- What changes: add brushes for ceiling, slope, downhill/uphill, spike pit
  (MAT_DEATH), one-way platform (MAT_ONEWAY), ice floor (MAT_ICE). Follow
  `.agents/map-creation.md` and `.agents/common-mistakes/og-map-polygon-winding.md`.
- Function(s): n/a (map data).
- Integration points: baked into `filesystem/lvl/first-room.{lvl,colmesh,manifest,nav}`.
- Error paths: winding must be correct (see common-mistakes).

##### Makefile
- What changes: ensure the bake rule for `first-room` runs and the new
  materials are in the manifest. If new texture sprites are introduced, add
  their file paths to `DFS_TEX_FILES`.
- Function(s): n/a.
- Integration points: `bake-first-room` target.
- Error paths: material indices must stay in sync (see `.agents/map-creation.md`).

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: document the default boot path. The current ROM boots into the
  Forsaken City map-pack (`forsyken-city`) by default, so `first-room` is not
  the default. Inc 8 must be exercised by either (a) a host fixture test that
  loads `first-room` directly, or (b) a temporary single-room boot (e.g.
  `lvl_path = "rom:/lvl/1-1.lvl"`). The implementer must verify the extended
  room through at least one of these paths before claiming Inc 8 done.
- Function(s): boot path selection (temporary or documented).
- Integration points: map-pack loader vs. single-room loader.
- Error paths: default map-pack boot never loads `first-room`.

#### Edge cases
- One-way and ice materials must be present in the manifest and material
  catalog (null-slot reservation for `TB_empty`).
- Spike pit must be `MAT_DEATH` so the motor's `FaceIsDeath` triggers respawn.

#### Verification
- Run: `python3 tests/level_bake_report_smoke.py` (0 winding errors);
  `./compile-rom.sh`; boot in Mupen64Plus/Ares and traverse each element, or
  run the host fixture that loads `first-room` directly.
- Tests to add/update: add a host test that loads the baked `first-room`
  (single-room path) so Inc 8 is exercised even when the default ROM boot is
  the Forsaken City map-pack.
- Done: bake report clean; ROM boots (or single-room host test passes); all
  16 spec elements reachable.

### Inc 9 — feel_spec acceptance expansion (M)
**Depends on:** 2,3,4,5,6,7
**Unblocks:** 10
**Status: done** (2026-08-11)
**Done criteria:** the `feel_spec` suite covers the spec §66 test cases
(standing, full analog, release, direction change/skid, jump, short/full jump,
coyote, late jump, dash, dash end, dash refill, wall jump, climb) and all pass.

#### Files to touch

##### tests/feel_spec/fixtures.json
- What changes: add scenarios for skid, slope, ledge, one-way, ice, dash-jump.
- Function(s): n/a (data).
- Integration points: `test_fixture_taxonomy.py` reads it.
- Error paths: locked windows must match the new features.

##### tests/feel_spec/*.cpp
- What changes: add `skid_smoke.cpp`, `slope_smoke.cpp`, `ledge_smoke.cpp`,
  `oneway_smoke.cpp`, `ice_smoke.cpp`, `dash_jump_smoke.cpp` (created in
  Incs 2-7) and wire them into a single `feel_spec` runner if one is added.
- Function(s): n/a.
- Integration points: each test links `player_controller.cpp` (+ motor/world
  for collision-dependent ones).
- Error paths: each test returns 0 on pass, 1 on fail.

#### Edge cases
- The spec §66 "late jump" (beyond 0.12s coyote) must fail the jump.
- The spec §66 "dash end" (airborne) must reduce velocity to ~75%.

#### Verification
- Run: each `feel_spec/*.cpp` via its header g++ command; `test_fixture_taxonomy.py`.
- Tests to add/update: the six new smokes + fixtures.
- Done: all `feel_spec` tests pass; taxonomy validates fixtures.

### Inc 10 — N64 perf pass (M)
**Depends on:** 9
**Unblocks:** none
**Status: done** (2026-08-11)
**Done criteria:** the controller update loop has no allocations, no expensive
float trig in the hot path, and collision queries are spatially limited; the
ROM runs at 60 Hz comfortably.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp
- What changes: audit for `malloc`/`new`/dynamic arrays in the update loop
  (spec §56); replace any with stack/preallocated. Confirm no `sin`/`cos`/
  `atan2` per frame (spec §57) — use the existing `runtime/math` LUT or
  `ApproachAngle`.
- Function(s): n/a (audit + micro-optimizations).
- Integration points: `StatePhase`/`TimerInputPhase`.
- Error paths: none.

##### src/user/gameplay/player/player_motor.cpp
- What changes: confirm collision queries go through the BVH broad phase
  (`OverlapAabbMesh`) and are spatially limited (spec §59); add a
  `QueryWallsMesh`/`QueryFloorSource` spatial limit if the global mesh query
  is unbounded.
- Function(s): n/a (audit + spatial limit).
- Integration points: `PlayerMotor::Step`.
- Error paths: spatial limit must not miss nearby geometry.

##### src/user/gameplay/runtime/math.hpp / math.cpp
- What changes: add a `SinLUT`/`CosLUT` or confirm `ApproachAngle` avoids
  per-frame trig.
- Function(s): `float SinLUT(uint16_t angle)`, `float CosLUT(uint16_t angle)`.
- Integration points: `RotateTowardXZ`/`DirectionFromAngle`.
- Error paths: LUT must be deterministic and match float results within
  tolerance.

#### Edge cases
- No allocation must hold even in dash/climb/skid transitions.
- LUT precision must not change feel (re-run `feel_spec`).
- **Scope of the trig gate:** the `rg` gate is scoped to the **controller
  update loop** (`player_controller.cpp`/`player_motor.cpp`), not the camera.
  `camera_controller.cpp` uses `std::cos`/`std::sin` for orbit — that is
  per-frame but out of scope for this increment (camera is a separate system).
  Make this boundary explicit in the gate so it is not misread as "no trig
  anywhere."

#### Verification
- Run: `rg "malloc|new |std::vector|sin\(|cos\(|atan2"` in
  `src/user/gameplay/player/player_controller.cpp src/user/gameplay/player/player_motor.cpp` → no hits in the update loop; re-run all
  `feel_spec` tests; `./compile-rom.sh`; boot and confirm 60 Hz.
- Tests to add/update: none new (audit + existing tests).
- Done: no-alloc audit clean; `feel_spec` green; ROM runs at 60 Hz.

## Cross-cutting verification

- **Backwards-compat / rollback / observability (per increment):** every
  increment is **additive with defaulted config** — new fields default to
  values that preserve current behavior, and new features are gated behind
  their own state/config. Each increment ships independently and is safe to
  roll back by reverting its single PR. Verify each via its own smoke test
  (listed per increment). The debug HUD (spec §64) is added at Inc 10; until
  then, per-increment observability is the smoke test output.
- After Inc 9, manually walk the spec §73 sequence in the ROM: RUN →
  ACCELERATE → JUMP → AIR CONTROL → DASH → LAND → RETAIN MOMENTUM → SKID →
  JUMP → WALL CONTACT → WALL JUMP → DASH → CLIMB → CLIMB JUMP. Confirm it
  feels like one continuous momentum system.
- After Inc 8, traverse all 16 test-room elements in the ROM (floor, runway,
  platform, gap, small/large ledge, wall, parallel walls, ceiling, slope,
  downhill, uphill, spike pit, dash corridor, climb wall, wall-jump section).
- After Inc 10, confirm the ROM holds 60 Hz with the debug HUD showing
  `STATE`, `SPD`, `VEL`, `DASH`, `GROUND`, `COYOTE` (spec §64).

## Standards / common-mistakes referenced

- `.agents/map-creation.md` — applies to: Inc 8 (bake pipeline, material
  catalog, entity IDs, kPosFp).
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: Inc 8
  (winding guardrail).
- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 8 (rom:/ path
  must match filesystem/ layout).
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: Inc 8
  (ResetPlayerToRoomStart after level load).
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: Inc 8/9
  (camera reset on respawn).

## Open questions (CONSIDER from review)

- **Inc 1 `NormalizeXZ` signature conflict:** `player_controller.cpp`'s
  `NormalizeXZ` takes a `fallback` param; `camera_controller.cpp`'s does not.
  The unified `runtime/math` signature wins (`NormalizeXZ(const Vec3&, const
  Vec3& fallback)`); camera callers must pass a fallback. Confirm no other
  callers break.
- **Inc 1 `runtime/math.hpp` name collisions:** it already exports
  `AngleApproach`, `DirectionFromAngle`, `Approach`, `ApproachXZ`,
  `RotateTowardXZ`. The plan's helper list partially overlaps these — the
  implementer must reuse existing names and add only the missing helpers
  (`NormalizeXZ`, `LengthXZ`, `DotXZ`, `AnalogMagnitude`, `RelativeMoveInput`,
  `Clamp`).
- **Inc 1 subnormal/NaN sanitization:** `Approach`/`ApproachXZ` in
  `runtime/math.cpp` do not flush subnormals/NaN like the file-local
  `player_controller.cpp` helper. Decide whether to add `FlushSubnormalBits` to
  the shared helpers or keep a thin caller-side wrapper so the ROM/host math
  behavior is identical.
- **Inc 4 `Room` identity in map-pack mode:** pass the same `Room` pointer the
  motor is already using into `StatePhase`. If map-pack active-room switching
  changes mid-frame, ledge-assist must not query a stale room.
- **Inc 5 one-way in the substep model:** the current floor probe may not
  cleanly express "land from above, pass through from below." If the substep
  model can't express it, revisit the approach (e.g. a dedicated one-way
  landing check before the general floor probe). **Ceiling and wall probes
  must also filter `MAT_ONEWAY`.**
- **Inc 6 `IceBlock` brush registration:** `tools/ogmap_lib/__init__.py`
  currently has no `MaterialClass.ICE` / `IceBlock` mapping. This must be added
  before `MAT_ICE` can appear in any `.map`.
- **Inc 8 material catalog sync:** one-way/ice materials must be added to the
  manifest and material catalog with the `TB_empty` null-slot reservation
  intact, or material indices shift and break rendering. Any new texture
  sprites must also be added to `DFS_TEX_FILES` in the Makefile.
- **Inc 8 default ROM boot path:** the current ROM boots into the Forsaken
  City map-pack (`forsyken-city`) by default. The single-room `first-room`
  extension must be exercised via a host fixture test or by a temporary
  single-room boot override; otherwise the default boot will not test it.

## Out of scope

- Fixed-point conversion (spec §4 escape hatch; user confirmed keep float).
- Swept-capsule collision rewrite (user confirmed extend existing motor).
- C-file restructure (user confirmed keep C++ split).
- Feather movement, spring launching, bubble/cutscene states (spec §8 lists
  these as "do not implement until core movement is working").
- 8-way dash quantization (docs/movement_spec.md lists as a non-goal while
  analog-first).
- World-level hitstop (docs/movement_spec.md lists controller-local hitstop
  as the current approach).
- Audio/animation polish (spec §69 Phase 10; future work).
