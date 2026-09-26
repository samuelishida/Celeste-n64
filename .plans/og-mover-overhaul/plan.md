# OG Mover Overhaul: rebuild Madeline's controllers/physics as a literal Celeste64 port

## Context

After four repair cycles (`wall-climb-overhaul`, `climb-and-zoom-fix`,
`climb-parity-3`, plus the frozen-climb geometry fix), Madeline still misbehaves
on device and now has a NEW bug: **she clips through walls and floors**. The
user asked for a complete controls/physics redesign from scratch, following the
OG Celeste64 spec (`docs/movement_spec.md`, recently replaced with the Portuguese
reverse-spec of upstream `Source/Actors/Player.cs`; the vendored
`Celeste64-og/` checkout is the authoritative source of truth for everything past
its truncated §4).

Root cause is architectural, not constant drift. OG Celeste64 has exactly ONE
mover and ONE repair primitive, called once per tick from the state machine:

- `SweepTestMove` (Player.cs:642-660): 2-unit substeps, `Popout(resolveImpact)`
  after every substep.
- `Popout` (Player.cs:669-718): GroundCheck ray (5 units down from feet+5) →
  ceiling check → wall check at waist (feet+3) AND head (feet+10), radius
  `WallPushoutDist` (3), full-3D position pushout + impact-only velocity
  cancellation against the true face normal.

The demake instead has four parallel repair paths that must all agree every
tick (explore report, file:line):

1. `PlayerMotor::Step` (player_motor.cpp:196-380) runs per-substep floor sweeps
   (only when `vy<0`) and wall sphere-overlaps, then post-loop unconditional
   wall pass + floor probe + slope re-probe + **50-unit ground snap**.
2. `SweepSphereMesh` (coll_mesh.cpp:413-509) is not analytic — 8 linear
   samples; at `ground_snap_distance = 50` that is 6.25-unit sample gaps
   **larger than the 5-unit sphere** → thin ledges/floors can be missed.
3. Climb corner-adopt repositions (`ApplyCornerAdopt`,
   player_controller.cpp:152-170) and the corner-ease lerp
   (player_controller.cpp:628-647) write `position` directly, **outside the
   motor, with zero collision validation** — the primary suspect for the new
   wall-clip.
4. Wall checks are sphere-overlap only, never swept; contiguity relies on
   `sweep_step (5) ≤ 2·radius (10)`, and during climb the radius drops to 3,
   shrinking the margin to 1 unit.

`restore-controls` proved the *constants* are already OG-exact; this plan
therefore does **not** touch the tuning table, and it does **not** re-port the
feel math that already exists (see M1 below). What is genuinely new is
RELOCATION + GATE CHANGE: move ALL gameplay movement (incl. climb) onto one
swept mover (`MoveService`), and swap the floor/snap/wall gates to OG's ray +
plane-cross-section checks. That relocation, not a rewrite of the movement
formulas, is what closes the clip and the divergent-repair-path bugs.

## Architectural decisions

- **Decision: literal OG mover** — a new `MoveService` (files
  `src/user/gameplay/player/player_movement.{hpp,cpp}`, matching
  `docs/controller_spec.md` §68 module naming) implements
  `SweepTestMove` + `Popout` + `GroundCheck`/`CeilingCheck` exactly, and is
  called ONCE per tick by `PlayerController` after the state handler runs
  (OG ordering: state handler edits velocity only; movement is applied
  afterwards; Player.cs:341-373). Rationale: one repair path cannot disagree
  with itself; four patch cycles failed to converge on the parallel-pass
  design. Alternatives rejected: a 4th patch cycle on the current motor
  (has not converged), minimal-mover-first (skips Skid/ledge-avoid the user's
  spec explicitly lists). **The substep-sweep + per-substep-popout algorithm
  is what `PlayerMotor::Step` already does — this MOVES it into `MoveService`,
  it does not reinvent it.** The feel formulas stay where they are (byte-
  unchanged, M1).
- **Decision: controller owns the state machine and calls the mover; the motor
  is demoted, then deleted.** `PlayerController` gains the OG handler
  structure (Normal / Dashing / Skidding / Climbing as separate handlers);
  `PlayerMotor` TUs are replaced by `player_movement.*` once the last caller
  migrates (Inc 2), and the Makefile `-O2` per-TU override moves from
  `player_motor.o` to `player_movement.o`.
- **Decision: port OG's collision primitives 1:1 (Z-up→+Y-up)**, replacing the
  demake gates (0.70-sphere floors, 0.35+confirm, 50-unit snap):
  - `GroundCheck` = ray down from feet+5, length 5.01, `FloorNormalCheck`
    (`normal.y > 0 && |n.y| ≥ 0.35` — Player.cs:886, 2280-2295).
  - `CeilingCheck` = ray up from feet+1, height 12 (Player.cs:2297-2308).
  - `Popout` walls = horizontal **plane-cross-section** nearest-wall check at
    waist (feet+3) and head (feet+10), radius 3 (OG `SolidWallCheckNearest`,
    World.cs:481-556) — the demake's geometry-bounded plane query family
    (`QueryWallsFacePlane`, world.cpp) already provides the mesh side; add the
    cross-section pushout math as a query wrapper (`QueryWallPushout`).
  - Ground snap = ray down **5** (was demake 50), gated by the EXISTING
    `ContactState::ground_snap_cooldown_remaining` field (player_state.hpp) —
    the field already exists; only the DISTANCE changes 50 → 5 (OG
    Player.cs:399-406, 711-712). The 50-unit snap and the sphere-sweep floor
    gates die with the motor (M4: no new cooldown field is invented).
- **Decision: climb moves through the same mover** (OG: `SweepTestMove(move *
  ClimbSpeed * dt, false)` inside StClimbing, Player.cs:1499-1531). Corner
  adopts keep OG's structure (probe → adopt new wall → eased 0.2 s
  position/facing lerp) but the resulting per-tick displacement is validated
  by next-tick `Popout` exactly as OG tolerates it (Player.cs:1623-1630) —
  with a regression fixture asserting no residual penetration.
- **Decision: port the Skidding state** (user spec §2 lists it as locomotion;
  demake removed it — only a dead `skidding` field remains in
  player_state.hpp:139). OG constants: SkidDotThreshold −0.7, SkiddingStartAccel
  300, SkiddingAccel 500, EndSkidSpeed 0.8×MaxSpeed, SkidJump speed/hold/xy =
  120 / 0.16 s / MaxSpeed*1.4 (Player.cs:72-79, 1356-1429).
- **Decision: port OG's Normal-state feel details as one feel increment (Inc
  4)**: ledge-avoidance wall-follow input rotation (Player.cs:1057-1082),
  slope max-speed mod (Player.cs:1036-1040), analog-magnitude map
  `ClampedMap(|move|, .4, .92, .3, 1)` (Player.cs:1047-1052), `coyote_y`
  snap-back (OG `coyoteZ`, Player.cs:409-411, 726), jump buffer aligned to
  OG's device 0.1 s AND the symmetric 0.10 s dash input buffer (OG applies
  the VirtualButton press buffer to Dash too), dash continuous
  rotate-and-re-set steering (Player.cs:1288-1293), `tNoDashJump` 0.1
  gating dash jumps (Player.cs:1300-1305).
- **Decision: keep demake-owned infrastructure** — fixed timestep pipeline,
  platform carry (`AdvanceMovingSurfaces` + carry storage), respawn/camera,
  input bindings, camera zoom deviations (documented in
  `docs/movement_spec.md`), kill plane at demake's −200 (OG −100 is a world
  difference, not feel). Platform-velocity boost clamps adopted from OG where
  cheap (AddPlatformVelocity clamps Z to [0,180], XY to 300 — Player.cs:817-834).
- **Decision: the tick order inverted to OG's shape**
  `Timers/Input → State handler (velocity edits) → SweepMove(velocity*dt) →
  LateContact (GroundCheck, coyote refresh, landing, dash refill, ground snap)
  → moving-surface carry`. `PlayerMotor::Step`'s substep-internal contact
  passes are replaced by Popout-inside-SweepMove.

## Assumptions and answers from code

- Decision: constants stay OG-exact; no tuning in this plan. Source: code @
  movement_config.hpp:5-75 vs Celeste64-og Player.cs:8-91; restore-controls
  plan (byte-equal verification).
