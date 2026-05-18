# Inc 4 — World query rewrite notes

## Material bit → existing semantics mapping

| `CollTriangle.material` bit | Existing code concept | Maps to |
|---|---|---|
| 0x0001 solid    | `Collider.solid == true`                    | blocks movement |
| 0x0002 oneway   | (new — no legacy equivalent)                | resolves only when sweep dir · normal < 0; passes upward |
| 0x0004 death    | gameplay tag, not motor (player_state)      | motor reports hit; gameplay reacts |
| 0x0008 climbable| `WallHit.is_climbable` (currently inferred) | sets `wall_normal` + `face_id`; controller decides grab |
| 0x0010 ice      | (new)                                       | friction = 0.0 in motor's tangent damping |

Bits beyond 0x0010 reserved. Add only with `data-model.md` version bump.

## BackfacePolicy passthrough

`BackfacePolicy::Cull` skips triangles where `ray_dir · tri_normal > 0`.
`BackfacePolicy::Ignore` keeps them (used by ground-snap from inside a slope).
Same semantics as legacy `RaycastRoomSource`.

## GroundHit/WallHit/CeilingHit construction from triangle hit

- `normal` = triangle normal (precomputed at bake time? No — derive at runtime
  from CCW vertex order; cheap and avoids storing 12 B per tri).
- `face_id` = triangle index (matches `data-model.md §2`).
- `point` = ray origin + ray dir × t.
- `is_oneway` / `is_climbable` etc.: AND with material bits.

## Parity-test sampling rule

Origins on a 5×5×5 grid inside the test level AABB. Directions: 26 axis +
diagonal vectors. = 25^3 × 26 ≈ 400 K rays, sample 1 000 with deterministic
seed.

## Room::use_collmesh toggle

```cpp
struct Room {
  // ...legacy:
  Collider colliders[kMaxColliders];
  int collider_count = 0;
  // new:
  const CollMesh* coll_mesh = nullptr;
  bool use_collmesh = false;
};
```

Every query function:

```cpp
GroundHit RaycastRoomSource(const Room& room, ...) {
  if (room.use_collmesh && room.coll_mesh)
    return RaycastRoomMesh(*room.coll_mesh, ...);
  return RaycastRoomLegacy(room, ...);
}
```

Single branch, no shared helper code between the two — keeps cutover atomic
per query.
