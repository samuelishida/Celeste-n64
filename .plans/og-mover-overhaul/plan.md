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
- Decision: single climb entry site exists and already matches OG TryClimb
  (climb-parity-3 Inc 2 done); this plan re-wires its *repositioning* through
  the mover but keeps the entry gates (facing-dot ≥ 0.5 kept as the documented
  deviation; neutral-or-into-wall stick; ClimbNormalCheck < 0.35). Source:
  code @ player_controller.cpp:434-457, docs/movement_spec.md deviations list.
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

### Inc 0 — Commit pending fixes (S)
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

### Inc 1 — OG collision queries + MoveService core (M) — depends on: 0
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

### Inc 2 — Controller inversion + motor deletion (M)
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

### Inc 3 — Climb onto the mover + clip regressions (M)
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

### Inc 4 — OG feel: dash buffers/steering + Skid + ledge-avoid + slope/analog (M)
**Depends on:** 2, 3 (serialized after 3 — both edit
player_controller.cpp; ledge-avoid rays interact with climb entry probes)
**Unblocks:** 5
**Done criteria:** reversing at ≥ RotateThreshold triggers Skidding (state
visible, EndSkidSpeed exit at 0.8×max, SkidJump available after 0.1 s);
dash has the full OG steering loop (rotate at DashRotateSpeed + re-
`SetDashSpeed`, continuous steer gate `Dot(move, facing) >= -0.2`) and a
0.10 s dash input buffer (OG VirtualButton device buffer, matching Jump's);
jump buffer at 0.10 s; coyote jump snaps to `coyote_y` takeoff height;
walking into a wall ledge-follows (input rotation) instead of dead-stopping;
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

### Inc 5 — Close-out: telemetry, docs, device gate (S)
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
  4. climb entry facing-dot ≥ 0.5 gate (OG has none);
  5. 2-tick mantle confirm (OG instant);
  6. climb-state collision radius 3 with entry visibility radius 5
     (climb-parity-3 documented deviation, retained);
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
- Run: `tools/capture_baseline.sh madeline_cube_rom.z64 120 og-mover-final`
  → archive to `build/baseline-og-mover-final-<ts>.txt`; diff vs
  `baseline-diag-20260826-150416.txt` for regressions in
  `[profiler]`/`[counters]` — gate: `[profiler]` update ≤0.25 ms (measured
  pre-overhaul budget 0.194 ms + ≤30% headroom; substeps + ≤2 wall
  probes/tick vs today's passes must not cost CPU) and `[memory]` delta
  < 50 KB (no new persistent allocations).
- `./tests/run_host_tests.sh` green immediately before close.
- Done: capture cited in AGENTS.md; suite green; ROM rebuilt.

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