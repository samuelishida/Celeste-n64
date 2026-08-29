# Climb physics parity: probe-shell geometry coupling (climb-parity-3)

## Context
Porting OG Celeste64's climb wall-check graph into the demake's query layer
(`.plans/climb-parity-3/`, Inc 1-5). The plan's MUST-FIX #3 predicted a
deterministic grab-instant-release loop; it materialized exactly as written,
and several other geometry couplings surfaced that the plan's "one query
family" decision did not fully pre-solve.

## What surprised us
- **The entry probe radius and the motor's climb radius are COUPLED.** With
  the motor's Normal-state radius 5.0, the resting anchor places the waist 5
  units from the face — outside the r4 sustain probe's reach → grab/exit pump
  every tick. The fix is the plan's own `climb_collision_radius` (3.0 = OG
  WallPushoutDist): with it, the entry anchor is a zero-popout equilibrium AND
  the r4 probe stays live. But the DAG sequenced the motor consumption in Inc
  2, one increment too late for the shared motor+controller fixtures — the
  consumption had to be pulled forward into Inc 1. Lesson: when a plan changes
  a probe radius, audit the collision radius that determines where the body
  actually rests in the SAME increment.
- **Entry probe must use the visibility radius (5.0), not climb_check_dist
  (4).** At exact tangency the waist rests one unit beyond r4 reach, yet OG
  still grabs there (Popout pulls her in; dist == radius counts as touching).
  Entry re-anchors to `wall.point + n*3`, after which the r4 sustain governs.
- **The plane query projects onto the face's INFINITE plane.** A lip probe
  hovering above a wall's top edge still "hits" at an extension of the face
  plane. OG's `ClimbCheckAt` miss is against actual geometry — a face ENDS at
  its triangle vertices. The mantle gate must bound the hit to the matching
  wall faces' vertex extents (project the probe onto the plane first: a
  vertical wall is zero-thick along its own normal, so a raw AABB test on the
  probe point always fails that axis).
- **A raw grip-probe reset in the mantle counter suppresses the mantle at the
  real lip.** The plane grip query hits the infinite plane even above the wall
  top, so "grip present → reset counter" never lets the counter reach 2. The
  counter must reset via the lip-probe-supported else branch instead.
- **Corner rays must be room!=nullptr guarded.** Controller-only flag climbs
  (room==nullptr) segfaulted gameplay_smoke the moment the corner system
  raycast unconditionally.
- **Side-ray direction = move intent projected onto the wall tangent plane,
  NOT a world-axis ray.** A world-X-only side ray made the fixture drift -z
  away from the corner and never adopt the joining wall.
- **The force-corner ease must gate on a genuinely NEW wall**
  (`side_probe.face_id != grip_face_id`). Without it, a plain up-climb
  (travel > 2 units) triggered the ease every tick → climb rate collapsed to
  14.67 u/s.

## Lesson
When porting a physics system that mixes probe queries with a collision
radius, the probe shell and the body's resting position are one coupled
system — change them together, in the same increment, and verify the
equilibrium (zero-popout, drift < 0.5 over 30 ticks) with a fixture. Also:
any query that projects onto an infinite plane needs a caller-side
geometry-boundedness check to reproduce OG's "miss at a real edge".

## Reuse
- Any session touching climb/wall physics or porting OG Celeste64 movement:
  the probe-radius ↔ collision-radius coupling, the visibility-radius entry
  probe, the infinite-plane lip bound, and the room==nullptr ray guards.
- Write-up template: symptom → which geometry coupling → the equilibrium
  fixture that locks it.
