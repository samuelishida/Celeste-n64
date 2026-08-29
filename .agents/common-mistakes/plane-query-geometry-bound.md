# Plane query must be geometry-bounded to triangle vertex extents

## Symptom

On-device Ares capture shows a **frozen climb pump**: `[climb] START` and
`[climb] release grip_lost (grip=0 ...)` firing every single frame at one
position, with the entry probe repeatedly succeeding while the sustain grip
immediately reports `grip=0`. In the reported case the player sat at
`pos=(5.0,50.3,35.4)` with wall normals toggling between two perpendicular
faces.

## Cause

`QueryWallsFacePlaneMesh` (world.cpp) projects the query point onto a wall
face's **infinite plane** and accepts a hit when the AABB overlaps the
triangle. A probe **above a wall's physical top edge**, or beyond an **open
vertical edge**, still "hits" the infinite-plane projection even though the
real triangle ended. Entry uses a larger radius (`wall_probe_radius`=5) than
the sustain grip probe (`climb_check_dist`=4), so at a wall top/corner the
entry radius AABB just grazes the triangle's corner and reports a ghost grab,
while the tighter sustain probe can't reach real geometry → release →
re-grab → infinite pump.

## Fix

Bound the plane-query hit to the owning triangle's actual vertex extents
before emitting the `WallHit`. If the projected closest point lies outside
`[vmin - skin, vmax + skin]` on any axis, skip the triangle. This makes the
query report real geometry only (a face ends at its triangle vertices), the
same semantics OG's `ClimbCheckAt` has against actual geometry. A small skin
(0.5) preserves the pushout-tangent grab boundary.

## Lesson

The plane-based wall query is NOT a substitute for a triangle-extent check.
Always geometry-bound to vertex extents for probe radii that can reach
"past" an edge. Mirror the existing `LedgeHopMiss` bounding.
