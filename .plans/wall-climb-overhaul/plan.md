# Wall-Climb Overhaul: OG-parity persistent wall contact

## Context

Two user-blocking complaints on device (Ares captures 2026-08-26):

1. **"Madeline grabs every wall without holding a shoulder button."**
2. **"When I hold the grab button and press up, she goes down / slides off."**

Prior analysis blamed a stuck Ares Z bit, then a sphere-probe tangency flicker,
and two rounds of surgical patches were shipped (`climb-and-zoom-fix` plan + a
follow-up that added `climb_grip_probe_radius`/grip-grace/mantle-persistence).
The host suite is green (34/34), but **device captures still show the pump**:
`[climb] START` fires repeatedly at descending Y (52.9→48.4→48.4), i.e. a
grab→release→fall→regrab cycle, and grabs fire on plain contact even without
intent.

Root-cause attribution after reading the OG source and the current demake:

- The demake's wall model is a **sphere-closest-point query per tick**
  (`QueryWallsMesh` → `SphereTriangle`). The normal it returns is the
  **centre-relative direction to the closest point**, which is diagonal at
  seams/edges/corners and flips frame-to-frame. `plane_normal` was bolted on as
  a patch but the **presence** (`QueryWallPresent`) and **grip identity**
  (`QueryWallClosestToNormal`) still lean on the sphere, and `wall_contact`
  (the motor body-center flag) is decoupled from the player's actual waist.
- The demake's wall-gate is too loose: `QueryWallsMesh` only rejects
  `|normal.y| >= 0.85`, so ~30°–45° slopes are returned as "walls". OG
  `ClimbNormalCheck` requires `MathF.Abs(normal.Z) < 0.35` (in the demake's
  +Y-up convention: **`|normal.y| < 0.35`** — only near-vertical faces are
  climbable). With the 0.85 gate, a stuck/in-range Z while brushing a slope
  makes Madeline "grab" surfaces she never intends — a second contributor to
  complaint #1.
- OG (`World.SolidWallCheck`) iterates **solid faces**, ignores planes with
  `|normal.Z| >= 1` (only walls), computes the closest **on-plane** point at the
  player's own Z level via `PlaneTriangleIntersection`, and returns
  `Normal = face.Plane.Normal` — a **stable face-plane normal**. Grip identity
  is `SolidWallCheckClosestToNormal` (pick the hit whose normal is closest to
  `-facing`), and the player **keeps the gripped wall** across ticks
  (`climbingWallNormal`, `climbingWallActor`).

The demake also drives movement/popout through the motor body-center while OG
roads a **waist-level probe** (`SolidWaistTestPos = Position + Z*3`) through the
same `SolidWall` helper, with `Popout()` only killing velocity on impact
substeps. The two systems are structurally different enough that continued
sphere-probe patching will not converge on OG behavior.

Both complaints trace to ONE root mechanism: the wall-*presence* query that
drives climb entry/sustain flips normals and is too permissive, which churns
entry/release (pump) and grabs slopes. Incs 1–3 remove that churn; a stable
hold = one entry, not grab→release→fall→regrab.

## Architectural decisions

- **Decision: mirror OG's plane-based wall model** in the demake's collmesh
  path rather than further tuning the sphere probe. Rationale: the OG model is
  stable by construction (face-plane normal, closest-on-plane point, persistence
  of the gripped face), and it is what the reference feel was tuned against.
  Alternatives rejected: (a) keep sphere queries + more grace windows (two
  attempts already failed on device); (b) fully replace with the engine's own
  mesh-query abstraction (blast radius too large for one plan).
- **Decision: add a persistent gripped-face record** to the climb state
  (`climb.wall_normal` + `climb.wall_surface_id` already exist; extend with the
  gripping face/point). While climbing, re-acquire the *same* face by identity,
  not by closest-any-wall. Rationale: this is exactly `climbingWallNormal`/
  `climbingWallActor` in OG and is what eliminates the frame-to-frame normal
  flip that pumps the release check.
