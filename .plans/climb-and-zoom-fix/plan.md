# Climb Wall-Contact Overhaul + Camera Zoom Speed

## Context

Two user-reported problems, one now fully attributed:

1. **"Madeline always grabs" / climbing still wrong.** Device capture (2026-08-26,
   `[input]`/`[climb] START` telemetry) proves TWO independent causes:
   - **(a) Stuck Z bit**: `z=1 l=0 r=0 al=0 ar=0` constant for minutes of active
     play — the Ares input mapping feeds something idle into the emulated N64
     Z trigger. Every "auto grab" was a legitimate Climb entry caused by lying
     input. Code cannot fix a stuck physical/mapped button; the FSM is provably
     gated (`player_controller.cpp:363,369`). User-side remap required.
   - **(b) Seam pump**: 43 consecutive `[climb] START` prints at an identical
     position with a pure vertical wall normal — release/regrab churn EVERY tick.
     Root cause per audit (explore log `/tmp/hawk-plan-large-explore-3.log`):
     sphere wall queries return closest-point→center normals (diagonal near
     seams/edges, with Y components), and `RemoveIntoNormal` uses the full 3D
     dot — so brushing any seam eats fall speed (vy −120 → ~−28 in one removal,
     gravity refills only 10/tick). XZ-only pushout re-penetrates every tick,
     re-firing contact forever. Sphere-sweep floor acceptance (`normal.y ≥ 0.35`)
     also hooks her mid-wall on diagonal trim geometry.

   The complete fix is OG Celeste64 parity for wall contact: face-plane normals,
   velocity removal only on impact frames, full-3D positional popout (which also
   yields free step-up over ankle-high geometry).

2. **Camera zoom too slow** (user-selected from options; orbit/follow match OG
   exactly and stay untouched). OG zoom rate is 1.0 normalized-distance/sec
   (`Player.cs:283`); ours matches, but feels slow on a stick/buttons. Tuned
   deviation: faster rate + faster detent snap.

## Architectural decisions

- Decision: **keep level-held climb entry** (`input.climb_held`, OG `Down()`
  semantics). Rationale: capture proves input was the liar, not the gate;
  edge-entry would make climbing impossible while a bit is stuck high.
  Alternatives rejected: press-edge-to-grab (breaks under the very bug it
  would mask), ignoring Z entirely (Z is canonical grab).
- Decision: **add face-plane normals to sphere wall queries** rather than
  replacing them with raycasts everywhere. Rationale: grip presence /
  closest-normal queries power climb + mantle and are heavily test-covered;
  swapping to raycasts changes every call site at once. Face normal comes from
  the owning triangle where available, falling back to the current
  center-relative normal with its Y component clamped toward 0 for the
  horizontal part of the math. Alternative rejected: full raycast rewrite
  (correct but one-PR-too-big blast radius).
- Decision: **velocity removal becomes impact-only + horizontal-clamped**, mirroring
  OG `Popout` (`Player.cs:694-712`): pushout is full-3D every contact, but the
  into-normal velocity component is removed only on contact-start substeps
  (edge), using the face-plane normal. Rationale: kills the continuous fall
  damping AND preserves the existing wall-rest feel verified on device.
- Decision: **sphere-sweep floor acceptance tightened separately from raycast
  floor acceptance.** Raycasts keep true face normals and the 0.35 gate;
  sphere sweeps require ≥0.70 unless confirmed by a downward raycast. Kills
  mid-wall grounded hooks without breaking baked ramp standing.
- Decision: **zoom rate becomes a config constant** defaulting to 1.8×/s with
  detent snap approach doubled (dt·1.0). Documented deliberate deviation from
  OG's 1.0/s (input-medium difference: stick vs OG mouse).
- Decision (found during device re-verify): `zoom_rate` alone does NOT fix
  perceived zoom — the camera-position follow (`camera_follow_decay=0.01`,
  ~7%/frame) lags the actual camera ~2-3s behind a fast `target_distance`
  sweep. While zoom input is active the follow decay tightens to `0.0001` so
  the camera chases the zoomed-in spot immediately; when zoom stops it eases
  back on the normal decay.

## Assumptions and answers from code

- Decision: stuck-Z attribution is final; no further input-layer code change.
  Source: device capture `[input] z=1 ... | c=1` across all samples +
  `[climb] START z=1` at grab frames | user-confirmed scope ("full overhaul").
- Decision: orbit (4 rad/s) and follow (XZ snap, Y pad 8 decay .001, position
  decay .01) already equal OG (`Player.cs:273,446-477`) — out of scope.
  Source: code @ camera_controller.cpp:154-159,183-212 vs Player.cs:446-478.
