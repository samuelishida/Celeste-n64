# Climb Physics Parity Pass 3: probe unification + OG corner system

## Context

Three climb complaints remain after `climb-wall-physics`, `climb-and-zoom-fix`,
and `wall-climb-overhaul` (all marked DONE but the wall-climb-overhaul device
gate never got a clean confirming capture):

1. **"She latches on regardless of Z-button state."** The code path analysis
   shows exactly ONE entry site (`player_controller.cpp:440`), gated on
   `input.climb_held` (`:434`), which is shoulders-only
   (`input_system.cpp:74-76`). User has verified post-remap `[input] z=0` at
   grab frames. So the bogus grabs are NOT a code-path bug and NOT (anymore) a
   stuck bit — the remaining suspect is the **grip latency**: the climb
   sustain/release path uses the plane query (gate `|n.y| < 0.35`) while the
   ledge probe uses the sphere query (gate `0.85`), so a face gripping as
   "wall" in one query can simultaneously read as "ledge clear" in the other.
   A near-0.35 slope grip + a sphere-probe flicker can present to the
   player as "grabbing things without intent". The motor ALSO ORs a
   body-center + waist flag (`RunWallFlagFromWaist` player_motor.cpp:186-206)
   so wall_contact is true far more often than OG's single waist check.
2. **"Ledges and wall corners are buggy, she can't transition smoothly."**
   Confirmed structural gap: the demake has NO corner migration. OG
   `StClimbingUpdate` (Player.cs:1449-1630) handles wall-plane changes with
   three explicit mechanisms the demake lacks:
   - **inner corner**: side raycast at the waist → snap Position to the new
     face, adopt its normal (`:1567-1574`)
   - **slope-away snap**: raycast along `-climbingWallNormal` → keeps the
     grip glued to walls that lean away (`:1576-1583`)
   - **outer corner rotation**: forward+side raycast → adopt the new wall,
     and a **0.2 s position/facing eased lerp** (`climbCornerEase`,
     `:1436-1442, 1585-1601, 1617-1630`) so rounding a corner is animated,
     not a teleport.
   The demake's sustain only re-acquires via `QueryWallClosestToFace` at the
   SAME waist point; at a 90° corner that query can return either wall
   (AABB tesselation order), and there is no mechanism to FEEL the new wall
   at the body offset that matters (side raycast). The ledge probe itself
   uses `QueryWallPresent` (sphere, `|n.y| >= 0.85` gate) while grip uses the
   plane query (`< 0.35` gate) — so the same world face is a "wall" for one
   probe and "no wall" for the other, which directly breaks the mantle and
   the "grab at the lip" feel. OG uses ONE query family for both
   (`ClimbCheckAt(Vec3.Zero)` / `ClimbCheckAt(Vec3.UnitZ)`, Player.cs:843-851,
   1602-1605).
3. **Reviewer's verdict after 3 plan cycles:** the demake's climb system is a
   patchwork of sphere + plane + motor-flag ORs; OG is a single coherent
   model. The right move is not another patch, it is **porting OG's actual
   wall-check graph** into the demake's query layer.

## Architectural decisions

- **Decision: adopt OG's exact probe distances and anchor points**
  (user-confirmed). `SolidWaistTestPos = feet + 3` (head = feet + 10, OG Z-up
  3/10 → demake +Y-up feet-based), `WallPushoutDist = 3`, `ClimbCheckDist =
  4`. Probe radius for the GRIP/LEDGE checks is ClimbCheckDist (4), not the
  sphere radius (5). The motor's physical popout keeps full-3D positional
  correction (impact-only velocity removal is already OG-parity).
  Rationale: all demake feel-constants already equal OG; the mismatch is the
  probe shell around them. Alternatives rejected:
  keep-5.0-radius shell (two cycles of tuning it did not converge).
- **Decision: climb state gets its own collision radius** —
  `PlayerMotorConfig::climb_collision_radius = 3.0f` (= WallPushoutDist).
  `RunWallContact` uses it while `movement_state == Climbing` and keeps
  `radius = 5.0` otherwise. Rationale (deterministic geometry, not taste):
  with the body sphere at 5.0, an entry anchor 3 units from the face means
  2 units of penetration every tick → `RunWallContact` pops the center out
  to 5 → the waist-to-face distance becomes 5 > `climb_check_dist` (4) →
  the grip probe misses → grab-instant-release loop, the exact bug this
  plan fixes. With climb radius 3.0, the anchored waist sits exactly at
  pushout equilibrium (popout 0) and 3 < 4 keeps the grip probe live.
  OG parity: OG's climb popout/opt radius IS `WallPushoutDist` (3) — the
  5.0 body sphere is a demake Normal-state convenience, not OG behavior.
  Alternative rejected: shrinking the global motor radius to 3 (changes
  dash/run/seam feel everywhere; far larger blast radius).
- **Decision: ONE query family for climb** — the plane-based
  `QueryWallsFacePlaneMesh` (world.cpp:255-316) with the OG
  `ClimbNormalCheck` gate (`|plane_normal.y| < 0.35`) is the only query used
  by climb entry, sustain grip, ledge probe, corner rays. The sphere
  `QueryWallPresent` is REMOVED from climb paths. `QueryWallNearest`
  (sphere) stays only inside the motor's physical popout
  (`RunWallContact`), which is correct there (real sphere-vs-triangle
  penetration depth). Rationale: OG's `SolidWallCheck` is plane-based for
  grip; the demake's two-query split is the measured seam/corner failure.