- **Decision: route grip presence and climb re-acquire through the new
  plane-based wall query**, decoupled from the motor's body-center flag.
  Rationale: OG checks grip at `SolidWaistTestPos`, a fixed +3 Z (up) from the
  feet, not the body center. The demake's `grip_present` already uses a
  waist-ish probe but a *different* radius than the motor, which created the
  flicker. One canonical waist query removes the two-probe mismatch.
- **Decision: keep the input model unchanged** — climb still requires a held
  shoulder (Z/L/R). The device-stuck-Z bit is real but separate; do NOT mask it
  by changing input. A stuck bit must remain a remap task (already documented).
  However, this plan makes the *grab* itself robust so that even with the bit
  stuck high the player holds cleanly instead of pump.
- **Decision: wall-slide is not a separate state** — full fall speed against a
  wall, no slide state, matching OG (gravity always runs in Normal; the climb
  SFX-only "wall slide" is not a physics state). Rationale: OG Source Player.cs
  `StNormalUpdate` applies gravity unconditionally and `Popout` only kills
  into-wall velocity on impact.

## Assumptions and answers from code

- Decision: `WallPushoutDist = 3`, `ClimbCheckDist = 4`, `ClimbSpeed = 40`,
  `ClimbHopUpSpeed = 80`, `ClimbHopForwardSpeed = 40`, `ClimbHopNoMoveTime =
  0.25`, climb cooldown 0.3 — already match the demake's MovementConfig (source:
  Celeste64-og/Player.cs:50-55 vs movement_config.hpp). No constant change
  needed, only the contact model.
- Decision: waist probe offset `SolidWaistTestPos = Position + Z*3` in OG. The
  demake's `climb_grip_probe_center_offset = -7.5` (feet-anchored) is a different
  but compatible convention (player height ~10; -7.5 ≈ +2.5 above feet ≈ waist).
  Keep the demake's offset convention; just make the radius equal to the motor's
  radius and use the same query path.
- Decision: `climb_grip_probe_radius` (6.5) vs `wall_probe_radius` (5.0) split
  was a band-aid. After the plane-based re-acquire, a SINGLE waist radius equal
  to the motor radius is correct and flicker-free because face normals don't
  carry the seam diagonals. Remove the second constant. Source:
  movement_config.hpp.
- Answer from code: `RunWallContact` already does full-3D popout with a Y cap
  and impact-only velocity removal; the geometry query it uses is the problem,
  not the popout math itself. Source: player_motor.cpp.

## Risks accepted

- The plane-based wall query must be authored against the demake's quantized
  collmesh (world-space float verts, BVH). It may be slower than the sphere
  query per face — budget the walk; the frame is RSP-bound and a few dozen wall
  faces per tick is cheap.
- Removing the extra `climb_grip_probe_radius` could change grip at extreme
  tangency again — but the face-plane normal is stable there, which is the
  whole point. Revisit if device still flaps.
- A stuck-Z input bit still causes climb to enter when the user expects no grab
  — cannot be fixed in game code; the runbook in AGENTS.md covers it. This plan
  makes the *held* behavior stable; the *entry* remains gated on the bit.

## Increment DAG

- Inc 1 — Plane-based wall query (`QueryWallsFacePlane`) (L) — DONE — depends on: none — unblocks: 2, 3
- Inc 2 — Persistent climb wall identity (M) — DONE — depends on: 1 — unblocks: 4
- Inc 3 — Motor wall contact via waist probe + stable re-acquire (M) — DONE — depends on: 1 — unblocks: 4
- Inc 4 — Device validation + docs + unify probe radii (S) — DONE — depends on: 2, 3

## Increments

### Inc 1 — Plane-based wall query (L) — DONE
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** a new host fixture can, for a vertical face with a seam and a
corner, return the **owning face-plane normal** (not the centre-relative
diagonal) and a stable on-plane point, deterministically — across the whole
tick, without the diagonal flip. Existing wall fixtures stay green.

#### Files to touch

