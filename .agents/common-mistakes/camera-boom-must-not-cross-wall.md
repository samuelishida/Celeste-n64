# Camera boom must not cross wall after collision

When camera obstruction shortens a third-person boom, do not enforce a minimum
player distance by moving the camera back along its original ray. In tight
spaces that second correction can put the camera inside the obstructing solid.
The player may then appear embedded or disappear behind the wall.

Resolve the actual smoothed camera point against geometry, keep it on the
player/look-at side of the hit, and adapt the render near plane if the shortened
boom brings the avatar close to the camera. Test wall containment and free-room
framing; a distance-only assertion can bless the broken result.