- **Decision: full OG corner set** (user-confirmed): inner-corner side
  raycast, slope-away snap raycast, outer-corner forward+side raycast, plus
  the 0.2 s `climbCornerEase` position/facing lerp — ported 1:1 with OG
  (converting Z-up→Y-up). Rationale: "she can't transition smoothly" is
  exactly the missing corner system, not a probe tweak.
- **Decision: ledge hop = OG's `ClimbCheckAt(+1 waist unit)` miss**, not the
  sphere-based "ledge_clear + 2-tick persistence". The ledge probe becomes
  the same `ClimbCheckAt` with the normal's ClimbNormalCheck gate, evaluated
  one waist-unit above the grip probe. Persistence filter: keep a 2-tick
  confirm (prevents single-frame flicker) but base it on the unified query.
  Rationale: the current mantle probe model (a *different query type*) is
  the confirmed cause of lip bugs.
- **Decision: keep the demake's clean state lifetime** (`wall_contact`/
  `wall_normal` cleared + re-derived each tick) and keep the two-way
  controller/motor split, BUT route the controller's grip through the
  unified plane query with OG anchors, and drop the motor's extra
  waist-flag OR (`RunWallFlagFromWaist`) — the climb does its own probe;
  `wall_contact` reverts to body-center sphere semantics (physical contact
  only). Rationale: the OR made `wall_contact` true far more often than
  OG's model (OG never consults wall_contact for climb — it calls
  `TryClimb()` which does its own waist check), inflating phantom-grab
  reports.
- **Decision: mirror OG's climb entry exactly**: `TryClimb()` = waist grip
  query (radius ClimbCheckDist, prefer `-facing`) + input gate (neutral OR
  into-wall stick) + ClimbNormalCheck, PLUS the airborne snap-up
  (`TryClimb` +4Z variant) already present in OG. Entry repositions the
  player to `wall.Point + normal * WallPushoutDist` via a swept move, not
  velocity-zero only. Rationale: today's entry depends on motor-derived
  `state.wall_normal`/`wall_contact` which is a different surface than the
  sustain's — one more cause of grab-then-instant-drop.
- **Decision: corner ease is data on ClimbState, driven inside
  StClimbing-equivalent only** — no global timer, no new state. On corner
  adopt, stash `corner_from/corner_to/facing_from/facing_to`, run the ease
  in-place for 0.2 s, and freeze normal movement while it runs (matches OG
  structure where the ease branch replaces the move branch).
- **Decision: keep `into_wall_input` gate exactly as OG** (`RelativeMoveInput
  == Vec2.Zero || Dot(hit.Normal.XY, RelativeMoveInput) <= -0.5` — already
  demake-correct). The "latches regardless of Z" complaint is treated as a
  consequence of (1)/(3), not an input gate change; the `[input]`/`[climb]`
  telemetry + capture gate in the final increment is the arbiter.

## Assumptions and answers from code

- Decision: `climb_held` = shoulders only; verified single entry site gated
  on it. Source: code @ input_system.cpp:74-84, player_controller.cpp:434,
  440 | user-confirmed z=0 post-remap.
- Decision: OG uses ONE wall-check family for grip, ledge, and corners
  (`SolidWallCheckClosestToNormal` at `SolidWaistTestPos` with
  `ClimbCheckDist=4` and `ClimbNormalCheck` gate on the returned NORMAL,
  not on a sphere probe). Source: code @ Celeste64-og/Source/Actors/Player.cs:843-851,
  World.cs:577-594.
- Decision: OG mantle = `inputTranslated.Y < 0` (up in Z-up) while
  `ClimbCheckAt(+1 unit above waist)` MISSES; the demake's
  `climb_ledge_probe_center_offset = -2.5` + sphere-present probe is a
  different semantic (a band miss, not a point grab check). Source: code @
  Player.cs:1602-1615 vs movement_config.hpp:41-47.
- Decision: OG inner-corner/slope/corner logic lives entirely inside
  `StClimbingUpdate` with explicit `SolidRayCast` calls at the waist +
  `WallPushoutDist` repositioning; the demake has NONE of these today
  (sustain re-acquire only). Source: code @ Player.cs:1567-1630 vs
  player_controller.cpp:205-414.
- Decision: demake collmesh face_id IS the triangle index, supported by
  `ResolveOwner(mesh, room, h.face_id)` and the bake pipeline
  (10618 tris global). Source: code @ world.cpp:312, map_runtime boot log
  `[mappack] v2 loaded ... global=...colmesh (10618 tris)`.
- Decision: raycast support for climb corner logic needs a horizontal
  `RaycastMesh` call with `BackfaceCull::Ignore` (winding trap) + a
  `ClimbNormalCheck` filter on the hit normal — the existing
  `RaycastRoomMesh` wrapper (world.cpp:248-289) already does
  `BackfaceCull::Ignore` + owner resolution; expose a public
  `QueryWallRay` wrapper. Source: code @ world.cpp:248-289.
- Decision: `climb_corner_ease_time = 0.20f` (OG `Time.Delta / 0.20f`
  approach). Source: code @ Player.cs:1596.
- Decision: keep `climb_cooldown = 0.30` only on mantle (OG sets
  `tClimbCooldown = 0.3` on hop; release/wall-jump set none — demake
  already matches). Source: code @ Player.cs:1612 vs
  player_controller.cpp:316 note.
