# Acceptance scenarios

## Scenario 1: Test level loads with CollMesh

GIVEN a freshly built `madeline_cube_rom.z64` after Inc 4
AND the test level's manifest has `use_collmesh = true`
WHEN the player enters the test room
THEN `Room::coll_mesh` is non-null
AND `world_query_parity_test` reports zero divergences vs legacy on 1 000
sampled queries.

## Scenario 2: Swept-sphere walking against a triangle wall

GIVEN Inc 6 completed
AND the test level contains a wall built from two coplanar triangles sharing
an edge
WHEN the player walks at the seam at full ground speed
THEN player slides smoothly along the wall — no snag, no penetration
AND the recorded path matches the captured baseline within 1e-2 per axis.

## Scenario 3: 60 Hz determinism under variable frame rate

GIVEN Inc 5 completed
WHEN `timing_fixed_step_test` injects the exact deterministic dt sequence
documented in `plan.md` Inc 5 (300 ms-values summing to 5.000 s)
THEN motor tick count == 300
AND no frame consumes >5 ticks
AND the spiral cap engages exactly on the 100 ms entry
AND `InterpolatedPosition(prev=(0,0,0), curr=(10,0,0), alpha=0.5) == (5,0,0)`
within 1e-4
AND `prev_position` is set to `position` on spawn/teleport/room-transition
events (covered by dedicated case).

## Scenario 4: Material flags drive gameplay

GIVEN a triangle authored with material name suffix `_climbable`
WHEN the player swept-sphere contact hits that triangle
THEN `WallHit.face_id` references that triangle
AND `coll_mesh.triangles[face_id].material & 0x0008 != 0`
AND climb input transitions player to grab state.

## Scenario 5: Moving platform carries player

GIVEN a `MovingSurface` whose colliders moved this tick
AND `SurfaceLink` entries map its `face_id`s to its `owner_id`
WHEN the player stands on a face owned by that surface
THEN `MotorResult.position` includes the surface displacement
AND `rider_velocity` is exposed for stored-velocity carry on jump.

## Scenario 6: Per-level rollback

GIVEN Inc 7 not yet shipped
AND a level on `use_collmesh = true` regresses
WHEN that level's manifest is flipped back to `use_collmesh = false`
THEN the level loads via legacy `Collider[]`
AND no rebuild required (legacy data still in repo until Inc 7).

## Cross-cutting checks

- After Inc 2: bake the largest level in `data/levels/`. Confirm `.colmesh`
  size ≤ 256 KB. If exceeded, NO-GO on Inc 3 until format revised.
- After Inc 4: golden raycast trace of player_motor for one minute of
  recorded input on test level. Compare to pre-Inc-4 trace. Must match
  within 1e-3 per axis (legacy still in use here — confirms parity test
  is sound).
- After Inc 6: full playthrough of test level by hand. Walk, jump, dash,
  wall-jump, climb, spring, spike. Each interaction must feel parity with
  legacy build. Subjective — record video if disputed.
- After Inc 7: `rg -n 'ColliderType|kMaxColliders|Collider colliders' src/`
  returns zero hits.
- Rollback test: `git revert` of each increment's PR leaves `make test` green.