- Decision: zoom detents (interval 1/3, threshold 0.1) stay; only rate/snap speed
  change. Source: code @ camera_controller.cpp:162-181 vs Player.cs:288-292.
- Decision: OG slide behavior = full fall speed at walls, no slide state.
  Source: code @ Celeste64-og/Source/Actors/Player.cs:1179 (gravity always in
  Normal state); "wall slide" is climb-only SFX (:1620-1628).
- Decision: kEnableBackCull stays false (user device fix, locked contract).
  Source: .agents/learnings/stale-contract-vs-working-tree.md.

## Risks accepted

- Face-plane retrieval may be unavailable for edge/vertex-region hits → fallback
  path keeps old behavior minus Y-damping; seam fix still lands because the
  dominant case (face hits) gets true normals. Revisit if seam test still fails.
- Impact-only removal could let fast dashes sink slightly into walls between
  substeps → accepted; pushout corrects position each substep and dash is
  ≤0.22 s. Revisit if dash-through-wall reported.
- Full-3D popout can eject upward off diagonal trims → cap per-substep Y
  correction at ±6 units; step-up benefit preserved (OG parity for lips).
- Tightened sweep-floor gate could un-ground players on borderline ramps that
  today rely on the hook → ground-snap (50 units) covers descent onto real
  floors; slope-standing range narrows to true-face slopes only.
- Zoom feel is subjective → single knob (`zoom_rate`), device check before close.

## Increment DAG

- Inc 1 — Ares Z-remap runbook (S) — depends on: none — unblocks: 5
- Inc 2 — Face-plane wall normals + impact-only velocity removal + full-3D
  popout (L) — depends on: none — unblocks: 3, 5
- Inc 3 — Sphere-sweep floor acceptance tightening (M) — depends on: 2 —
  unblocks: 5
- Inc 4 — Zoom speed tuning (S) — depends on: none — unblocks: 5
- Inc 5 — Device validation + docs sync (S) — depends on: 1, 2, 3, 4

## Increments

### Inc 1 — Ares Z-remap runbook (S) — DONE
**Depends on:** none
**Unblocks:** 5
**Done criteria:** AGENTS.md ROM-testing section contains a numbered remap
procedure naming the exact Ares settings page, and states how to verify via the
existing `[input]` telemetry line (`z=0` at rest).

#### Files to touch

##### AGENTS.md
- What changes: add "Ares controller mapping hygiene" subsection under ROM
  testing notes.
- Content: Settings → Input → Port 1; find the N64 **Z** binding; remove/rebind
  whatever idle control feeds it (common offenders: analog-trigger mappings and
  stick-clicks with bad rest thresholds); launch ROM, confirm `[input] z=0`
  before touching anything; repeat for L/R if ever suspected.

#### Edge cases
- User's device may be keyboard or gamepad — instructions phrased for both.
- If z=1 persists after remap, next suspect is hardware; `[input]` line remains
  the arbiter.

#### Verification
- Run: manual — rebuild not required (docs-only).
- Done: user can follow the steps and reach `z=0` at rest.

### Inc 2 — Face-plane wall normals + impact-only velocity removal + full-3D popout (L) — DONE
**Depends on:** none
**Unblocks:** 3, 5
**Done criteria:** host test sliding a motor-driven player down a flat wall
containing a perpendicular seam reaches vy ≈ max_fall (no damping); corner-grip
test holds Climbing 60 ticks with zero exit/re-enter cycles; existing suite green.

#### Files to touch

##### src/user/gameplay/world/world.hpp / world.cpp
- What changes: `WallHit` gains `Vec3 plane_normal` (true face normal when the
  query resolved against a triangle face; fallback = center-relative normal).
- Function(s): `QueryWallClosestToNormal(const Room&, const Vec3& center, float radius, const Vec3& prefer)` fills both `normal` (unchanged semantics) and new `plane_normal`.
- Integration points: player_controller climb block, player_motor RunWallContact.
- Error paths: no hit → plane_normal = {0,0,0}; caller guards as today.

##### src/user/gameplay/physics/geom.cpp (+ physics/coll_mesh.cpp sweep plumbing)
- What changes: closest-point resolution records the owning face normal when the
  feature region is a face; edge/vertex regions keep center-relative fallback.
  Feasibility verified: `SphereTriCollision` (geom.cpp, Ericson §5.2.7) computes
  the triangle plane normal already for the on-plane fallback (:188,
  `Norm(Cross(ab,ac))`) — capture it into the hit struct on face-region hits;
  BVH traversal keeps the winning triangle so the plane normal survives to the
  query layer.
- Data shapes: extend the existing hit out-struct with `plane_normal`; no ABI
  break beyond the struct field.
- Error paths: degenerate triangles → skip plane fill, leave fallback.