- Decision: OG Position = feet; demake position = center; every OG Z-offset
  converts to feet-anchored (+Y): waist = center.y − half_height + 3, head =
  center.y − half_height + 10, ground ray origin = center.y − half_height + 5.
  Source: code @ Player.cs:154-157 vs player_motor.hpp half_height=10.
- Decision at plan time: single climb entry site exists and already matches OG
  TryClimb (climb-parity-3 Inc 2 done); this plan re-wires its *repositioning*
  through the mover and initially keeps the facing-dot ≥ 0.5 gate. That gate is
  removed by the 2026-09-25 OG side-facing climb-entry parity follow-up below;
  neutral-or-into-wall stick and ClimbNormalCheck < 0.35 remain.
- Decision: the user was unavailable for the question gate; recommended
  defaults chosen autonomously (full OG mover; OG collision primitives; port
  Skidding; clip-context "not isolated — fix the class, verify by capture").
  Source: user-confirmed via empty answers ("work autonomously").
- Decision: `docs/movement_spec.md` is now the OG reverse-spec (Portuguese,
  truncated at §4) and must NOT be overwritten — implementation notes append a
  new "Demake implementation" section instead. Source: git diff shows full
  replacement dated 29/08/2026.
- Decision: pending working-tree changes (world.cpp plane-bound +23 lines,
  wall_plane_query_smoke +58 regression, controller macro fix, movement_spec
  rewrite) are correct and get committed BEFORE Inc 1 (repo memory
  celeste-frozen-climb: this is the frozen-climb pump fix). Source:
  `git status` / `.agents/common-mistakes/plane-query-geometry-bound.md`.
- Decision: host fixtures are collmesh-synthetic (floor+wall quads via
  `CollHeader/CollVertex/CollTriangle/BVH` vectors, Room stub with
  `coll_mesh` pointer) and drive full `motor.Step + controller.Step` tick
  loops — the overhaul's fixtures reuse this pattern. Source: code @
  tests/motor_wall_rest_smoke.cpp:23-73, tests/climb_mantle_smoke.cpp:88-130.
- Decision: `MovementConfig` vs `MovementProfile` duplication (config.hpp:1-96)
  gets collapsed onto MovementConfig in Inc 2 — Profile is a second,
  partially-divergent copy of the same numbers (e.g. neutral walljump split
  exists only in Profile). Source: code @ movement_config.hpp:62-96,
  player_controller.cpp:298.

## Risks accepted

- **Feel regression on the baked map's slopes/stairs** (OG ray-ground + 5-unit
  snap vs demake's forgiving gates): accepted — OG numbers on OG-shaped
  geometry is the point of the overhaul; the Ares checklist includes the
  stair/slope cells, and the old constants remain in git for instant revert.
- **Host-fixture churn**: tick semantics change (single mover call vs
  substep passes); ~10 motor-based fixtures rewire in Inc 2. Mitigation:
  fixtures migrate WITH the inversion increment; the runner stays
  toolchain-free so iteration is cheap.
- **Climb re-clip risk**: corner adopts still write positions; mitigation =
  OG-parity ease (position written, Popout recovers next substep) + new
  no-penetration regression fixture (3 ticks after any adopt → sphere query
  reports no overlap).
- **Platform-carry regressions** (moving solids + stored velocity): demake
  carry path untouched except the boost clamps; moving_solid tests protect.
- **Skid triggering mid-combat-with-camera-turn** (OG triggers on input-vs-vel
  dot, camera-relative input): accepted; OG shipped this exact logic with the
  same camera-relative formula (§3.2 of the spec is Player.cs:597-620).

## Increment DAG

- Inc 0 — Commit pending fixes (S) — depends on: none — unblocks: 1
- Inc 1 — OG collision queries + MoveService core (M) — depends on: 0 — unblocks: 2
- Inc 2 — Controller inversion + motor deletion (M) — depends on: 1 — unblocks: 3, 4
- Inc 3 — Climb onto the mover + clip regressions (M) — depends on: 2 (transitively 1) — unblocks: 4, 5
- Inc 4 — OG feel: dash buffers/steering + Skid + ledge-avoid + slope/analog (M) — depends on: 2, 3 (serialized after 3: both edit player_controller.cpp; ledge-avoid interacts with climb entry) — unblocks: 5
- Inc 5 — Close-out: telemetry, docs, device gate (S) — depends on: 3, 4 — unblocks: none

## Increments

### Inc 0 — Commit pending fixes (S) — [done]
**Depends on:** none
**Unblocks:** 1
**Done criteria:** working tree clean; the frozen-climb plane-bound fix,
macro fix, spec rewrite, and the query regression are committed in ≤3
logically-separated commits; host suite green.

#### Files to touch
- Working tree only: `git add` the SIX modified/new files listed in
  `git status` (world.cpp, wall_plane_query_smoke.cpp,
  player_controller.cpp, input_system.hpp, movement_spec.md,
  .agents/common-mistakes/plane-query-geometry-bound.md) as:
  1. `fix(climb): geometry-bound plane query + regression` (world.cpp +
     tests + common-mistakes note)
  2. `chore: N64 macro + cast hygiene` (player_controller.cpp,
     input_system.hpp)
  3. `docs: replace movement spec with OG reverse-spec`

#### Edge cases
- Any test red at commit time = the stale-contract rule applies
  (`.agents/learnings/stale-contract-vs-working-tree.md`): the new regression
  is authoritative for the plane-bound.

#### Verification
- Run: `./tests/run_host_tests.sh` (green), `git status` (clean).

### Inc 1 — OG collision queries + MoveService core (M) — [done]
**Depends on:** 0
**Unblocks:** 2
**Done criteria:** a new `MoveService` passes a fixture suite proving OG-exact
behavior on synthetic collmesh: (a) a 140 u/s dash into a 1-unit-thick wall
never tunnels (final position outside the wall, no interior tick), (b) walking
off a step-down snaps within 5 units via the cooldown-gated snap, (c) a 35°
slope sets `grounded` with `ground_normal` from the ray, (d) walking into a
wall pops out to radius 3 at waist AND head with the face normal, falling
speed preserved tangentially. Nothing in gameplay calls it yet (dead code +
tests only); tree compiles and old suite still green.

#### Files to touch

##### src/user/gameplay/player/player_movement.hpp (NEW)
- What changes: `MoveService` + `MoveServiceConfig`.
- Function(s):
  - `struct MoveServiceConfig { float substep = 2.0f; float wall_radius = 3.0f;
    float ground_ray_up = 5.0f; float ground_ray_len = 5.01f;
    float ceiling_probe_up = 1.0f; float ceiling_height = 12.0f;
    float climb_wall_radius = 3.0f; float ground_snap_distance = 5.0f;
    float ground_snap_cooldown = 0.10f; }`
  - `bool GroundCheck(PlayerState&, const Room&, Vec3* pushout, Vec3* normal) const;`
  - `bool CeilingCheck(PlayerState&, const Room&, Vec3* hit) const;`
  - `void SweepMove(PlayerState&, const Room&, Vec3 amount, bool resolve_impact) const;`
    — OG algorithm verbatim: remaining = amount.Length(); while remaining>0:
    step = min(remaining, substep); position += step_normal*step;
    `Popout(resolve_impact && first_wall_this_move)` per substep; then one
    unconditional `Popout(false)` after the loop (Player.cs:642-660, 368-373).
  - `bool Popout(PlayerState&, const Room&, bool resolve_impact) const;` —
    OG order: GroundCheck → pushout + on-resolve `vy = max(vy, 0)`; else
    CeilingCheck → `vy = min(vy, 0)`; else wall at waist then head →
    `position += pushout`, and if resolve_impact:
    `velocity -= normal * velocity.Length() * min(0, dot(v̂, normal))`
    (Player.cs:669-718). Returns "repaired something".
  - `void GroundSnap(PlayerState&, const Room&, float dt) const;` —
    ray down 5 from feet when recent-ground &&
    `state.contact.ground_snap_cooldown_remaining <= 0` (field already
    exists, player_state.hpp; Player.cs:399-406).
- Data shapes: uses `PlayerState`, `Room`; writes position/velocity/
  ground_normal/grounded.
- Integration points: consumed by PlayerController (Inc 2/3); world queries
  below.
- Error paths: room == nullptr → no-op (fixture compatibility, same shim as
  climb-parity-3).