- Decision: motor keeps `sweep_step = 5.0`; OG uses 2.0-unit substeps BUT
  the demake's substep loop already re-runs contacts per substep, and
  shrinking sweep_step doubles collision cost per tick — adopt OG's 2.0
  ONLY for the climb repositioning move (which is short), not globally.
  Source: default config player_motor.hpp:11.
- Assumption: bogus "latch regardless of Z" during NORMAL movement
  (not climb-entry) is explained by `RunWallFlagFromWaist` making
  `wall_contact` true whenever ANY near-vertical face is within radius 5
  in front of the body (including while just RUNNING along a wall) —
  combined with the entry gate `into_wall_input` passing when the stick
  is neutral (OG semantics: neutral = allowed). OG behaves the same way,
  BUT OG's physical pushout radius is 3 (much tighter), so she cannot
  "grab" walls she is not actually pressed against. With OG anchors
  adopted, this phantom-adjacency band shrinks from 5 to 4 and from
  body-center to waist+3.

## Risks accepted

- OG anchor units (waist +3, radius 4, pushout 3) are tighter than the
  current 5.0 shell; grip may feel like it "lost" a wall the old shell
  could reach — mitigation: the popout positions the waist exactly at
  `wall.Point + normal*3` after entry, so the tangency boundary is
  deliberate (OG behavior), and the mantle probe sits above it.
- The motor radius/anchoring conflict (MUST-FIX from review) is resolved by
  construction: climb-state collision radius 3.0 makes the post-entry anchor
  a zero-popout equilibrium; inc 2's fixture asserts this explicitly
  (no sustained penetration after 30 climb ticks).
- Changing wall_contact back to physical-only could re-open climb-entry
  misses at seams where the body sphere misses the face the waist grips —
  mitigation: TryClimb now does its OWN waist query (OG model), so climb
  entry no longer needs the motor's flag at all; the Normal-state walljump
  also queries fresh (OG `WallJumpCheck` parity).
- The facing gate is KEPT at `wall_facing_dot >= 0.5`, a deliberate
  narrowing vs OG (no dot threshold there) — accepted because the live
  user complaint is phantom latching; documented in movement_spec.md
  (Inc 5). Revisit only if device review asks for OG-authentic side-grabs.
- Corner ease must freeze gravity input, or the player sags mid-lerp —
  the ease branch zeroes velocity and sets position directly (OG parity).
- Sphere-independent ceil/floor probes untouched — plan must not change
  grounded feel; all edits stay in wall/climb paths.
- Fixtures currently locked to the 5.0 shell (climb_mantle_smoke geometry
  places the player 5 units out) may need fixture geometry shifts; edges
  documented per-increment.

## Increment DAG