##### src/user/gameplay/player/player_motor.cpp
- What changes: `RunWallContact` — (1) positional pushout becomes full-3D
  (Y included) with per-substep |Δy| cap 6.0; (2) velocity removal becomes
  OG-`Popout` parity: on contact-edge substeps (wall_contact false→true within
  the Step) or when requested velocity drives >1 unit deeper into the wall,
  remove the full-3D into-normal component once, using `plane_normal`
  (`velocity -= n * min(0, dot(vel,n))`); resting contact NEVER edits velocity;
  no Y-clamping of the removal itself — true face normals carry no spurious Y,
  and one-time impact loss is gravity-refilled while sliding; (3) fallback when
  plane_normal invalid (<0.1 length): legacy XZ-only `RemoveIntoNormal`, also
  edge-gated.
- Function(s): `RunWallContact(PlayerState&, …)` internal signature unchanged;
  new static helper `ApplyImpactRemoval(state, plane_normal)`.
- Integration points: called per-substep + unconditional end-of-tick pass
  (existing structure preserved).
- Error paths: plane_normal invalid → legacy path as above.

##### tests/wall_slide_seam_smoke.cpp (new)
- What changes: synthetic collmesh floor + tall wall built from two quads whose
  shared seam runs vertically; drive player airborne against wall, hold nothing;
  assert vy reaches ≤ −110 within 60 ticks (today fails ≈ −30..−60), assert no
  `[climb]`-style state entries (movement_state never Climbing without input),
  assert XZ drift < 2 units (no popout launch).
- Wire into tests/run_host_tests.sh GAMEPLAY_LINK block.

##### tests/climb_mantle_smoke.cpp
- What changes: extend corner scenario — count Climbing exits during a 60-tick
  hold; assert 0 re-entries after initial grab (pump regression).

#### Edge cases
- Dash into flat wall: FEEL CHANGE (accepted) — straight-on dash now pins
  against the wall until the timer ends instead of hard-stopping on first
  touch; angled dashes slide along tangentially. Existing dash tests must stay
  green; add an assert that dash-end position stays outside the wall surface.
- Walljump away: velocity set outward; contact edge fires once, harmless.
- Grounded wall-rest (device-verified in climb-wall-physics): zero-net-force
  resting behavior must survive — resting contact never edits velocity under
  the new rule; motor_wall_rest_smoke is the regression guard and must pass
  unmodified.
- Ceiling-adjacent seams: Y-capped pushdown cannot fight floor snap (snap 50 »
  cap 6, fine).