##### src/user/gameplay/world/world.hpp / world.cpp
- What changes: NORMALIZE the existing queries to the OG gates — do NOT add
  near-duplicate `QueryFloorRay`/`QueryCeilingRay` wrappers (a second
  floor/ceiling family with slightly different normal gates is exactly the
  probe-shell divergence class that caused the climb bugs):
  - `QueryFloorSource` (world.cpp:419): confirm its floor gate is the OG
    `FloorNormalCheck` (`normal.y > 0 && |n.y| ≥ 0.35`, Player.cs:886); if it
    gates differently, change the gate IN PLACE. It already returns a
    `GroundHit` with normal — `MoveService` consumes it directly; no new
    `QueryFloorRay`.
  - `QueryCeilingSource` (world.cpp:426): likewise confirm the gate in place;
    no new `QueryCeilingRay`.
  - `RaycastRoomSource` already applies the backface + face-identity policy
    `MoveService` needs — reuse, don't re-wrap.
  - `bool QueryWallPushout(const Room&, const Vec3& point, float radius, WallHit* out)`
    — add ONLY this (no demake equivalent exists): the horizontal
    plane-cross-section nearest-wall port (OG `SolidWallCheckNearest`,
    World.cs:481-556: reject |n.y|≈1 faces, intersect the face plane with
    the horizontal plane at point.y, closest point on that line, pushout =
    (r − dist) along the in-plane direction), geometry-bounded like the
    climb-plane query (infinite-plane trap per learnings/
    climb-probe-shell-geometry-coupling), oriented toward the query point.
- Function(s): two gates normalized + one new query; `WallHit` reuse (add
  `pushout` if absent).
- Integration points: MoveService only (climb queries unchanged).
- Error paths: null mesh → miss; ray epsilon reuses `kRayEpsilon`.

##### Makefile
- What changes: add `player_movement.o` to the gameplay TU list; (Inc 2 will
  move the `-O2` override here).

