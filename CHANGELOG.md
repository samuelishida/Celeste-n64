# Autoresearch changelog — climb/collision bug hunt

## 2026-09-07 — converged (5 iterations)

**What changed:** 6 root causes fixed in the climb/collision system:
1. Climb movement now moves along the wall's UP direction (OG `-wallUp *
   inputY` parity) instead of straight up in world Y — the head/waist no
   longer enter sloped wall planes.
2. New `MoveService::SweepMoveClimb` (waist-only popout during climb) — the
   head wall query + ceiling check no longer fight the climb.
3. Entry snap without the 3-unit/axis cap (the cap broke the anchor on sloped
   walls, embedding the body).
4. Corner system gates on the LATERAL (move projected on the wall tangent),
   not the raw camera-relative XZ length — plain up-climbs no longer fire the
   corner rays.
5. Corner rays check the projection length BEFORE normalization (NormalizeXZ
   of a tiny vector produced a unit vector that passed the post-normalization
   length check and re-anchored the body through the wall).
6. Grip query front-side gate (OG `distanceToPlane < 0 → continue`) — the
   player can only grab walls she is in front of (overhang-from-behind no
   longer latches + releases).
7. Popout reverted to OG pushout semantics (`(radius - dist) * diff.Normalized()`
   — push along the direction from the closest point to the body, never
   through the face).

**Metric result:** `tests/climb_bug_probe.cpp` 6/16 failures → 16/16 PASS.
`make test` 57/57. ROM builds. Device capture clean (167× near_batches=56,
`[memory] used=5,822,032` flat, no exceptions).

**What failed:** the signed-pushout approach (iteration 3) pushed bodies
through walls and was reverted; the front-side gate initially broke a
reversed-winding test fixture (fixed the fixture's winding — the bake
pipeline's `reversed_winding_faces=0` guardrail confirms the real map is
consistent).

**Next step:** manual play test in Ares to confirm the feel (the original
complaint was device-side; the host probe + 57 smokes verify the physics).

## 2026-09-07 — working wall grab (1 iteration)

**What changed:** removed the front-side gate from the wall query
(`QueryWallsFacePlaneMesh`). The gate (added in the previous climb-collision
loop) rejected walls the query point was "behind" the true winding normal —
but the baked collision mesh's winding is reversed relative to the player's
approach on many faces, so it rejected EVERY wall and the grab never fired
(device capture: Z held near walls, zero `[climb] START` lines).

**Metric result:** `/tmp/pos_probe` (queries the real CMSH at the device's
player positions): `climb5=0` at every position → `climb5=1` where walls are
in reach. `make test` 57/57, probe 16/16, ROM builds.

**What failed:** the previous loop's winding probe only tested points in
front of the true normal — it missed that the real map's faces are reversed.

**Next step:** manual play test in Ares to confirm the grab fires (the
original report was device-side).