- wall_slide_seam_smoke must also assert grounded-rest preservation (reuse
  motor_wall_rest_smoke's rest assertion pattern) so the seam fixture doubles
  as a combined guard.

#### Verification
- Run: `./tests/run_host_tests.sh`
- Tests to add/update: wall_slide_seam_smoke (new), climb_mantle_smoke (extended).
- Done: all green including the two named above.

### Inc 3 — Sphere-sweep floor acceptance tightening (M)
**Depends on:** 2 (sequencing, not data: same sweep-acceptance code region in
player_motor.cpp is touched by Inc 2's contact rework; serializing avoids a
self-conflict)
**Unblocks:** 5
**Done criteria:** knee-high lip walks over via pushout step-up (not grounded);
diagonal trim at mid-wall no longer sets grounded; ramp standing unaffected
(existing surface_parity fixtures stay green).

#### Files to touch

##### src/user/gameplay/player/player_motor.cpp
- What changes: substep floor sweep accepts `SweepSphereMesh` hits as floor only
  if `normal.y ≥ 0.70`, OR `normal.y ≥ 0.35` plus downward-raycast floor
  confirmation at the sweep point. Raycast-path floor gate stays 0.35.
- Function(s): the sweep acceptance site (~line 186-203) gains
  `ConfirmFloorByRay(room, point)` helper (world.hpp inline wrapper over
  QueryFloorSource).
- Integration points: ApplyGroundContact unchanged below the gate.
- Error paths: raycast miss → treat as wall (RunWallContact handles next substep).

##### tests/climb_mantle_smoke.cpp
- What changes: add knee-high lip fixture (0.75-radius player vs lip height ~5):
  walking into it with move input must raise feet onto it within 30 ticks
  (grounded on top), not stop dead.

#### Edge cases
- Steep ramps (31.8°–69.9°): standing now requires ray confirmation — baked city
  ramps are true faces, rays confirm; pure-sphere-only diagonals reject.
- Moving platforms: unaffected (separate surface system).

#### Verification
- Run: `./tests/run_host_tests.sh`
- Done: lip walkover passes; surface_parity_smoke + gameplay_smoke green.

**Single-mover migration follow-up (2026-09-25):** `player_motor.cpp` was
later deleted by `.plans/og-mover-overhaul/`. Its lip criterion had no live
MoveService coverage and regressed to a hard stop at `x=-3`. The current owner
is `MoveService::TryStepUp` in `player_movement.cpp`; `move_service_smoke`
covers 5-unit walkover plus rejection of a 6-unit lip and a low overhang.
This supersedes this increment's old file/test ownership, not its acceptance
intent.

### Inc 4 — Zoom speed tuning (S)
**Depends on:** none
**Unblocks:** 5
**Done criteria:** C-Up/Down sweeps full zoom range in ≈0.55 s (was 1.0 s);
detent settle visibly quicker; no other camera behavior changed.

#### Files to touch

##### src/user/gameplay/player/camera_controller.hpp
- What changes: `CameraConfig` gains `float zoom_rate = 1.8f` (normalized
  distance/sec) and `float zoom_snap_speed = 1.0f`.

##### src/user/gameplay/player/camera_controller.cpp
- What changes: `target_distance += input.zoom * config.zoom_rate * dt` (replaces
  implicit 1.0 at cpp:163); detent snap `max_delta = dt * config.zoom_snap_speed`
  (replaces 0.5f at cpp:170).
- Integration points: none downstream (clamp [0,1] unchanged).

##### docs/movement_spec.md
- What changes: camera section notes the deliberate zoom deviation (1.8×/s vs
  OG 1.0/s) with rationale (stick vs mouse medium).

#### Edge cases
- Analog C-stick partial deflection scales proportionally (unchanged semantics).
- Snap fights zoom? Snap runs only at zero input — unchanged guard.

#### Verification
- Run: `./tests/run_host_tests.sh` (camera constants compile-time only; suite
  guards regressions elsewhere).
- Done: constants present, doc updated, device feel confirmed in Inc 5.

### Inc 5 — Device validation + docs sync (S)
**Depends on:** 1, 2, 3, 4
**Unblocks:** none (closes plan)
**Done criteria:** rebuilt ROM passes an Ares session checklist; AGENTS.md
status paragraph reflects new wall-contact model + zoom constants.

#### Files to touch

##### AGENTS.md
- What changes: status bullet — wall contact is face-plane/impact-based (OG
  Popout parity), seam-slide fixed, zoom 1.8×/s.

##### docs/movement_spec.md
- What changes: wall-contact model table (normals source, removal condition,
  pushout axes/caps, sweep-floor gates).

#### Edge cases
- Remap not done before validation: `[climb] START z=1` will still appear —
  checklist item 1 is a hard gate for interpreting every other item.
- Validation finds seam-slide still damped on some geometry → fallback path
  engaged; file follow-up rather than expanding this plan.

#### Verification
- Run: `./compile-rom.sh && ./tests/run_host_tests.sh`
- Done: Ares session checklist below passes end-to-end and docs merged.

#### Verification checklist (Ares, after `./compile-rom.sh`)
- `[input] z=0` at rest post-remap (Inc 1 done on user side).
- Jump onto flat wall holding nothing: slides at full fall speed, no slow-stick.
- Hold Z on wall: climbs; up+lip mantles; release lets go instantly.
- Corner hang: zero `[climb] START` spam in capture (≤1 per genuine grab).
- Knee-high lips walk over without jumping.
- Zoom C-Up/C-Down: full range ≈ half a second, settles on detents.
- Host suite + ROM build clean.

## Cross-cutting verification
After Inc 5: one continuous Ares session exercising every checklist item above
with telemetry visible; capture saved under
`.plans/climb-and-zoom-fix/capture-final.txt` for the record. Host suite 100%
green immediately before tagging done.

## Standards / common-mistakes referenced
- .agents/learnings/stale-contract-vs-working-tree.md — evidence hierarchy when
  a test contract disagrees with device-verified defaults.
- .agents/learnings/n64-opt-device-verify-and-telemetry.md — cite latest
  captures; [input]/[climb] lines are the arbiter for grab claims.

## Open questions (CONSIDER from review)
- Capture artifact: use the literal path
  `.plans/climb-and-zoom-fix/capture-final.txt` in Inc 5 (slug was written as a
  placeholder `<slug>`).
- If face-region capture proves unreliable for edge/vertex-heavy baked geometry,
  consider promoting the raycast-everywhere rewrite from Out-of-scope earlier
  rather than layering more fallbacks.

## Out of scope
- Orbit/follow/FOV camera tuning (constants already OG-exact; revisit only after
  zoom lands and user re-reports).
- Raycast-everywhere replacement of sphere queries (larger rewrite; revisit if
  fallback paths prove insufficient).
- Any input-binding code change for the stuck bit (user-side Ares remap only).
- Corner-climb lateral traversal (still deferred, unchanged from prior plan).