##### src/user/gameplay/world/world.hpp
- What changes: add `WallHit QueryWallClosestFacePlane(const Room&, const Vec3&
  point, float radius, const Vec3& prefer)` — OG `SolidWallCheckClosestToNormal`
  parity.
- Function(s): `int QueryWallsFacePlane(const Room&, const Vec3&, float radius,
  WallHit* out, int max)`; wrapper that picks the hit with max
  `Dot(hit.plane_normal, prefer)`.
- Data shapes: reuse `WallHit` (already has `point`, `normal`, `plane_normal`,
  `pushout`, `face_id`).
- Integration points: climb entry, climb sustain, wall-jump, motor. The new
  query is called with the SINGLE canonical `wall_probe_radius` (= motor
  radius, 5.0) by BOTH the controller and the motor from Inc 1 onward — do NOT
  keep the 6.5/5.0 split. `climb_grip_probe_radius` is removed in Inc 4; until
  then both callers pass the same 5.0 value directly.
- Error paths: no face → `{ hit=false }`; degenerate triangle → skip; caller
  falls back to the existing `wall_normal`.

##### world.cpp
- What changes: implement `QueryWallsFacePlane` over the existing BVH candidate
  set. For each candidate triangle (material SOLID), compute the **geometric
  plane** from its three verts (already have `plane_normal` logic in
  `QueryWallsMesh`), **oriented toward the query point** (sign-flip via the dot
  of the raw normal with `point - triangle[0]`, exactly as `QueryWallsMesh`
  does at world.cpp — do not drop this, or inverted baked winding flips the
  pushout). Discard faces with `|plane_normal.y| >= kClimbNormalMaxY` where
  `kClimbNormalMaxY = 0.35` (OG `ClimbNormalCheck`: only near-vertical faces
  are walls). Compute the closest point on the plane at the query's own Y
  (project the query point onto the plane); pushout = radius - dist. This is
  what gives a stable normal and a stable "grip surface".
- Function(s): `static Vec3 PlaneProject(const Vec3& a, const Vec3& b, const
  Vec3& c, const Vec3& p)`.
- Error paths: identical to above.

#### Edge cases
- The wall is the floor of an overhang / a slanted trim — the `|normal.y|` gate
  (`kClimbWallMaxY = 0.35`) filters those out as non-wall. This MUST be
  strictly less than the floor gate (0.35), so a surface that the ground query
  calls a floor (>= 0.35) can never also be a wall (< 0.35). No ambiguity.
- A face whose plane is at the player's own level but whose triangle edges stop
  short of the query point: use the plane-projection regardless of triangle
  extent (matching OG, which only constrains by the plane distance, not the
  triangle silhouette). If the plane is within radius, it's a wall.
- Two faces overlap (coplanar trims): pick max `Dot(plane_normal, prefer)` for
  the query, matching `SolidWallCheckClosestToNormal`.

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: new `tests/wall_plane_query_smoke.cpp` (a vertical
  quad wall + a seam + a corner; assert the returned normal is the face normal,
  not the diagonal; assert pushout is > 0 when touching, 0 when not). Include an
  **inverted-winding face** so the orientation fix is locked (see
  `.agents/common-mistakes/og-map-polygon-winding.md`). Assert a ~45° slope is
  NOT returned as a wall (gate `kClimbWallMaxY = 0.35`).
- Done: suite green including the new fixture; existing wall fixtures unchanged.

### Inc 2 — Persistent climb wall identity (M) — DONE
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** climbing a corner now stays on the SAME face across frames;
the release check no longer flips because the query returns the stored face's
normal instead of the any-frame nearest. Host pump fixture asserts 0 re-entries
when climbing a corner/setting-free with neutral input.

#### State to touch

##### src/user/gameplay/player/player_state.hpp
- What changes: `ClimbState` currently stores `wall_normal` (Vec3) and
  `wall_surface_id` (owner/surface id). The stable identity for a static
  collmesh triangle is its **`face_id`** (array index), NOT `wall_surface_id`
  (which is the moving-surface owner). Add `int32_t grip_face_id = -1` to
  `ClimbState`; keep `wall_normal`. Persistence keys on `grip_face_id`.