##### tests/move_service_smoke.cpp (NEW)
- What changes: five fixtures using the SyntheticMesh idiom (floor+wall
  quads, collmesh vectors + BVH, Room.coll_mesh pointer — pattern from
  tests/motor_wall_rest_smoke.cpp:23-73). **Reuse the collmesh geometry of the
  existing `motor_wall_rest_smoke` / `player_motor_collmesh_test` fixtures
  where they exercise the same shapes instead of authoring five brand-new
  meshes** (C2) — re-deriving identical vectors wastes effort and risks the
  new fixtures drifting from the motor ones they replace:
  (a) anti-tunnel dash, (b) step-down snap, (c) 35° slope ground, (d) waist
  AND head wall popout, and (e) **normal-state wall-rest equilibrium**:
  walk into a flat wall at full speed for 60 ticks → body center rest
  distance = wall_radius (3) from the face ±0.25, zero-popout drift < 0.5
  over the final 30 ticks (the `sweep_step ≤ 2·wall_radius` contiguity
  audit for NORMAL locomotion, applying the coupling rule at the increment
  that introduces radius 3 — not only in Inc 3's climb fixture).
- Integration points: new `run_cpp` line +
  `MOVE_LINK=(player_movement world coll_mesh geom math)` in
  tests/run_host_tests.sh.

#### Edge cases
- Substep contiguity + cost: substep 2 < 2·wall_radius(3) — no tunnel gap.
  Per-tick query cost is bounded and justified: Popout runs ground ray (1
  raycast) + ≤2 wall cross-section queries per substep (≤~3 substeps/tick at
  140·(1/60)=2.3 units → ≤8 primitive queries/tick) — comparable to the
  motor's current per-substep passes; the Inc 2/5 device captures assert
  `[profiler]` update stays ≤0.25 ms (vs the measured 0.194 ms budget,
  learning: n64-opt-device-verify-and-telemetry — measure, don't assume).
- Dash diagonal into a corner: head-check catches over-waist faces; fixture
  includes a head-only lip.
- `resolve_impact` fires once per move (not per substep) — OG
  `resolveImpact = false` after the first repair (Player.cs:648-652); fixture
  asserts velocity not re-scrubbed on a resting contact.
- Slope walking INTO a wall that is also the floor's continuation: wall gate
  rejects near-vertical only; floor ray wins first (OG ordering).
- Normal-state body rest distance changes from the old motor tangent (5) to
  OG's 3: this is a DELIBERATE collision-radius change coupled to the new
  wall_radius — locked by fixture (e) above in the SAME increment (coupling
  rule); documented in the Inc 5 deviation ledger.

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add: the five fixtures above; assert interior-point test via
  `QueryWallPushout` (position strictly outside every wall face after the
  move).
- Done: suite green, `grep -rn MoveService src/` shows zero gameplay callers
  (integration is Inc 2).

### Inc 2 — Controller inversion + motor deletion (M) — [done]
**Depends on:** 1
**Unblocks:** 3, 4
**Done criteria:** `PlayerController::Step` shape is OG's: TimerInput →
state handler (velocity edits only) → `SweepMove(velocity·dt, tNoMove≤0)` →
Popout(s internal to SweepMove) → LateContact (GroundCheck + coyote refresh,
`coyote_y` + landing dash-refill + ground snap). Dash port is
**behavior-minimal** (enter/exit/end-mult, no steering refinements — those are
Inc 4); `PlayerMotor` and its parallel-pass `Step`/`RefreshContacts` are
deleted; all gameplay movement flows through MoveService; a per-tick `[move]`
telemetry line lands here (clip forensics available from the first device
capture, not only in Inc 5). gameplay_smoke (jump/dash/pickup/respawn) green;
jump height, dash distance, coyote, and buffer behaviors match host asserts;
ROM boots with `[climb]`/`[input]` telemetry unchanged in shape.

#### Files to touch

##### src/user/gameplay/player/player_controller.hpp/.cpp
- What changes:
  - `Step(...)` restructured to the OG pipeline above; state handlers
    extracted as private methods `StNormal(dt)`, `StDashing(dt)`,
    `StClimbing(dt)` (climb body moves to Inc 3 — interim: existing climb
    block keeps working through the new move call).
  - **Move-math is NOT re-ported (M1).** The ground/air Approach+rotate
    blocks, Jump(), hold-jump + half-gravity, WallJumpCheck, TryDash + dash
    enter/exit, coyote + jump-buffer + coyote-height snap-back,
    `AnalogMagnitude`, rotate-threshold turning and air accel-mult already
    live in `player_controller.cpp` at OG-exact values and were byte-verified
    by `restore-controls` (:248-255, 310-322, 390-403, 990-1038). This
    increment leaves them BYTE-UNCHANGED; it only (a) inverts the tick order,
    (b) routes the resolved move through `MoveService`, (c) deletes
    `PlayerMotor`. Any feel deltas (dash steering + buffer, `coyote_y` snap
    refinement, skid, ledge-avoid, analog-map adjustment, slope mod) are Inc 4.
  - **StClimbing at this increment's boundary** (no re-derivation): the
    existing climb-parity-3 sustain/entry/corner logic is transplanted
    verbatim as the body of `StClimbing`; its only change is that movement /
    entry-reposition calls route through `Mover().SweepMove` instead of
    direct `position` writes (the ease lerp block stays as-is until Inc 3
    formalizes it). No gate/probe semantics change here. If this transplant
    proves larger than one PR, split as declared: Inc 2a (Normal+Dash
    inversion) + Inc 2b (Climb transplant) — the default fallback, not an
    afterthought (C1).
  - Timers: DO NOT add `t_ground_snap_cooldown` — the field
    `ContactState::ground_snap_cooldown_remaining` already exists and is
    reused (M4); `t_no_dash_jump` is added when the dash-jump feel lands
    (Inc 4) — declare the field now, consume later.
  - LateContact: `GroundCheck`; on ground → `tCoyote = 0.12`,
    `coyote_y = position.y`, `RefillDash()` when `tDashResetCooldown <= 0`;
    landing squash hook left for the render layer (out of scope here).
  - `MovementProfile` struct deleted; all references collapse onto
    `MovementConfig`. `MovementConfig` field inventory at this increment:
    DELETE `respawn_fall_height` consumers audit only (keep), mark
    `PlayerMotorConfig::sweep_step/ground_snap_distance/ground_contact_tolerance`
    dead (used only in Inc 1/2 fixtures after migration).
  - Consumers inventory FIRST: `grep -rn "PlayerMotor\|player_motor" src/ tests/`
    before editing; enumerate every motor contact-output consumer
    (gameplay_scene telemetry `:1031-1054`, respawn_system, moving_solid
    carry) and migrate each call site in this increment — the grep done
criterion below is the closure check, not the discovery mechanism.
- Function(s): signatures unchanged publicly (`Step(state, input,
  camera_forward, dt)`); internals restructured; delete
  `player_motor.hpp/.cpp` calls.
- Integration points: gameplay_scene.cpp tick loop (scene still calls
  AdvanceMovingSurfaces → controller.Step per tick; motor include removed);
  gameplay_smoke / climb fixtures updated to tick without PlayerMotor.
- Error paths: room == nullptr shims preserved for controller-only fixtures.

##### Makefile
- What changes: `-O2` per-TU override moves from `player_motor.o` →
  `player_movement.o` (hot-TU list, Makefile:273-278).

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: ONE ungated `[move]` debugf beside the `[input]` dump —
  `[move] pos=(%.1f,%.1f,%.1f) v=(%.1f,%.1f,%.1f) g=%u st=%s` every 30th
  frame (clip forensics from the first device capture; grep-able in-geometry
  positions). For the specific no-clip PROOF, run ONE additional capture at
  full frame cadence (every frame) — a cheap one-off that makes in-geometry
  positions unambiguous (C3).
- Integration points: per-frame block near the `[input]` dump (:1107); NOT
  per-tick (frame cadence is enough for forensics, cheaper; the C3 full-cadence
  capture is the exception).

##### tests (migrate)
- gameplay_smoke, movement_parity_smoke, movement_contracts_test,
  player_phase_smoke, player_motor_smoke (→ move_service or
  controller-level), motor_wall_rest_smoke (→ move_service_wall_rest),
  player_motor_collmesh_test (→ move_service_collmesh), wall_slide_seam_smoke:
  replace `motor.Step(...) → controller.Step(...)` orchestration with
  controller-owned SweepMove; assertions on position/velocity semantics
  identical where OG semantics identical; wall-rest and slide fixtures keep
  their geometry.

#### Edge cases
- `tNoMove` gates resolve_impact exactly like OG (mantle no-move window
  doesn't re-scrub velocity).
- Landing ON a slope while `vy < 0`: GroundCheck ray normal gates
  `FloorNormalCheck`; steep (≥ ~69°) faces are walls per ClimbNormalCheck —
  cannot ground (OG parity), she slides/falls instead of standing.
- Platform carry: `AdvanceMovingSurfaces` runs BEFORE the tick (scene order
  kept); stored-velocity consumption inside Jump/WallJump clamps Z [0,180],
  XY 300 (AddPlatformVelocity, Player.cs:817-834).
- Respawn/`no_move` paths: StRespawn-equivalent skips SweepMove (OG early-out
  states list, Player.cs:300-305).
- Respawn camera coupling re-verified after the Step restructure:
  `RespawnSystem::Step` returning true still ends in
  `CameraController::Reset(camera, player.position)` for that frame
  (`.agents/common-mistakes/camera-respawn-reset.md`;
  `tests/scene_update_order_smoke.cpp` guards it).
- Hitstop stays controller-local (documented deviation): when dash hitstop
  active, SweepMove amount = 0.

#### Verification
- Run: `./tests/run_host_tests.sh` (all green incl. migrated fixtures);
  `./compile-rom.sh` clean; 30 s Ares capture
  (`tools/capture_baseline.sh madeline_cube_rom.z64 30 inc2`) — boot healthy,
  `[climb]`/`[input]` lines present, no `[respawn]` spam.
- Tests to add: `tests/controller_inversion_smoke.cpp` — full-stick speed 64,
  jump apex asserted as a RANGE accounting for hold-jump + half-gravity
  (apex > 90²/(2·600) because the 0.1 s hold at 90 and the 0.5× mult raise it;
  assert a bracketed window rather than a single `≈`, S2), dash 0.2 s at 140
  end 0.75×, coyote 0.12 + buffer 0.10, walljump SET 83.2/90.
- Done: grep `player_motor` returns only plan/docs; ELF builds; device capture
  shows normal boot + walk.

### Inc 3 — Climb onto the mover + clip regressions (M) — [done]
**Depends on:** 2 (1 transitively)
**Unblocks:** 4, 5
**Done criteria:** climb entry/corner/mantle all request motion through
MoveService; corner adopts and the 0.2 s ease end with ZERO sphere
penetration (fixture: after adopt + 3 ticks, waist/head pushout queries at
radius 3 report no wall deeper than equilibrium); climb_parity/mantle/corner
fixtures green; a fast exit-dash at a corner cannot pass through the wall
(dash-through-corner fixture).

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp (StClimbing)
- What changes:
  - Wall movement becomes `SweepMove(move * climb_speed * dt, false)`
    (Player.cs:1499-1531 ordering: wallUp/wallRight basis, input sign flip
    when facing opposite camera — port `climbInputSign`, Player.cs:1492-1497;
    zero dead-zone components < 0.1; down-cancel while grounded; spikes-above
    gate n/a until spikes exist).
  - Corner adopt + ease: keep climb-parity-3 mechanics (side ray, slope snap,
    outer forward+side ray, 0.2 s ease) BUT position application goes through
    `SweepMove` for continuous paths; only the eased lerp writes position
    directly (OG does exactly this, Player.cs:1623-1630), and after each ease
    tick a `Popout(false)` runs to guarantee non-penetration.
  - `Entry reposition`: sweep to
    `wall.Point + (waist_offset preserved) + normal * 3` (OG TryClimb sweep,
    Player.cs:854-879): implement as an explicit displacement —
    `SweepMove(target_center - position, false)` — with the +4 airborne
    snap-up variant realized as the same sweep after lowering the probe
    origin (OG semantics: sweep to the found wall's surface; the +4 probe
    only WIDENS the grab set, the sweep target is identical). The corner-adopt
    paragraph above applies to the continuous corner paths; the eased lerp's
    explicit position write stays as the single allow-listed exception (M2:
    removed the duplicated second copy here).
  - Mantle hop velocity + timers unchanged (80/40, 0.25 s / 0.3 s cooldown).
- Function(s): `StClimbing(dt)`; helpers `ReacquireGrip`, `ClimbCheckAt`,
  `InnerCornerSideRay`, `SlopeSnapRay`, `OuterCornerRay` retained.
- Integration points: climb telemetry sites (gameplay_scene.cpp:939-966)
  untouched.
- Error paths: room == nullptr shim retained (controller-only flag climbs).

##### tests
- Update: climb_parity_smoke, climb_mantle_smoke, climb_corner_smoke tick
  loops to `controller.Step` (no motor call), assertions unchanged.
- New: `tests/climb_no_clip_smoke.cpp` — corner adopt + 3 ticks →
  `QueryWallPushout(waist,3)` and head == no-hit; dash while climbing at a
  convex corner wall ends outside geometry.

#### Edge cases
- Corner ease interrupted by release: release check stays BEFORE the ease
  branch (OG 1451 ordering — already demake-correct).
- Corner ease vs gravity: ease branch zeroes velocity (parity-3 behavior
  kept); gravity resumes on ease end.
- Grabbing during SweepMove (mid-substep wall contact): climb entry evaluates
  after the move completes (OG order: state switch next Update), no
  same-tick double-move.

#### Verification
- Run: `./tests/run_host_tests.sh` (climb_* all green); `./compile-rom.sh`.
- Device: 30 s capture with intentional corner walk; assert ZERO positions
  where `[climb] CORNER` is followed by out-of-geometry behavior — proxy
  gate: no `[climb] START` spam (>2/s sustained) at one position.
- Done: fixtures + capture; grep shows no direct `position` writes outside
  `SweepMove`/ease in climb paths (allow-listed ease block only).

### Inc 4 — OG feel: dash buffers/steering + Skid + ledge-avoid + slope/analog (M) — [done]
**Depends on:** 2, 3 (serialized after 3 — both edit
player_controller.cpp; ledge-avoid rays interact with climb entry probes)
**Unblocks:** 5

**Follow-up audit (2026-09-25):** the earlier DONE label overstated parity:
`ledge_sweep_dist` existed but was unused, and `ledge_smoke` only asserted
that some movement occurred. Restored OG's feet-anchored floor/wall probes and
bounded side sweep in grounded Normal movement. Strengthened the regression so
it fails without supported-side steering and also checks clear-path, wall-ahead,
idle, and roomless behavior. Host suite 67/67 and ROM build pass. This fixes grounded
edge-follow input only; it does not establish wall/camera visual clearance.

**Done criteria:** reversing at ≥ RotateThreshold triggers Skidding (state
visible, EndSkidSpeed exit at 0.8×max, SkidJump available after 0.1 s);
dash has the full OG steering loop (rotate at DashRotateSpeed + re-
`SetDashSpeed`, continuous steer gate `Dot(move, facing) >= -0.2`) and a
0.10 s dash input buffer (OG VirtualButton device buffer, matching Jump's);
jump buffer at 0.10 s; coyote jump snaps to `coyote_y` takeoff height;
near an unsupported edge, input rotates toward a side with supported floor;
ordinary wall-ahead movement remains unchanged;
downhill/uphill modulates max speed; partial stick maps to
`ClampedMap(.4,.92,.3,1)` max-speed scale. All host-asserted.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp (StNormal)
- What changes: port Skidding state (enter/exit per Player.cs:1356-1429), the
  ledge-avoid input-rotation block (1057-1082 — ray d=4, rotate ±0.3 rad per
  0.1 step, ≤10 tries), slope max-speed mod (1036-1040), analog magnitude map
  (1047-1052). Add `skid` constants to MovementConfig (currently missing —
  demake deleted the state in the controls refactor).
- Function(s): `StSkidding(dt)`; skid triggers inside the ground rotate
  branch.
- Data shapes: `PlayerState.skidding` field exists (dead) — becomes the
  active state flag/timer set (`tNoSkidJump` added to PlayerState).
- Integration points: state machine registration; gameplay_smoke test list.
- Error paths: input near-zero cancels skid to Normal (OG 1382).

##### src/user/gameplay/player/movement_config.hpp
- What changes: add `skid_dot_threshold = -0.7f`, `skidding_start_accel =
  300`, `skidding_accel = 500`, `end_skid_speed = 51.2` (0.8×64),
  `skid_jump_speed = 120`, `skid_jump_hold_time = 0.16`,
  `skid_jump_xy_speed = 89.6` (1.4×64); set `jump_buffer_time = 0.10f`
  (OG device buffer, from 0.08); add `dash_buffer_time = 0.10f` (OG
  VirtualButton press buffer applies to Dash too) with the symmetric
  `t_dash_buffer` timer in PlayerState consumed by TryDash. **Dash-buffer
  semantics (S1):** the press is consumed on acceptance (`ConsumePress()`, the
  existing edge-consume fast path stays); a press with no dash opportunity at
  that instant stores the buffer flag/timer, and `TryDash` drains it within
  0.10 s if a dash opportunity opens. It is a grace window on top of
  edge-consume, NOT a re-consumable held flag.

#### Edge cases
- Skid during tNoMove (post-mantle): blocked by tNoMove gate (OG checks
  on-ground branch order).
- Analog flicker at the dot threshold: hysteresis not in OG — accept OG
  behavior, note in spec.
- Ledge-avoid rays need `BackfaceCull::Ignore` + room guard (learning:
  corner-ray room==nullptr segfault).

#### Verification
- Run: `./tests/run_host_tests.sh`; new `tests/skid_smoke.cpp` (trigger,
  exit-at-speed, skid jump, dash rotate/SetDashSpeed steering, dash-buffer
  acceptance window) + extend movement_contracts for the magnitude map and
  slope mod.
- Device: 30 s capture with the Inc 2 `[move]` line — named assertion:
  ≥1 frame showing `state=SKID` (or demake state string) during a scripted
  AutoWalk-against-wall segment; no `[respawn]` anomalies.
- Done: suite green + the feel section in movement_spec ("Demake
  implementation") updated.

### Inc 5 — Close-out: telemetry, docs, device gate (S) — [implementation complete; final acceptance pending]
**Depends on:** 2, 3, 4
**Unblocks:** none
**Done criteria:** continuous Ares session (≥120 s, kDebugAutoWalk walk +
manual checklist) shows: zero out-of-geometry events (no wall/floor pass —
checked by scripted position sanity in capture log [fixture]/[respawn]
anomalies), ≤1 `[climb] START` per intentional grab, 0 phantom grabs at rest
(post-remap `z=0 l=0 r=0`), jump/dash/skid/climb checklist passes. Host suite
100% green. Documentation reflects the new mover.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: add ungated `[move]` line per tick sampling
  `pos=(x,y,z) v=(...) grounded=%u state=%s` every 30th frame (clip forensics:
  a position inside a wall is grep-able) + keep `[climb]`/`[input]` lines as
  they are.
- Function(s): one debugf site beside the existing `[input]` dump (:1107).
- Error paths: none (debugf-only).

##### docs/movement_spec.md
- What changes: APPEND "## Demake implementation (og-mover-overhaul)" — mover
  mapping (SweepTestMove/Popout/GroundCheck anchors, demake Y-up conversion
  table OG-Z → demake-+Y), the deletions (50-unit snap, substep-internal
  passes, parallel floor gates), and the 
  **deviation ledger** (a checklist, each with one-line rationale):
  1. kill plane −200 (demake world) vs OG −100;
  2. hitstop controller-local (OG world-level freeze);
  3. camera zoom rates 1.8/1.0 (documented pre-existing deviation);
  4. climb entry facing-dot ≥ 0.5 gate (OG has none; removed in the
     2026-09-25 parity follow-up below);
  5. 2-tick mantle confirm (OG instant);
  6. physics/climb wall radius 3 with OG `ClimbCheckDist=4` for entry and
     sustain; static model clearance is renderer-only and must not widen the
     gameplay probe or collision shell;
  7. normal-state wall rest distance now 3 (OG WallPushoutDist) — changed
     from the old motor's 5-radius tangent (og-mover-overhaul decision,
     fixture (e) locked);
  8. ground snap: OG-shape ray down 5 with `t_ground_snap_cooldown` 0.1 s
     (this is OG's own mechanism, Player.cs:399-406, 711-712 — listed for
     completeness, not a deviation);
  9. platform-velocity clamps Z [0,180] / XY 300 (OG AddPlatformVelocity).
  Do NOT rewrite the existing OG reverse-spec sections.

##### AGENTS.md
- What changes: status bullet — movement/collision now run OG's single
  SweepTestMove+Popout mover via MoveService; the parallel-pass motor is gone;
  clipping class closed by construction (every tick ends in a Popout'd state)
  with the capture artifact cited.

#### Edge cases
- kDebugAutoWalk walks into walls by design — use it to PROVE no-clip (the
  100-unit auto-walk must end outside every wall face per the fixture idiom).
- Capture hygiene per AGENTS.md: `[input] z=0 l=0 r=0` at rest gate before
  attributing any grab line.

#### Verification
- Run: `bash tools/capture_baseline.sh madeline_cube_rom.z64 120 og-mover-final`
  → archive to `build/baseline-og-mover-final-<ts>.txt`; diff vs
  `baseline-diag-20260826-150416.txt` for regressions in
  `[profiler]`/`[counters]` — gate: `[profiler]` update ≤0.25 ms (measured
  pre-overhaul budget 0.194 ms + ≤30% headroom; substeps + ≤2 wall
  probes/tick vs today's passes must not cost CPU); confirm `[artifact]` ROM
  SHA-256 matches tested build and `[memory]` delta
  < 50 KB (no new persistent allocations).
- `./tests/run_host_tests.sh` green immediately before close.
- Done: capture cited in AGENTS.md; suite green; ROM rebuilt.

#### Revalidation (2026-09-25)

The final guided Ares route now approaches a real map wall without teleporting,
enters at 3.35 units (OG probe range 4), climbs 17 ticks, and mantles. The
machine clearance gate records 17 samples, zero violations, and 0.50 minimum
rendered-model gap. `./tests/run_host_tests.sh` passes 67/67 and the normal ROM
was rebuilt with physics autoplay disabled. This is implementation/device
telemetry evidence only; it does not close final acceptance: current interactive
Ares process predates the ROM rebuild, so latest visual state and the continuous
120 s walk/manual controller checklist remain pending.

### Grounded lip step-up migration follow-up (2026-09-25)

Audit found the old climb-and-zoom Inc 3 knee-high-lip acceptance was not
migrated when `PlayerMotor` was deleted. Reproduction stopped a grounded mover
at `x=-3` against a 5-unit lip. `MoveService::TryStepUp` now probes the
leading-edge support surface, steps only while grounded and only up to 5 units,
and rejects blocked headroom or a second wall. `move_service_smoke` covers the
5-unit mover walkover, the same traversal through actual controller input/state
ticks, 6-unit rejection, and low-overhang rejection. Full host suite passes
67/67. Fresh ROM builds with autoplay disabled. This is a collision
regression fix, not proof that the screenshot's wall/model overlap is gone;
current Ares session still needs reload for visual revalidation.

### Ground-snap landing bookkeeping follow-up (2026-09-25)

A one-tick controller regression across a 4-unit step-down showed a parity
hole: feet snapped to `y=-4`, but `grounded` and `was_grounded` remained false.
`GroundSnap` had used its confirming `GroundCheck` only to copy the floor
normal; controller coyote/dash contact bookkeeping therefore skipped the
snap tick. This also left OG `autoJump` uncleared after contact. The regression
asserts same-tick feet position, grounded/contact flags, coyote refresh, and
auto-jump clearing. Focused `move_service_smoke` passes after updating the
mover's grounded result and the controller's common landing branch. Full
host suite passes 67/67; `make madeline_cube_rom.z64` succeeds. ROM SHA-256:
`b991c51377ac91770ba83c2d69c3c7f636edf06fd4e3de0b287bd70e7636b1dc`.
Live Ares/manual controller acceptance remains pending; current process still
emits the old `[input-trace]` format.

### Per-move contact accumulation follow-up (2026-09-25)

Before-fix mover regression swept across a narrow `MAT_DEATH` strip and ended
on safe floor with `death_contact=0`: `Popout` cleared the per-move flag at
each <=2-unit repair substep. Moved death/ceiling resets to sweep entry and
made step-up, climb, and ground-snap contact writes accumulate for the whole
move. Focused mover fixture fails before/fixes after; full host suite 67/67 and
`make madeline_cube_rom.z64` pass. ROM SHA-256:
`19db75053a5971a6e83d2f81eed1d6d4a230186d6a283850f6092df8cf887053`.

This is a contact-reporting fix, not visual proof for the user's wall/model
glitch. Interactive Ares PID 190708 predates this ROM; latest visual/controller
acceptance still needs user reload and retest.

### Sustained-climb ground-snap regression follow-up (2026-09-25)

Grounded synthetic entry reproduced a real state-ordering bug: first held
up-climb tick ended with `dy=0`, `grounded=1`, and no snap cooldown. OG
`StClimbingUpdate` re-arms `CancelGroundSnap` every sustained tick after its
release/jump/dash early exits; the demake had only launch and mantle setters.
Added that refresh plus a grounded-entry regression. The real-map guided test
now starts at manifest Start, walks to face 5190, rests against it for 30 ticks
with neutral input (0.000 drift), then grabs while still grounded. First climb
step rises and mantle completes after 16 climb ticks. It records 196 waist/head
samples with zero collision-shell overlaps and 62 runtime-equivalent model
samples with zero violations (minimum gap 0.500, maximum correction 6.110);
maximum ordinary tick displacement is 1.491. Continued forward input at this
cliff edge invokes OG ledge-avoid steering, so rest telemetry releases movement
before measuring drift. Host suite passes 68/68. `make
madeline_cube_rom.z64` succeeds; SHA-256:
`bd3c4f38646fccd3c522eb4d2b5edb91c388509ed15979a8c832adccda7e75c4`.
The opt-in Ares autoplay gate now also requires grounded wall entry, first
climb-step rise ≥0.5, and positive snap cooldown after state update; the prior
10-tick aggregate missed this one-tick regression. Device rerun remains pending.
`make physics-autoplay-rom` builds the diagnostic artifact separately as
`physics_autoplay.z64` under a distinct object tree; normal ROM keeps autoplay
off. Its filesystem is included and its current hash is
`b5b8a4ff24eb2bb16b86a68933fc17baf8c7c1d76b360315c18546ba3b0f584d`.
This closes the reproducible sustained-climb snap defect, not the remaining
visual/manual acceptance gate: Ares PID 190708 was not restarted and predates
this build.

### Physics capture provenance follow-up (2026-09-25)

The existing capture script could start a second Ares beside an interactive
session and did not record which ROM file produced telemetry. It now refuses
when Ares is already live, prints ROM SHA-256/size, and prefixes captures with
`[artifact]`; ROM boot also prints `[build] compiled=...` for manual sessions.
`bash -n` passes; live-Ares refusal was verified against PID 190708. A stubbed
capture verified the metadata header against the current ROM hash
`73fc8f81f43b49e532f9a5d5aae212cca93ad5a6b082a03c95f98e66ec7e80eb`.
No second emulator was launched. Current ROM's manual boot marker is
`[build] compiled=Sep 25 2026 03:01:07`.

### Guided physics device rerun (2026-09-25)

`make physics-autoplay-rom` followed by
`bash tools/capture_baseline.sh physics_autoplay.z64 30 current-autoplay`
passes Ares route gate. Capture:
`build/baseline-current-autoplay-20260925-051503.txt`; ROM SHA-256
`a400df43bfa453aeb451d6c42ffc8cff10518105e21fe7a695d301d4ca9a9b42`.
Route starts 51.2 units from face 5190, walks 50 ticks to 3.35-unit entry gap,
enters climb while grounded, rises 0.67 on first held climb tick with 0.100 s
snap cooldown, climbs 16 ticks, and mantles. All 16 rendered-model clearance
samples pass at 0.50-unit minimum gap; zero violations. Movement gates pass:
run peak 64, brake to 0, reverse speed 64, jump rise 24, dash distance 37.7.
Idle shoulders remain zero. This is scripted device evidence, not user feel
acceptance; normal ROM `madeline_cube_rom.z64` remains the manual test target.

### Live-controller replay instrumentation (2026-09-25)

The fixed-tick `[input-trace]` now records movement and camera input, camera
forward, current/target facing, raw button bits, buffered action/edge
consumption, position, velocity, contacts, movement state,
dash/coyote/jump-hold timers, and dash
availability. `make physics-input-trace-rom` builds a separate
`physics_input_trace.z64` with live controller input and trace enabled; it does
not enable autoplay or change default logging. After the air-control and
texture-prefilter fixes below, current artifact hashes are: normal ROM
`2f90a5e14a04b7fced12dc922f561470d31745c661a675e2e067e7c13e00eace`, input
trace ROM `fbb7181dfc2adea04e066fc149fa6cc2ade55292dc1d254fd47da49b71d70eeb`,
and autoplay ROM `73d11293943244fd2475fa858d5447a9b3264d19bcb8b6ba5b7111806ccda5ae`.
Builds succeed; host suite passes 71/71. The live Ares process was not
restarted or disturbed, so controller/visual acceptance still requires loading
the current normal ROM in the user's session.

### OG air-control parity regression (2026-09-25)

Before-fix captures in `/tmp/hawk-fix-bug-air-friction-before.log` and
`/tmp/hawk-fix-bug-air-analog-repro.log` reproduced two mismatches against the
local OG `Player.cs`: no-input air velocity failed to decay from 64 to 24 over
30 ticks, and half-stick airborne steering stayed below `run_speed`. OG
normalizes `RelativeMoveInput` in air, targets full `MaxSpeed`, and applies
`AirFrictionMult=0.1` when input is idle; `AnalogMagnitude` only scales the
grounded target. The controller now has those separate ground/air rules, with
coyote jump priority and directional wall-jump launch preserved by keeping
wall-jump after horizontal movement and skipping same-tick gravity/accel.
`locomotion_smoke` covers idle-air friction, half-stick air target, unchanged
ground analog speed, and the existing wall-jump launch contract. Full host
suite passes 71/71; all three ROM variants above rebuild successfully.

## Cross-cutting verification

- After Inc 2: Ares 30 s capture, walk-with-AutoWalk; confirm the OLD bug
  classes are still fixed (no `[climb] START` with z=0 l=0 r=0; no
  climb-pump spam) — the mover swap must not regress parity-3's wins.
- After Inc 3: corner-walk capture (worst cell n01_n02); after Inc 5: the
  final 120 s capture gate listed in Inc 5, archived under `build/` per
  `n64-opt-device-verify-and-telemetry` learning (cite captures, not docs).
- `git bisect` viability: one commit per increment; each increment's grep
  criteria (`grep -rn player_motor src/` empty after Inc 2, etc.) make
  bisection meaningful.
- Host suite green at EVERY increment boundary (`run_host_tests.sh`); ROM
  builds at Inc 2, 3, 5 minimum.

## Standards / common-mistakes referenced

- `.agents/learnings/climb-probe-shell-geometry-coupling.md` — probe-radius ↔
  collision-radius coupling; infinite-plane geometry bounding;
  room==nullptr ray guards; entry-visibility radius. All adopted in the
  query/MoveService design and called out per-increment.
- `.agents/learnings/stale-contract-vs-working-tree.md` — fixtures encoding
  the old parallel-pass semantics are the stale side after Inc 2; migrate with
  justification, don't back-port old gates.
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` —
  kDebugAutoWalk + capture_baseline.sh is the device walk/runbook; cite
  captures.
- `.agents/common-mistakes/plane-query-geometry-bound.md` + repo memory
  (frozen climb) — plane queries must be vertex-bounded; Inc 1's
  `QueryWallPushout` inherits the bounding rule.
- `docs/controller_spec.md` §60-61 (collision queries, movement collision
  algorithm), §67-68 (module separation — player_movement/player_collision/
  player_input split is the naming source for the new TU), §64 (debug mode)
  — the N64 target contract this plan implements.

## Open questions (CONSIDER from review)
- Mid-plan device attribution for Normal-state regressions is thinner than
  climb's (Inc 2 adds the `[move]` frame line, which is the mitigation);
  if Inc 2's 30 s capture shows ambiguous positioning, consider pulling the
  every-30th-frame cadence to every-frame for that capture only.
- The `Inc 2` boundary semantics for `StClimbing` (verbatim transplant, movement
  routed through SweepMove) are deliberately intermediate; if the
  transplant proves larger than one PR, the fallback split is: Inc 2a inverts
  Normal+Dash, Inc 2b transplants Climb — keep the DAG honest if so.
- `WallHit::pushout` field and the shared `kRayEpsilon` value: confirm both
  during Inc 1 implementation (WallHit already carries pushout for sphere
  queries; rays historically return t — normalize in `QueryFloorRay`'s
  wrapper, one line each).
- Skid dust/sfx hooks are unported (OG has dust + sfx_skid); visual feedback
  gaps may read as "skid didn't trigger" on device — host asserts are the
  arbiter, note for the art milestone.

## Out of scope

- Camera behavior (zoom/orbit deviations stay as documented).
- Feather, Bubble, Cassette, StrawbGet/Reveal, Cutscene states (OG has them;
  demake doesn't need them for this milestone).
- Moving-platform riding extensions beyond existing demake carry (OG
  IRidePlatforms is richer; revisit with a moving-platforms milestone).
- Art/animation hooks (squash/squish values recorded but not wired).
- Input remapping/Ares config (user-side runbook stands).
- World-scale retuning (10× demake scale conversions stay).
- Respawn/transition overhaul (whole-scene OG reload is a different
  architecture than the demake's respawn_system; keep).
- Spikes/DeathBlocks (no spike actors exist; death plane + respawn unchanged).

### Integrated air-control route gate (2026-09-25)

The isolated map autoplay's post-jump idle window did not test directional
air steering or release braking end-to-end. Replace ticks 170–179 with six
camera-right airborne steering ticks followed by four no-input ticks; retain
the dash press at tick 180 and existing no-teleport guided climb route. Record
camera-right velocity delta, release horizontal speed drop, and any grounded
tick. Gate pass on delta ≥8, drop ≥4, and remaining airborne for the full
window. `locomotion_smoke` now replays the same 6+4 sequence from a captured
jump velocity. Validation: host suite 74/74 (including registered
`slope_smoke`); isolated
`make physics-autoplay-rom` succeeds (SHA-256
`dbf72d53a396d9e2cb8d249946b965ba4757e9bc3109b64042e8cd296f2e18a6`). At the
time of this entry the Ares gate was pending; the post-fix device replay is
recorded below.

### Slope-speed regression registration (2026-09-25)

Review found `tests/feel_spec/slope_smoke.cpp` asserted flat, uphill, downhill,
and near-vertical movement scaling but was absent from `tests/run_host_tests.sh`.
Register it beside `locomotion_smoke` so future controller changes execute this
gate on every host-suite run. Manual slope feel comparison is now item 10 in
`docs/physics_feel_acceptance.md`.

### Grounded overspeed carry and steering parity (2026-09-25)

OG `Player.cs` saves slope-adjusted `trueMax` before analog scaling, selects
`PastMaxDeccel` for aligned overspeed carry, rotates above-max `targetFacing`
at `RotateSpeedAboveMax`, and approaches scalar speed along that facing. The
demake had used slow deceleration only in air, never used the above-max turn
rate, and overwrote the rotated target facing with raw stick input. It also
computed slope bias from current/ledge-adjusted input rather than the
pre-turn facing basis. Restored OG ordering and equations.

Regressions fail before fix: grounded aligned speed 100 must become 99 after
one 60 Hz tick; above-max diagonal steering must turn about
`RotateSpeedAboveMax / 60` radians without snapping to input. Real-map guided
climb now targets the selected wall plane, ignores unrelated contact flicker,
and waits for tangent velocity to settle before its 30-tick wall-rest sample.
Route passes: rest drift 0.000, grounded climb entry, 16 climb ticks, mantle,
zero shell/model overlap. Host suite: 74/74.

The boot log supplied for the missing-map issue exposed a separate asset
contract bug: generated `__TB_empty` had no texture path but inherited
`bilinear`, so `MaterialCatalog` rejected the entire MATMETA and flat fallback
then read compressed LVL bytes through raw `fopen`. Sentinel metadata now uses
`filter=none`; textured renderer remains available with a null catalog for
flat-color fallback; MapRuntime no longer constructs an unused legacy room
renderer. Fresh Ares boot loads 32 material slots, 13 residents, and reports
56 near batches / 9 texture uploads, with no `bad magic` or catalog failure.

Normal ROM SHA-256: `156ca7b9d286c33339652a9bfc6928acb4b7ebbf182ebb0c15b69bcefb188fb3`.
Separate autoplay ROM SHA-256:
`dbfa9ecf10e08c8c43328b09cfb93cb526d2b7b35fe89827e4b9150ffa9e2064`.
Fresh Ares `[phys-test] result=PASS`: run peak 64, brake to 0, reverse 64,
jump rise 24, dash distance 37.9, air-steer delta 26.1, air-release drop 5.3;
grounded wall entry at gap 3.35, first climb step +0.67 with 0.100 s snap
cooldown, 16 climb ticks, mantle, and zero model-clearance violations.
Existing interactive Ares PID 387484 remains untouched. Free-form movement
feel, slope feel, and camera-angle shimmer still need manual inspection after
reloading the normal ROM.

### Climb wall-jump input parity (2026-09-25)

Source/test audit found a demake-only neutral split: climb wall-jump set
`(horizontal=40, vertical=105)` with no stick input, but OG `WallJump()` always
sets outward `WallJumpXYSpeed=83.2` and `JumpSpeed=90`. The split had no tuning
evidence and made the same action launch differently based on unrelated climb
steering. `climb_mantle_smoke` now asserts the same OG launch for neutral and
up-input jumps; its pre-fix run failed at `(0,105,40)`. Host suite passes
74/74.

The isolated Ares route now attempts wall-jump after 10 sustained climb ticks,
before its selected short wall can mantle. Fresh Ares capture
`build/physics-autoplay-walljump-vector-gate-20260925.log` reports
`result=PASS`, grounded entry, 10 climb ticks, first-step rise 0.67,
`walljump_ok=1`, XY speed 83.2, vertical speed 90, outward speed 83.2, and
zero model-clearance violations across 10 samples. Normal ROM SHA-256:
`df143c56d1398da4a2d0d0d77beb2f5f4ff51e6974378462f961ed41de572266`;
autoplay ROM SHA-256:
`5154415ca871712725c8ea91fc8b6f24a76c3cc9881f3d31d1928b438e31aa0b`.
Manual controller checklist remains open; existing Ares PID 387484 still has
the prior ROM loaded.

### Moving-surface carry + late-coyote integration follow-up (2026-09-25)

Closed the gameplay writer gap for the existing `Room::moving_surfaces` path:
post-move `GroundHit.owner_id` records rider ownership, then the next fixed
tick's pre-input phase applies platform displacement through `SweepMove` and
stores the platform velocity for OG's 0.10 s launch window. This does not add
the deferred actor-trait/solid ownership architecture. `surface_parity_smoke`
guards rider ownership, swept displacement, and one-shot jump carry.

Added and registered `coyote_edge_smoke`: run off a finite floor, wait until
late in the 0.12 s coyote window, then assert height restoration at the
state-phase boundary and upward movement after `MovePhase`. Full suite:
76/76. Fresh normal ROM builds; separate current autoplay/input-trace ROMs are
available for device replay. No Ares capture was taken because interactive PID
387484 is still open. Manual controller/A-B feel gate remains open.

### Real-map slope-speed replay (2026-09-25)

The prior `slope_smoke` injected synthetic normals and could pass a blocked
real-map track because it only checked that the player stayed grounded. Added
`real_map_slope_smoke`: scan CMSH for stable two-way ramp corridors, replay 12
ordinary fixed controller ticks in each direction, compare against flat speed,
and require nonzero movement plus uphill/downhill speed ordering. First stable
candidate had zero uphill speed due adjacent geometry and is now rejected; the
second candidate passes on face 17 with downhill `73.06`, flat `64.00`, uphill
`54.94` (normal `(0, 0.929, 0.369)`). Host suite prints selected route metrics
directly. This strengthens slope evidence but does not close subjective device
feel or the still-pending manual checklist.

### ROM build-marker freshness (2026-09-25)

The user-provided Ares log's 03:01 marker was ambiguous: `__DATE__/__TIME__`
was compiled into `rom_main.o`, while Make later relinked gameplay objects
without rebuilding that TU. Added a phony marker prerequisite for
`$(BUILD_DIR)/src/user/rom_main.o`, including isolated autoplay/input-trace
builds. Dry-run now schedules that compile; normal build refreshed the object
at 08:48 and emitted ROM SHA-256
`e6b561c79589d61849c2b8eaa50d19a56cda415c8cf34a4af25457401f9083da`.
The captured `[map_runtime] LvlRoomRenderer::Load FAILED` line is absent from
current source/binary; reload current ROM before treating old Ares output as
current behavior. `rom_main.o` now rebuilds once per ROM invocation by design.

### Physics capture qualification gate (2026-09-25)

Added `tools/verify_physics_capture.py` and one-command
`tools/capture_physics_autoplay.sh`. A capture now qualifies only when its
SHA-256 matches the just-built ROM, boot/map/collision/material/textured-room
telemetry succeeds, near-render counters are nonzero, every raw Z/L/R/analog
sample is released, and detailed run/air/jump/dash/approach/climb/walljump/
clearance metrics pass. `capture_baseline.sh` preserves those startup tags.
The regression fixture proves the verifier rejects a `[phys-test] PASS` log
that also has malformed MATMETA, raw-LVL magic failure, and held Z, matching
the contaminated older capture pattern. Full runtime recapture remains
pending because interactive Ares PID 387484 is still active; never interpret
that process as the isolated current autoplay build.

The archived `build/baseline-current-autoplay-20260925-051503.txt` was run
through the new verifier and correctly rejected: map/collision/catalog/room
startup telemetry was absent, counters stayed `near_batches=0 resident=0`, and
its old physics result did not include current air-control and wall-jump gates.
Its earlier `result=PASS` remains narrow historical route evidence, not a
qualified whole-runtime physics capture.

### OG side-facing climb-entry parity (2026-09-25)

The 0.5 facing-dot gate was a demake-only rejection: `QueryClimbWall` already
uses facing as candidate preference, as OG does, but `StNormalUpdate` then
rejected valid wall hits when the player faced sideways. The gate had been
retained to address phantom grabs; raw Ares telemetry instead showed `z=1` at
rest, proving that report came from controller mapping. A new
`climb_mantle_smoke` case fails before the change (`valid side-facing climb
press was rejected`) and passes after. Entry now queries first, then checks
directional movement against the returned face normal; neutral input still
requires the climb shoulder, and movement away still rejects. This restores
OG candidate-ranking behavior without weakening input validation. Manual
controller feel remains unverified until current ROM is loaded in Ares.

### Independent wall-jump and mantle device profiles (2026-09-25)

Audit found the wall-jump route could emit `result=PASS` with `mantle=0`.
Added a separately built mantle profile; verifier now checks profile identity
and requires the selected exit event, preventing one climb exit from masking
the other. Both profiles still perform the same run/brake/reverse, jump/dash,
air-control, no-teleport real-wall approach, grounded climb entry, first-step,
and rendered-model clearance gates.

Fresh Ares captures:

- Wall-jump: `build/baseline-walljump-guided-20260925-102621.txt`, SHA-256
  `d9073254938d39a0e01681ab3d19cee43c5d738d0417582051db02437f832e09`;
  PASS at 10 climb ticks, `(83.2,90)` launch, 10 clear samples.
- Mantle: `build/baseline-mantle-guided-20260925-102327.txt`, SHA-256
  `8cfbc0c31e02f39f78d828f64dcffc9490435b232fc2b29cba6e877c41864007`;
  PASS at 16 climb ticks, mantle event, 16 clear samples.

Both captures passed map/collision/material/texture/runtime health and raw
shoulder gates; host suite passes 79/79. This closes the scripted mantle gap,
not live-controller feel or visual acceptance.

### Fresh current-build qualification (2026-09-25)

The prior pending-capture note naming Ares PID 387484 is historical. Current
normal ROM SHA-256 `5588babc1194eccd979105fc693d91b1a0288ed8b074eb79a3c158d1c6149b66`
boots 45 map cells, 10,618 global collision triangles, 32 materials and
textured rooms; stationary counters are 56 near batches / 13 resident cells.
Current isolated wall-jump (`8009168981c56b64b6644582f2c9e4b4e8d4921eb17b0df38c7e5570183dd7a1`)
and mantle (`c603a31f2afc71efd2eaa23f5ddf7958f99aa15135e329ee0ea88e6c31b561cb`)
captures both pass actual real-map approach, grounded climb, movement/air
gates, and zero rendered-model clearance violations. Separate profile gates
require their respective wall-jump or mantle event.

Interactive Ares now runs `physics_input_trace.z64` SHA-256
`c125799c4a2f9e1f7ddcacf452ca24418c7b223865925ecc2356211853781605` (PID
674964). Current session remains grounded/neutral in observed trace; no manual
movement or corner traversal has been recorded yet. Host suite 79/79. Thus
Inc 5's machine-verifiable route/collision gates are current, but its 120 s
walk and manual control/corner acceptance remain open.

### No-teleport corner device gate (2026-09-25)

Added `corner` profile to the isolated physics autoplay. Unlike host-only
corner fixtures that initialized beside the wall, the route starts from map
`Start`, walks to the z=38.4 face with normal movement, grabs at the live
`ClimbCheckDist`, and holds side input through the real corner ease. It does
not write `PlayerState::position`. Terminal gate requires exactly one corner
adoption, arrival on target normal, 10 stable grip ticks, ≤6-unit max corner
step, and no rendered-model clearance violations.

Host `real_map_corner_traverse_smoke`: 50 approach ticks, 51.20→3.57 gap,
49.24-unit displacement, one corner, zero exits, max step 1.914, 0.296 waist
and head pushout, 0.500 model gap. Ares capture
`build/baseline-physics-corner-20260925-175629.txt`, SHA-256
`725086e63bd938eef3fe3061a629be1757abd33ecd5d890609ed194d43756547`: PASS,
49 approach ticks, 73 climb ticks, one corner, target arrived, 10 stable
ticks, 86 clearance samples, zero violations, neutral raw shoulders. Verifier
has an independent corner profile and rejects repeated corner events. Host
suite 81/81; normal ROM rebuilt and re-booted in Ares with all map materials,
textured rooms, and neutral `z=0`.

Still open: actual rendered corner-frame inspection, live controller feel,
and 120-second uninterrupted walk. The Ares route qualifies mechanics and
clearance math, not final visual acceptance.