- Inc 1 — Unified climb wall query + OG anchors (M) — depends on: none — unblocks: 2, 3, 4, 5
- Inc 2 — OG climb entry (`TryClimb` parity) + wall_contact de-OR (M) — depends on: 1 — unblocks: 4, 5
- Inc 3 — OG corner system in ClimbState (L) — depends on: 1 — unblocks: 4, 5
- Inc 4 — Ledge/mantle probe unification (M) — depends on: 2, 3 — unblocks: 5 (serialized
  after 3 because both restructure the climb-sustain block and Inc 4's gate
  ordering depends on Inc 3's corner-branch placement)
- Inc 5 — Device validation + telemetry + docs (S) — depends on: 2, 3, 4

## Increments

### Inc 1 — Unified climb wall query + OG anchors (M) — DONE
**Depends on:** none
**Unblocks:** 2, 3, 4, 5
**Done criteria:** a host fixture proves that the SAME `QueryWallClosestToFace`
(renamed semantics: waist probe at feet+3, radius 4, plane-based, ClimbNormalCheck
gate) reports consistent wall/ledge answers for (a) a flat wall, (b) a wall+cap
lip, (c) a 90° corner — i.e. one query family now drives all climb decisions.
(Ledge-probe migration is Inc 4's; Inc 1 only provides the query + anchors.)

#### Files to touch

##### src/user/gameplay/player/movement_config.hpp
- What changes: replace probe constants with OG-anchored ones.
  - Add (feet-anchored, +Y-up demake convention; OG Z-up SolidWaistTestPos =
    Position + 3Z above feet parities to feet+3): `climb_waist_offset = 3.0f`,
    `climb_head_offset = 10.0f` (mantle probe uses waist + 1 unit →
    `climb_ledge_probe_extra = 1.0f`), `climb_check_dist = 4.0f`
    (grip/ledge/corner query radius), `climb_pushout_dist = 3.0f`
    (entry reposition target), `climb_corner_ease_time = 0.20f`.
  - Constant retirement is STAGED so every increment compiles (review pass
    2 MUST-FIX M1 — the entry block `player_controller.cpp:446-452` and the
    ledge probe `:275-279` still consume the old constants until Inc 2 /
    Inc 4 respectively):
    - Inc 1: ADD the new constants; change `climb_grip_probe_center_offset`
      to `+3.0f` semantics ONLY after the grip probe (sustain + entry
      inline query) is flipped to `ClimbCheckAt` in this increment — the
      sustain + entry probes are the only consumers migrated here, so
      `climb_grip_probe_center_offset` is DELETED in Inc 1 (both its
      consumers move in this increment).
    - `wall_probe_radius` and `climb_ledge_probe_center_offset` stay as
      deprecated aliases (marked with `// DEPRECATED — removed in Inc 4`)
      because `climb_ledge_probe_center_offset` is still consumed by the
      ledge probe (Inc 4) and `wall_probe_radius` by the same ledge probe +
      the inner re-acquire fallback — Inc 4 deletes both in the same commit
      as its probe migration.
  - Net effect per increment: Inc 1 compiles (grip consumers migrated,
    ledge aliases retained); Inc 2 compiles (no constant use change); Inc 4
    compiles (last two consumers migrate + aliases deleted together).
- Single source of truth for the half-height: do NOT duplicate `half_height`
  into MovementConfig. Add a `float half_height = 10.0f` parameter evaluated
  ONCE — move it OUT of PlayerMotorConfig into a shared spot only if the
  implementer finds a second consumer; otherwise the controller receives it
  via the constructor payload it already gets (MovementConfig) — preferred:
  controller owns `player_half_height` (its own copy, documented as equal to
  `PlayerMotorConfig::half_height`, with a static_assert in gameplay_scene
  init that both are equal). Fixtures that override one of the two must
  now override both (called out in fixture edits below).

##### src/user/gameplay/player/player_motor.hpp
- What changes (Inc 2 consistency): ADD
  `float climb_collision_radius = 3.0f;` now (field only); the call-site
  use + `RunWallFlagFromWaist` deletion + `wall_probe_center_offset`
  deletion happen together in Inc 2 so every increment leaves the tree
  compiling (DAG review finding #1).

##### src/user/gameplay/world/world.hpp / world.cpp
- What changes: expose the plane-query trio for player use with OG gates:
  - `bool QueryClimbWall(const Room&, const Vec3& point, float radius, const Vec3& prefer_dir, WallHit* out)` — wrapper over `QueryWallClosestToFace` that ALSO enforces `|plane_normal.y| < 0.35` (ClimbNormalCheck) on the returned face (the inner query already gates; the wrapper documents/enforces the contract for callees).
  - `bool QueryClimbWallPresent(const Room&, const Vec3& point, float radius, const Vec3& prefer_dir)` — any-hit variant (OG `ClimbCheckAt` bool return).
  - `bool QueryWallRay(const Room&, const Vec3& origin, const Vec3& dir, float max_dist, WallHit* out)` — horizontal raycast wrapper over `RaycastMesh` with `BackfaceCull::Ignore`, keeping only hits whose normal passes the ClimbNormalCheck gate; used by corner logic (Inc 3).
- Function(s): three wrappers; reuse `WallHit`.
- Data shapes: `WallHit` unchanged (has `plane_normal`, `point`, `face_id`, `pushout`).
- Integration points: player_controller (climb entry/sustain/ledge/corners), and NO motor changes beyond the new Inc-2 field.
- Error paths: room/coll_mesh null → miss; ray hitting a slope (`|n.y| >= 0.35`) → treated as no-hit by callers (OG semantics).

##### src/user/gameplay/player/player_controller.cpp
- What changes: all climb probes move to the new anchors/radii: waist =
  `position.y - player_half_height + 3` (position is body CENTER; feet =
  center.y - half_height — review finding #2 anchor note). Grip query =
  `QueryClimbWall(waist, climb_check_dist, -facing)`. Sustain re-acquire
  uses the same call. (The ledge probe switch stays in Inc 4 — Inc 1 only
  updates the gripped probe to the new anchor/radius.)
- Function(s): private helper `ReacquireGrip(state, room, delta) -> bool`;
  private helper `ClimbCheckAt(state, room, y_extra) -> WallHit` mirroring OG.
- Integration points: sustain block (lines ~205-414), entry block
  (~416-457).
- Error paths: no room (unit fixtures) → behave as today (fall back to flags).

#### Edge cases
- Fixtures use a 20-unit-tall wall; waist at center-7 (feet+3) with radius 4
  catches it the same as before (feet on floor). Fixtures that set
  `cfg.half_height` non-default (e.g. climb_mantle_smoke's 5.0) must now
  also set the controller's `player_half_height` — call it out in the PR.
- Coplanar double-face (bake seams): plane query returns identical normals
  from both faces — selection by max Dot(prefer) is stable.
- Ray origin INSIDE geometry (waist 3 above feet while standing): forward
  rays must ignore backfaces AND start-penetration — `RaycastMesh` with
  t>epsilon; document the epsilon choice (reuse `kRayEpsilon`).

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: `wall_plane_query_smoke` — extend with the lip +
  corner meshes asserting the unified query agrees with itself for grip vs
  ledge (same face → same answer); `climb_mantle_smoke` — update probe
  geometry notes; assert the mantle still triggers at the real lip with
  radius 4.
- Done: suite green with the two named fixtures updated; grep shows
  `climb_grip_probe_center_offset` gone from ALL code, and
  `wall_probe_radius` / `climb_ledge_probe_center_offset` appear ONLY in
  `movement_config.hpp` (as the deprecated Inc-4 aliases) + the ledge-probe
  consumer lines.

### Inc 2 — OG climb entry (`TryClimb` parity) + wall_contact de-OR (M) — DONE
**Depends on:** 1
**Unblocks:** 4, 5
**Done criteria:** climb entry behaves as OG's TryClimb: own waist-plane query
(radius 4, prefer -facing), neutral-or-into-wall input gate, ClimbNormalCheck,
entry REPOSITIONS the player CENTER to
`hit.point + Vec3{0, half_height - climb_waist_offset, 0} + normal * 3`
(center-anchored form: waist lands at wall.Point + normal*3, feet follow —
review finding #2); airborne rising +4-unit snap-up variant works; motor
`wall_contact` is physical-only (waist OR removed); climb-state collision
radius 3.0 makes the anchor a zero-popout equilibrium (asserted by fixture:
no sustained penetration over 30 climb ticks); walljump (Normal) uses a
fresh waist query with the same room==nullptr shim as entry.
Host fixtures: no-entry-when-facing-away 90° fixture passes; suite green.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp (entry block ~416-457)
- What changes: replace `state.wall_contact && entry_normal_valid && facing_wall`
  with `QueryClimbWall(...) || (airborne rising && QueryClimbWall(waist+4,...))`
  (OG `TryClimb` + the +4 UnitZ snap-up variant). On success (OG parity):
  `climb.wall_normal = hit.plane_normal; climb.grip_face_id = hit.face_id;`
  then reposition the CENTER directly (single-frame snap; the motor's next
  `RunWallContact` at climb radius 3.0 finds ZERO penetration — see the
  climb_collision_radius decision):
  `position = hit.point + {0, player_half_height - climb_waist_offset, 0}
  + normal * climb_pushout_dist`
  (waist anchored at the wall surface + 3 out; feet follow the center).
  Velocity zeroed. `target_facing = -normal.XZ` (OG sets facing INTO the
  wall).
- **Facing gate — SINGLE DECISION (review pass 2 MUST-FIX M2): KEEP the
  `wall_facing_dot >= 0.5` entry precondition.** Fact check that forced the
  explicit choice: OG's `ClimbCheckAt` has NO dot threshold — the prefer
  vector only RANKS candidate faces (a lone wall behind her still returns a
  hit, `World.cs:577-594`), and the demake's `QueryWallClosestToFace`
  likewise never gates (`world.cpp:658-668`) — so "prefer-dir encodes the
  facing gate" is FALSE. Keeping the 0.5 gate is a deliberate deviation
  from OG (narrower grab set: no side/behind grabs while facing away),
  chosen because the user's live complaint is phantom latching and OG-exact
  removal would WIDEN the grab set. Recorded as a documented deviation in
  `docs/movement_spec.md` (Inc 5) and listed under "Risks accepted". If
  device review shows OG-authentic side-grabs are wanted, delete the
  `facing_wall` conjunct — one line, parity restored.
- Function(s): `bool TryClimb(PlayerState&, const Room*, float y_extra = 0)`
  mirroring OG; called from the entry site and (later) from Feather-adjacent
  paths if ever added.
- Data shapes: reads `state.last_facing` (XZ), writes
  `climb.wall_normal/grip_face_id`, `position`, `target_facing`.
- Integration points: `StatePhase` climb-entry block; Normal-state
  walljump (below).
- Error paths: `room == nullptr` → fall back to today's flag-based entry
  (documented shim so controller-only fixtures keep working). The
  Normal-state walljump uses THE SAME shim (review SHOULD-FIX #1).

##### src/user/gameplay/player/player_motor.hpp
- What changes: delete `wall_probe_center_offset` (its only consumer dies
  this increment). Add `float climb_collision_radius = 3.0f;` (declared in
  Inc 1, consumed here).

##### src/user/gameplay/player/player_motor.cpp
- What changes: (1) delete `RunWallFlagFromWaist` and its call site
  (:186-206, :316); `wall_contact` returns to body-sphere semantics only.
  (2) `RunWallContact` radius becomes `state.movement_state ==
  PlayerMovementState::Climbing ? config_.climb_collision_radius :
  config_.radius` (deterministic geometry: climb anchor = popout-3
  equilibrium; review MUST-FIX #3).
- Function(s): signature of `RunWallContact` unchanged (radius arg computed
  at call sites).
- Integration points: per-substep + end-of-tick wall passes.
- Error paths: none new.

##### player_controller.cpp (Normal-state walljump ~462-470)
- What changes: replace `state.wall_contact` gate with an immediate
  `TryClimb`-style `QueryClimbWall(waist, climb_check_dist, -facing)` check
  (with the room==nullptr fallback shim); adopt `hit.plane_normal` as jump
  dir; apply `hit.pushout * (climb_pushout_dist / climb_check_dist)`
  reposition exactly like OG WallJumpCheck (Player.cs:900-910).
- Function(s): integrate into the existing walljump block.
- Integration points: Normal-state jump handling.
- Error paths: miss → fall through to normal jump.

#### Edge cases
- Grabbing while rising past a wall top: the +4 waist-unit snap-up variant
  MUST not grab above the lip (the lip probe in Inc 4 handles the mantle);
  OG behavior: only grabs when `Velocity.y > 0 && !grounded` — replicate.
- Grabbing a wall beside her while facing away: BLOCKED by the explicit
  `wall_facing_dot >= 0.5` gate — a deliberate narrowing vs OG (M2
  decision above; OG would allow this grab via prefer-rank alone).
- Entry reposition pushes the player INTO a corner: cap the snap so the
  final center position never exceeds 3 units of net movement per axis
  (OG moves via SweepTestMove; our single-snap respects the same bound).
- Tunnel guard for the single-snap (review pass 2 C2): after applying the
  entry reposition, run one cheap sanity re-probe —
  `QueryWallsFacePlane(center, climb_collision_radius)` — and if it reports
  NO face, walk the snap back 50% toward the pre-snap center (one
  iteration). This replicates the anti-tunnel safety OG gets for free from
  `SweepTestMove` without a full substep move in the controller.
- Zero-popout equilibrium: climb radius 3.0 + anchored at normal*3 →
  `RunWallContact` computes pushout ≈ 0 sustained; the fixture asserts
  position drift < 0.5 over 30 climb ticks (review MUST-FIX #3 gate).

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: `climb_parity_smoke` — entry reposition assertion
  (waist ends ≈ wall.Point + normal*3, i.e. center at
  `wall.Point + {0, half_height - waist_offset, 0} + normal*3`) + 30-tick
  climb equilibrium assert (no popout oscillation). For the walljump: the
  EXISTING `gameplay_smoke` walljump section covers climb-state jump-out
  only; add a Normal-state directional walljump section there (review
  pass 2 S3 — set `wall_contact=true` + `room=nullptr` and assert the
  fresh waist-query shim + SET-away velocity), and update
  `climb_parity_smoke:30`'s walljump case to the new query if it asserts
  the old flag path.
- Done: all green; grep shows `RunWallFlagFromWaist` gone.

### Inc 3 — OG corner system in ClimbState (L) — DONE
**Depends on:** 1
**Unblocks:** 4, 5
**Done criteria:** host corner fixtures prove: (a) climbing into an inner
90° corner re-anchors to the new wall within 2 ticks with the side raycast,
(b) climbing up over a convex outer corner rotates around it via the
forward+side raycast with an eased 0.2 s position/facing lerp (no teleport
pop), (c) grip on a wall that slopes away is retained via the snap raycast.
Zero uncommanded climb exits across all three fixtures.

#### Files to touch

##### src/user/gameplay/player/player_state.hpp (ClimbState)
- What changes: add corner-ease fields mirroring OG
  (`climbCornerFrom/To/FacingFrom/FacingTo` + `climbCornerEase`):
  `Vec3 corner_from{}; Vec3 corner_to{}; Vec3 corner_facing_from{};
  Vec3 corner_facing_to{}; Vec3 corner_travel_origin{}; float corner_ease =
  0.0f;` (0 = inactive).
- Integration points: read/written only inside the Climb state block.

##### src/user/gameplay/player/player_controller.cpp (Climb sustain)
- What changes: port OG's three corner mechanisms (Player.cs:1567-1630,
  Z-up→Y-up conversion: OG `.XY()` = demake XZ, OG UnitZ = demake +Y):
  1. **inner corner**: `move_x != 0` and `QueryWallRay(waist, side_dir_x,
     climb_check_dist)` hits → set CENTER `position = hit.point +
     {0, player_half_height - climb_waist_offset, 0} + hit.normal *
     climb_pushout_dist` (same center-anchor as entry; review finding #2),
     adopt `hit.plane_normal` as the new `climb.wall_normal` +
     `grip_face_id`, `target_facing = -normal.XZ`.
  2. **slope-away snap**: skip when the grip probe ALREADY reports the same
     `face_id` this tick (CONSIDER adopted — saves a raycast and avoids
     coplanar seam-face normal flapping); else if
     `QueryWallRay(waist, -climb_normal, climb_check_dist + 2)` hits with
     ClimbNormalCheck → same center-anchored adoption.
  3. **outer corner rotation**: else if `move_x != 0` and
     `QueryWallRay(waist + forward*(climb_check_dist+1) + side*move_x,
     -side*move_x, climb_check_dist*2)` hits → same adoption, plus start the
     corner ease: `corner_from = position at the START of this movement
     tick` (the pre-move position, stashed before the tick's movement runs
     — review pass 2 C1: OG stashes `climbCornerFrom` after the move section
     of the PREVIOUS update; define it as "position before this tick's
     corner-adopting change"), `corner_to = new position`,
     `corner_facing_from/to`, `corner_ease = 1.0f`.
  4. **ease integration**: at the TOP of the Climb block, when
     `corner_ease > 0`: zero velocity, `position = Lerp(from, to, 1-ease)`,
     `target_facing = angle-lerp(facing_from, facing_to, 1-ease)`,
     `corner_ease = Approach(0, dt / 0.20)`; SKIP normal wall movement this
     tick (OG structure). While easing, grip is NOT re-probed (the corner
     raycast just adopted the new wall).
  5. **force corner** trigger (review SHOULD-FIX #3 — per-tick displacement
     is ≤0.67 units at climb_speed 40/60 Hz, so a per-tick > 2 test never
     fires): measure displacement against the STASHED origin, exactly like
     OG — on every corner-adopt or ease-end, stash
     `climb.corner_travel_origin = position`; after each movement tick, if
     `(position - corner_travel_origin).Length() > 2`, stash from/to and
     start the ease (this also matches OG's
     `(Position - climbCornerFrom).Length() > 2`, which accumulates across
     ticks since `climbCornerFrom` only resets when a corner eases).
- Function(s): helpers `PortCornerMechanics(...)` split into
  `InnerCornerSideRay`, `SlopeSnapRay`, `OuterCornerRay` (all static, take
  state+room+inputs).
- Data shapes: `WallHit` reuse; corner state in ClimbState (above).
- Integration points: Climb sustain block, before the wall-plane movement
  section (movement section runs only when `corner_ease <= 0`).
- Error paths: ray misses (open wall) → all three fall through to normal
  movement — no behavior change on plain walls.

#### Edge cases
- Corner at a T junction (three walls): rays hit the side wall first (nearest
  t wins — `RaycastMesh` returns nearest; assert in fixture).
- Corner ease interrupted by button release: release check runs BEFORE the
  ease branch (OG checks Climb.Down first, :1451) — release always wins.
- Mantle during corner ease: deferred until ease finishes (OG performs the
  ease INSTEAD of movement, and the ledge check happens only in the movement
  path; port that ordering exactly).
- Facing flip across the corner: `targetFacing = -normal.XZ` both sides;
  the eased facing lerp uses angle interpolation (shortest arc) like OG's
  `Calc.AngleLerp`.

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: new `tests/climb_corner_smoke.cpp` — builds a
  floor + two perpendicular walls + an outer corner (wall with a 90°
  convex edge); drives: up-climb into inner corner (assert new
  `wall_normal` = corner wall's normal within 5 ticks, keep climbing),
  sideways into outer corner (assert `corner_ease` engages, position is
  continuous — no >6-unit jump per tick — and facing ends pointing into
  the new wall), slope-away wall fixture (assert grip retained over 60
  ticks). Wire into `tests/run_host_tests.sh` GAMEPLAY_LINK block.
- Done: all green, zero uncommanded exits in the three fixtures.

### Inc 4 — Ledge/mantle probe unification (M) — DONE
**Depends on:** 2, 3 (serialized after 3 — both touch the climb-sustain
block's pre-movement region; the mantle gate must slot around Inc 3's
corner-branch ordering, per review pass 2 S1)
**Unblocks:** 5
**Done criteria:** mantle fires OG's way: holding up while
`ClimbCheckAt(waist + 1)` MISSES → hop (up 80, forward 40 into the lip,
no-move 0.25, cooldown 0.3) — with the unified plane query and the
2-tick flicker confirm retained. The sphere `QueryWallPresent` is no longer
referenced by player code. Edge-grab at the lip works with the tighter
radius-4 probe.

#### Files to touch

##### src/user/gameplay/player/player_controller.cpp (mantle block ~246-305)
- What changes: replace `QueryWallPresent(sphere)` ledge probe with
  `QueryClimbWall(waist + climb_ledge_probe_extra, climb_check_dist,
  -facing)` miss-check (OG `ClimbCheckAt(Vec3.UnitZ)`). Keep the
  `ledge_miss_ticks >= 2` persistence (flicker guard) but ALSO reset it
  whenever the grip probe reports present (probe-agreement sanity).
  Mantle velocity exactly OG: `velocity = (target_facing * 40, 80)`,
  `tNoMove = 0.25`, `tClimbCooldown = 0.3` (already the values used).
- Function(s): fold into `ReacquireGrip`/`ClimbCheckAt` helpers from Inc 1/2.
- Data shapes: unchanged.
- Integration points: mantle gate; grip-sustain block.
- Error paths: probe miss while NOT holding up → nothing (today's semantics).

##### src/user/gameplay/world/world.hpp
- What changes: deprecate `QueryWallPresent` for gameplay use (mark with a
  comment; remove the header decl ONLY ifMotor/other callers stay clean —
  grep first; the sphere variant stays for debug tooling).

#### Edge cases
- The 2-tick confirm vs OG's instant hop: decide UP FRONT (review CONSIDER
  #4 — no fixture-conditional forking): keep the 2-tick guard as a
  documented deliberate deviation (flicker immunity won in
  wall-climb-overhaul; OG's "instant" hop already tolerates ~1 frame of
  probe noise because it re-grabs via TryClimb). Record it in
  movement_spec.md as a deviation. If device shows a delayed mantle
  (>50 ms lip-lag), revisit with a capture rather than silently flipping.
- Downward climb into the wall base: unchanged (cancel-down-while-grounded
  stays today's behavior, matches OG `move.Z < 0 && GroundCheck`).

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: `climb_mantle_smoke` — assert mantle hop triggers
  with the radius-4 probe; assert NO mantle when the lip probe still hits
  mid-wall (climbing up a tall wall must NOT mantle). New assertion:
  grip and ledge probes use the same face identity (log/inspect
  `grip_face_id` equality in the fixture path).
- Done: green; `grep -rn QueryWallPresent src/user/gameplay/player` is empty.

### Inc 5 — Device validation + telemetry + docs (S) — DONE (telemetry + docs; device capture pending toolchain)
**Depends on:** 2, 3, 4
**Unblocks:** none
**Done criteria:** rebuilt ROM boots on Ares; scripted capture (no-input walk
enabled via `kDebugAutoWalk`) shows ZERO `uncommanded` grab events (no
`[climb] START` with z=0 l=0 r=0 in the same log line) across ≥120 s, corner
traversal produces ≤1 `[climb] START` per intentional grab (no START-spam at a
single position), and a manual play session confirms: grab feels deliberate,
corners round smoothly, lip mantle works. Host suite 100% green.

#### Files to touch

##### src/user/gameplay/scene/gameplay_scene.cpp (climb telemetry ~935-954)
- What changes: extend the ungated `[climb] START` line with
  `face=%d wn_xy=(%.2f,%.2f)` (already has wn) + add an ungated
  `[climb] CORNER` debugf when `corner_ease` starts (fires once per corner
  adopt — validates Inc 3 on device) + add an ungated `[climb] MANTLE
  hop=...` debugf at the mantle hop (review pass 2 S2: no MANTLE debugf
  exists today — only the `RecordClimbMantle()` counter hook at
  gameplay_scene.cpp:953; the line must be ADDED, not assumed).
- Function(s): 3 new debugf sites; counters already exist.
- Integration points: existing RecordClimbStart/RecordClimbMantle hooks stay.
- Error paths: none.

##### docs/movement_spec.md
- What changes: replace the wall-contact section with the OG-parity model:
  probe anchors (waist feet+3/head feet+10), ClimbCheckDist 4,
  WallPushoutDist 3, ClimbNormalCheck 0.35, corner mechanics (inner/slope/
  outer + 0.2s ease), mantle rule — INCLUDING the two documented deviations:
  (a) facing gate `|dot(normal, -facing)| >= 0.5` kept (OG has none),
  (b) 2-tick mantle confirm retained (OG is instant).

##### AGENTS.md
- What changes: status bullet — climb now runs OG's single wall-check family
  (plane-based, waist-anchored radius 4) with the full corner system; the
  two-query sphere/plane split and the motor waist-flag OR are gone. Keep
  the Ares remap runbook (unchanged ownership: input hygiene).

#### Edge cases
- A stuck-Z bit still causes grabs — runbook unchanged; the capture must
  show `z=0` lines to attribute grabs to code, and Inc 5's gate REQUIRES a
  clean-input capture (post-remap).
- Frame-rate headroom: corner logic adds ≤2 raycasts/tick while climbing
  (waist side ray + slope snap) — negligible vs the 0.25 ms update budget
  (telemetry arbiter per learnings/n64-opt-device-verify-and-telemetry).

#### Verification (Ares checklist)
- Pre-condition: `[input] z=0 l=0 r=0 al=0 ar=0` at rest.
- Idle against a wall 10 s: zero `[climb] START` (post-remap; z=0 + no
  shoulder = no grabs — the phantom-latch gate).
- Intentional grab + climb to a corner: ≥1 CORNER line, ≤1 START per
  intentional grab; no START spam at one position (pump gate).
- Climb to a lip holding up: 1 MANTLE, lands on top.
- Wall-slide: full fall speed (`wall_slide_seam_smoke` covers host-side).
- `./tests/run_host_tests.sh` green; `./compile-rom.sh` clean.

## Cross-cutting verification

- One commit per increment; the grep-based done criteria make `git bisect`
  viable across the DAG (review CONSIDER #2).
- Intermediate device smokes (review SHOULD-FIX #5): after Inc 2 and after
  Inc 3, run one quick Ares boot + 30 s capture (`tools/capture_baseline.sh
  madeline_cube_rom.z64 30 inc<N>`) and diff `[climb] START` counts +
  boot health vs the previous capture — do not stack four increments
  before the first device signal.
- After Inc 5: one continuous Ares session walking the checklist above with
  telemetry captured to `.plans/climb-parity-3/capture-final.txt` AND
  archived to `build/baseline-climb-parity3-<ts>.txt` (capture_baseline
  already writes there — cite that artifact in AGENTS.md, since `.plans/`
  may be untracked and the before/after diff must survive; review CONSIDER
  #3). Diff the `[climb] START` count vs the wall-climb-overhaul capture
  (must drop to ≈1 per intentional grab with z=0 evidence). Host suite
  green immediately before close. Manual thumb-on-stick corner walk in
  Forsaken City house cell (cell_n01_n02) — the dense-geometry worst case
  for corner rays.

## Standards / common-mistakes referenced

- `.agents/learnings/stale-contract-vs-working-tree.md` — evidence
  hierarchy: latest device capture > working tree > comments > test
  contracts. If fixtures built around the old 5.0 shell go red after Inc 1,
  the FIXTURE is the stale side (geometry assumptions), not the new
  constants; update fixtures with justification.
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — device
  verification runbook: kDebugAutoWalk + capture_baseline.sh (rom,
  wall_seconds, label); near_batches 56/94 signal; [climb]/[input] lines
  are the arbiter for grab claims; cite the capture, never the doc.
- `.agents/common-mistakes/og-map-polygon-winding.md` — baked faces can be
  reversed; the plane and ray queries already orient toward the query point
  (rays use BackfaceCull::Ignore); the corner fixture must include an
  inverted-winding face to lock the ray path.

## Open questions (CONSIDER from review)

- Camera lerp during corners (OG lerps `cameraTargetForward` with the ease)
  is deferred to a follow-up — flagging here so corner feel review on
  device doesn't misattribute the missing camera swing to Inc 3.
- The 2-tick mantle guard is a deliberate OG deviation (documented in
  movement_spec.md by Inc 4) — if the user prefers OG-exact instant hops,
  delete the `ledge_miss_ticks >= 2` gate and this stays parity-clean.
- The entry facing gate (`|dot| >= 0.5`) is a deliberate narrowing vs OG
  (OG has NO dot threshold; prefer-rank only) — same one-line removal if
  OG-authentic side-grabs are wanted after device review (M2 decision).

## Out of scope

- Input bindings/remapping (Ares stuck-bit runbook remains user-side).
- Feather-state wall interactions (OG TryClimb call inside Popout's Feather
  branch — no Feather in the demake yet).
- Camera behavior during corners (OG lerps cameraTargetForward with the
  ease — defer until the camera_junction work; note as a follow-up nicety).
- Moving/motorized surface climbing (dynamic colliders are excluded from
  the plane query — OG treats Solids generically; revisit with the
  moving-platform milestone).
- Dash state interactions with climb (dash-out path unchanged from
  wall-climb-overhaul).
- Global `sweep_step` retuning for the motor (2.0-unit substeps stayed
  climb-local only).