##### player_controller.cpp (Climbing block)
- What changes: on entry and on each in-climb tick, re-acquire grip via
  `QueryWallsFacePlane` at the waist probe (radius 5.0), but **prefer the
  stored `grip_face_id`**: if the re-acquired hit is the SAME `face_id` as
  stored, keep the stored normal (do not overwrite). If the query returns a
  DIFFERENT face whose `Dot(face_normal, stored_normal) < 0.5` (genuine wall
  change, e.g. corner traversal), update `grip_face_id` + `wall_normal`. If the
  query returns NO face, keep the stored identity (grip-grace rides out a
  one-frame miss). This makes the release check stable on a face even when a
  nearby edge/corner's sphere would have flipped.
- Function(s): helper `bool ReAcquireGrip(room, state, radius) -> bool`
  (returns whether grip is still present, updating stored identity on real
  wall change).
- Integration points: the `grip_present` computed in the Climbing block, and
  the release guard.

#### Edge cases
- Player faces away from the gripped wall → the re-acquired face's
  `Dot(normal, -facing) < 0.5` → release (still correct).
- Climbing onto a new wall (corner traversal) → the stored face fails the
  facing check → re-acquire the new face → continue, no pump.
- Re-acquired face is the floor (steep normal) → treat as no wall → release
  (OG `ClimbNormalCheck`).

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: extend `climb_mantle_smoke` with a corner fixture
  (two walls at 90°) — climbing up the first must NOT re-enter when reaching
  the corner; assert single entry + zero uncommanded exits (reuse the existing
  pump-check pattern). Assert `state.climb.grip_face_id` stays constant across
  the hold (persistence key), and only changes when the player rounds onto the
  second wall.
- Done: green; corner fixture asserts 0 pump and stable `grip_face_id`.

### Inc 3 — Motor wall contact via waist probe + stable re-acquire (M) — DONE
**Depends on:** 1
**Unblocks:** 4
**Done criteria:** `wall_contact` in `PlayerMotor::Step` is sourced from the
plane-based waist probe (the same query the climb uses), not the body-center
sphere. Sliding down a wall brushes at full fall speed with no damping and no
seam pump, verified by the existing `wall_slide_seam_smoke`.

#### State  touching

##### player_motor.cpp
- What changes: `RunWallContact` currently queries `QueryWallNearest` (sphere)
  at the body center and uses `wall.plane_normal`. Keep the SWEEP/POPOUT
  resolution at body-center (the feet/body must not clip geometry), but source
  the `wall_contact` FLAG from the new plane-based waist query so climb entry
  and the motor agree on grip. Concretely: `RunWallContact` computes the
  physical pushout/impact from `QueryWallNearest(state.position, radius)` (as
  today), AND separately sets `result.wall_contact`/`state.wall_contact` true
  when `QueryWallsFacePlane(waist, radius, -facing)` returns a wall. The two are
  combined (OR) so the flag is true when either the body resolves a wall or the
  waist grips one — no seam gap.
- Function(s): `RunWallContact(PlayerState&, ...)` — keep the sweep at body
  center; add the waist flag source.
- Integration points: the per-substep + end-of-tick wall pass.
- Error paths: fall back to `QueryWallNearest` (sphere) for both when the mesh
  isn't present or the plane query returns nothing.

#### Edge cases
- Waist point is outside a face's plane reach when the player is deep in a
  pit: plane projection distance > radius → no waist grip → flag false (unless
  body sweep resolves a wall), so she falls — no phantom grip (matches OG where
  the wall must be within `ClimbCheckDist`).
- Player against a floor edge / step: the floor is a `|Y|` face and excluded;
  the wall query does not grab the step side.
- The waist flag and the body sweep disagree transiently at a seam: keep the OR
  so a body-resolved wall still sets the flag; the persistent identity (Inc 2)
  prevents the controller from flipping.

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests add/update: `wall_slide_seam_smoke` (must stay green; its wall is two
  quads sharing a vertical seam — now the waist flag is stable per face, so
  fall speed is preserved). `motor_wall_rest_smoke` (resting on a wall → the
  flag stays true, no re-impulse).
- Done: suite green; seam and rest fixtures pass.

### Inc 4 — Device validation + doc + probe radius cleanup (S) — DONE
**Depends on:** 2, 3
**Unblocks:** none
**Done criteria:** rebuilt ROM boots; Ares capture shows NO repeating
`[climb] START` at the same/depth position while holding Z — measured as
`climb_start_events` ≈ 1 per held wall-climb, not dozens; grab feels clean on
the Forsaken city wall.

#### Files to touch

##### movement_config.hpp
- What changes: remove the now-unneeded `climb_grip_probe_radius` divergence;
  use a single `wall_probe_radius` for both the controller grip probe and the
  motor waist query, equal to `PlayerMotorConfig::radius`. Keep the doc that the
  plane-based model needs no skin.

##### AGENTS.md
- What changes: status bullet — wall contact is now plane-based & persistent
  (OG parity), not sphere; the seam/grab pump is gone; note the Ares Z-remap
  runbook is still the arbiter for "grabs without any shoulder" input-level
  cases.

##### docs/movement_spec.md
- What changes: replace the "wall-contact model" table (previous plan) with the
  plane-based query + persistent-identity description.

#### Edge cases
- A stuck-Z bit still reports grabs the user didn't intend — the remap runbook
  is the arbiter; this plan makes the held behavior stable but cannot invent a
  physical shoulder press.
- Removing the extra probe radius: if device still flaps at extreme tangency,
  the plane model's stable normal should prevent it — flag and revisit rather
  than re-adding a skin.

#### Verification (Ares checklist)
- `[input] z=0` at rest (post-remap) is a pre-condition to interpret the rest.
- Jump onto a flat wall holding Z: continuous grab, no pump.
- Hold Z + up: climbs continuously; up+over mantles at the lip.
- Corner climb: one grab, no re-enter.
- Fall against a wall: full fall speed, no slow-stick.
- **Numeric gate:** reuse the `climb_start_events` telemetry (`[telemetry]`
  line) — a single held wall-climb must report ≈1 climb entry (not dozens). The
  Ares gate is a before/after entry-count comparison, not a visual judgment.
- `./tests/run_host_tests.sh` green; `./compile-rom.sh` clean.

## Cross-cutting verification
After Inc 4: one continuous Ares session exercising every item above with
telemetry; capture saved to `.plans/wall-climb-overhaul/capture-final.txt`.
Host suite 100% green immediately before closing.

## Standards / common-mistakes referenced
- `.agents/learnings/stale-contract-vs-working-tree.md` — evidence hierarchy:
  device capture > working tree > comments; don't "fix" a red test that
  encodes the old model.
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — the `[input]` /
  `[climb]` / `[telemetry]` lines are the arbiter for grab claims; cite latest
  capture.
- `.agents/common-mistakes/og-map-polygon-winding.md` — baked faces can come
  in reversed winding (bake-side), so the runtime plane query MUST orient each
  face normal toward the query point (Inc 1) rather than trust winding; the
  fixture locks it (see Inc 1 Verification).

## Open questions (CONSIDER from review)
- Whether to also make the *entry* (TryClimb) use the same plane query as the
  sustain — YES, so entry/sustain see the same wall. The entry gate stays
  `input.climb_held` (shoulder) + face near + facing — unchanged.
- Whether to keep the Z-remap note in AGENTS.md given the switch — keep; a stuck
  bit is still a bit the game cannot ignore.

## Out of scope
- Input binding / remap code (stuck-Z is user-side Ares; runbook stays).
- Camera orbit/follow tuning (OG-exact already).
- The engine's own abstracted mesh query layer (out of scope for this rewrite;
  use the collmesh directly).
- Feather / dash state interactions (out of scope; climb is Normal/Climbing
  only).
- The dash-into-wall pin behavior (OG parity: straight-on dash pins until
  timer end — accepted, not changing).
