#include "gameplay/world/world.hpp"

#include <cmath>

#include "gameplay/physics/coll_mesh.hpp"

namespace madeline_cube {

namespace {

constexpr float kRayEpsilon = 0.0001f;
// Sentinel: colliders added at runtime (moving platforms) have owner_id >= 0.
// Static colliders from the legacy path have owner_id == -1 and are skipped
// when a CollMesh is present (CollMesh handles all static geometry).
constexpr int kStaticOwner = -1;

struct OwnerInfo { int id; Vec3 velocity; };

OwnerInfo ResolveOwner(const physics::CollMesh& mesh, const Room& room, int face_id) {
    uint16_t owner_raw = physics::SurfaceOwnerOf(mesh, face_id);
    if (owner_raw == physics::INVALID_OWNER) return {kStaticOwner, {}};
    int owner_id = static_cast<int>(owner_raw);
    const MovingSurface* ms = FindMovingSurface(room, owner_id);
    return {owner_id, ms ? ms->rider_velocity : Vec3{}};
}
inline bool IsStaticCollider(const Collider& c) { return c.owner_id == kStaticOwner; }

float Abs(float value) {
    return std::fabs(value);
}

Vec3 AddScaled(const Vec3& origin, const Vec3& direction, float distance) {
    return {
        origin.x + direction.x * distance,
        origin.y + direction.y * distance,
        origin.z + direction.z * distance,
    };
}

bool RaycastPlane(const Collider& collider, const Vec3& origin, const Vec3& direction, float max_distance,
                  float& distance, Vec3& normal) {
    if (Abs(direction.y) <= kRayEpsilon) return false;

    // Slopes set has_plane_origin so the plane equation references an actual
    // on-plane point.  Axis-aligned floors leave it unset and the min-corner /
    // max-y combination resolves to the same horizontal plane the bounds
    // describe (min.y == max.y for those colliders).
    const Vec3 plane_point = collider.has_plane_origin
        ? collider.plane_origin
        : Vec3{collider.bounds.min.x, collider.bounds.max.y, collider.bounds.min.z};
    const float denom = (collider.normal.x * direction.x) +
                        (collider.normal.y * direction.y) +
                        (collider.normal.z * direction.z);
    if (Abs(denom) <= kRayEpsilon) return false;
    const float numer = (collider.normal.x * (plane_point.x - origin.x)) +
                        (collider.normal.y * (plane_point.y - origin.y)) +
                        (collider.normal.z * (plane_point.z - origin.z));
    distance = numer / denom;
    if (distance < 0.0f || distance > max_distance) return false;

    const Vec3 point = AddScaled(origin, direction, distance);
    if (point.x < collider.bounds.min.x || point.x > collider.bounds.max.x ||
        point.z < collider.bounds.min.z || point.z > collider.bounds.max.z) {
        return false;
    }
    // Slopes additionally guard with the Y bounds so a tall vertical plane
    // does not absorb rays well above or below the actual face.
    if (collider.has_plane_origin &&
        (point.y < collider.bounds.min.y - kRayEpsilon ||
         point.y > collider.bounds.max.y + kRayEpsilon)) {
        return false;
    }

    normal = denom < 0.0f
        ? collider.normal
        : Vec3{-collider.normal.x, -collider.normal.y, -collider.normal.z};
    return true;
}

bool RaycastBox(const Collider& collider, const Vec3& origin, const Vec3& direction, float max_distance,
                float& distance, Vec3& normal) {
    float near_time = 0.0f;
    float far_time = max_distance;
    Vec3 near_normal;

    const float origins[3] = {origin.x, origin.y, origin.z};
    const float directions[3] = {direction.x, direction.y, direction.z};
    const float mins[3] = {collider.bounds.min.x, collider.bounds.min.y, collider.bounds.min.z};
    const float maxs[3] = {collider.bounds.max.x, collider.bounds.max.y, collider.bounds.max.z};
    const Vec3 min_normals[3] = {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    const Vec3 max_normals[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    for (int axis = 0; axis < 3; ++axis) {
        if (Abs(directions[axis]) <= kRayEpsilon) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) return false;
            continue;
        }

        float axis_near = (mins[axis] - origins[axis]) / directions[axis];
        float axis_far = (maxs[axis] - origins[axis]) / directions[axis];
        Vec3 axis_normal = min_normals[axis];
        if (axis_near > axis_far) {
            const float swap_time = axis_near;
            axis_near = axis_far;
            axis_far = swap_time;
            axis_normal = max_normals[axis];
        }

        if (axis_near > near_time) {
            near_time = axis_near;
            near_normal = axis_normal;
        }
        if (axis_far < far_time) far_time = axis_far;
        if (near_time > far_time) return false;
    }

    if (near_time < 0.0f || near_time > max_distance) return false;
    distance = near_time;
    normal = near_normal;
    return true;
}

bool IsCloser(float candidate_dist, int candidate_face_id, const GroundHit& best) {
    if (!best.hit) return true;
    if (candidate_dist < best.distance - kRayEpsilon) return true;
    return Abs(candidate_dist - best.distance) <= kRayEpsilon && candidate_face_id < best.face_id;
}

// ---------------------------------------------------------------------------
// Mesh-backed query helpers (CollMesh path)
// ---------------------------------------------------------------------------

// Prevent the MIPS -Os/-ffast-math optimizer from inlining this function and
// corrupting the GroundHit return value; a noinline here made the difference
// between the player falling through the floor on boot and snapping correctly.
__attribute__((noinline))
static GroundHit RaycastRoomMesh(const Room& room, const physics::CollMesh& mesh,
                                  const Vec3& origin, const Vec3& direction,
                                  float max_distance, BackfacePolicy backface) {
    using namespace physics;
    RayHit h;
    uint32_t nodes = 0;
    uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
    // Always use Ignore so inverted-winding triangles are found; apply the same
    // dot-product filter as legacy RaycastRoomSource to respect BackfacePolicy.
    h = RaycastMesh(mesh, origin, direction, max_distance, BackfaceCull::Ignore, nodes_ptr);
    if (room.query_counters) {
        ++room.query_counters->raycasts;
        room.query_counters->bvh_nodes_touched += nodes;
    }
    if (!h.hit) return GroundHit{};
    if (backface == BackfacePolicy::Ignore) {
        const float dot = h.normal.x*direction.x + h.normal.y*direction.y + h.normal.z*direction.z;
        if (dot >= 0.0f) return GroundHit{};
    }
    OwnerInfo owner = ResolveOwner(mesh, room, h.face_id);
    return GroundHit{
        .hit = true,
        .point = h.point,
        .normal = h.normal,
        .distance = h.t * max_distance,  // h.t is fraction [0,1]; convert to world units
        .face_id = h.face_id,
        .owner_id = owner.id,
        .owner_velocity = owner.velocity,
    };
}

static int QueryWallsMesh(const Room& room, const physics::CollMesh& mesh,
                           const Vec3& point, float radius,
                           WallHit* out_hits, int max_hits) {
    using namespace physics;
    physics::AABB q = {
        { point.x - radius, point.y - radius, point.z - radius },
        { point.x + radius, point.y + radius, point.z + radius },
    };
    int candidates[256];
    uint32_t nodes = 0;
    uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
    int n = OverlapAabbMesh(mesh, q, candidates, 256, nodes_ptr);
    if (room.query_counters) {
        ++room.query_counters->overlap_queries;
        room.query_counters->bvh_nodes_touched += nodes;
    }

    int count = 0;
    for (int i = 0; i < n && count < max_hits; ++i) {
        int fid = candidates[i];
        const CollTriangle& ct = mesh.triangles[fid];
        if (!(ct.material & MAT_SOLID)) continue;

        const Vec3& a = mesh.world_verts[ct.i0];
        const Vec3& b = mesh.world_verts[ct.i1];
        const Vec3& c = mesh.world_verts[ct.i2];

        SphereTriHit sh = SphereTriangle(point, radius, a, b, c);
        if (!sh.hit) continue;

        const float dx = point.x - sh.closest.x;
        const float dy = point.y - sh.closest.y;
        const float dz = point.z - sh.closest.z;
        const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (len < 1e-7f) continue;

        const Vec3 normal = { dx/len, dy/len, dz/len };
        if (normal.y >= 0.85f || normal.y <= -0.85f) continue;

        // Owning triangle's plane, oriented toward the query centre. For
        // face-region contacts this is the true face normal; near seams/edges
        // it stays a real adjacent-face plane instead of the diagonal
        // centre-relative direction that made fall speed bleed away.
        Vec3 pn = physics::Norm(physics::Cross(
            physics::Sub(b, a), physics::Sub(c, a)));
        if ((pn.x*dx + pn.y*dy + pn.z*dz) < 0.0f) {
            pn.x = -pn.x; pn.y = -pn.y; pn.z = -pn.z;
        }

        const float pushout = radius - sh.dist;
        if (pushout < 0.0f) continue;

        OwnerInfo owner = ResolveOwner(mesh, room, fid);
        out_hits[count++] = WallHit{
            .hit = true,
            .point = sh.closest,
            .normal = normal,
            .plane_normal = pn,
            .pushout = pushout,
            .face_id = fid,
            .owner_id = owner.id,
            .owner_velocity = owner.velocity,
        };
    }
    return count;
}

// Distance from `p` to the infinite plane through (a,b,c), signed against the
// triangle's geometric normal oriented toward `p` (the sign is resolved by the
// caller picking the face orientation toward the query point, matching the
// existing `QueryWallsMesh` sign-flip). Returns an unsigned distance.
static Vec3 PlaneProject(
    const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& p)
{
    using namespace physics;
    const Vec3 n = physics::Norm(physics::Cross(physics::Sub(b, a), physics::Sub(c, a)));
    // Signed distance from p to the plane.
    const float d = physics::Dot(n, physics::Sub(p, a));
    // Closest point on the plane to p.
    return { p.x - n.x * d, p.y - n.y * d, p.z - n.z * d };
}

// OG SolidWallCheck parity (plane-based). For each candidate triangle that is a
// near-vertical face (|plane_normal.y| < 0.35), compute the face plane, project
// the query point onto it at the query's own level, and record the face-plane
// normal as the stable contact normal. Unlike the sphere query, this does NOT
// return a centre-relative diagonal normal at seams/edges.
static int QueryWallsFacePlaneMesh(const Room& room, const physics::CollMesh& mesh,
                                    const Vec3& point, float radius,
                                    WallHit* out_hits, int max_hits,
                                    CollisionQueryDiagnostics* diagnostics) {
    using namespace physics;
    physics::AABB q = {
        { point.x - radius, point.y - radius, point.z - radius },
        { point.x + radius, point.y + radius, point.z + radius },
    };
    int candidates[256];
    uint32_t nodes = 0;
    uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
    int n = OverlapAabbMesh(mesh, q, candidates, 256, nodes_ptr);
    if (room.query_counters) {
        ++room.query_counters->overlap_queries;
        room.query_counters->bvh_nodes_touched += nodes;
    }

    int count = 0;
    for (int i = 0; i < n && count < max_hits; ++i) {
        int fid = candidates[i];
        const CollTriangle& ct = mesh.triangles[fid];
        if (!(ct.material & MAT_SOLID)) continue;

        const Vec3& a = mesh.world_verts[ct.i0];
        const Vec3& b = mesh.world_verts[ct.i1];
        const Vec3& c = mesh.world_verts[ct.i2];

        // Face plane normal, oriented toward the query point (baked winding can
        // be reversed — see .agents/common-mistakes/og-map-polygon-winding.md).
        Vec3 pn = physics::Norm(physics::Cross(physics::Sub(b, a), physics::Sub(c, a)));
        const Vec3 to_p = physics::Sub(point, a);
        if (physics::Dot(pn, to_p) < 0.0f) {
            pn = {-pn.x, -pn.y, -pn.z};
        }

        // OG ClimbNormalCheck: only near-vertical faces are walls. |Y| < 0.35.
        if (std::fabs(pn.y) >= 0.35f) continue;

        // Closest point on the face plane to the query point, at the query's own
        // level. pushout = radius - plane distance. Accept pushout >= 0 (the
        // tangent boundary still counts as touching, matching OG which accepts
        // dist < radius and anchors the player into the wall).
        const Vec3 closest = PlaneProject(a, b, c, point);
        const Vec3 delta = physics::Sub(point, closest);
        const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const float pushout = radius - dist;
        if (pushout < 0.0f) continue;

        if (diagnostics) ++diagnostics->wall_candidate_hits;
        OwnerInfo owner = ResolveOwner(mesh, room, fid);
        out_hits[count++] = WallHit{
            .hit = true,
            .point = closest,
            .normal = pn,
            .plane_normal = pn,
            .pushout = pushout,
            .face_id = fid,
            .owner_id = owner.id,
            .owner_velocity = owner.velocity,
        };
    }
    return count;
}

}  // namespace (close anon ns, reopen for AABB methods)

// --- AABB methods ---

bool AABB::Contains(const Vec3& point) const {
    return point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y &&
           point.z >= min.z && point.z <= max.z;
}

bool AABB::Intersects(const AABB& other) const {
    return min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
}

bool AABB::IntersectsXZ(const Vec3& point) const {
    return point.x >= min.x && point.x <= max.x &&
           point.z >= min.z && point.z <= max.z;
}

// Prevent MIPS -Os/-ffast-math optimizer from inlining and corrupting the
// GroundHit struct return value (world.cpp:133-134).
__attribute__((noinline))
GroundHit RaycastRoomSource(const Room& room, const Vec3& origin, const Vec3& direction, float max_distance, BackfacePolicy backface) {
    // Static world geometry: always use collmesh.
    GroundHit best;
    if (room.coll_mesh) {
        best = RaycastRoomMesh(room, *room.coll_mesh, origin, direction, max_distance, backface);
    }

    // Dynamic colliders (moving platforms): raycast against collider array.
    if (max_distance < 0.0f) return best;

    for (int i = 0; i < room.collider_count; ++i) {
        const Collider& collider = room.colliders[i];
        if (!collider.solid) continue;
        if (room.coll_mesh && IsStaticCollider(collider)) continue;

        float distance = 0.0f;
        Vec3 normal;
        bool hit = false;
        if (collider.type == ColliderType::Plane) {
            hit = RaycastPlane(collider, origin, direction, max_distance, distance, normal);
        } else if (collider.type == ColliderType::Box) {
            hit = RaycastBox(collider, origin, direction, max_distance, distance, normal);
        }
        if (!hit) continue;

        if (backface == BackfacePolicy::Ignore) {
            const float dot = (normal.x * direction.x) + (normal.y * direction.y) + (normal.z * direction.z);
            if (dot >= 0.0f) continue;
        }

        const int face_id = collider.face_id >= 0 ? collider.face_id : i;
        if (IsCloser(distance, face_id, best)) {
            const MovingSurface* surface = FindMovingSurface(room, collider.owner_id);
            const Vec3 owner_velocity = surface != nullptr ? surface->rider_velocity : collider.velocity;
            best = {
                .hit = true,
                .point = AddScaled(origin, direction, distance),
                .normal = normal,
                .distance = distance,
                .face_id = face_id,
                .owner_id = collider.owner_id,
                .owner_velocity = owner_velocity,
            };
        }
    }

    return best;
}

// Prevent MIPS -Os/-ffast-math optimizer from inlining and corrupting the
// GroundHit struct return value (world.cpp:133-134).
__attribute__((noinline))
GroundHit QueryFloorSource(const Room& room, const Vec3& origin, float max_distance) {
    return RaycastRoomSource(room, origin, {0.0f, -1.0f, 0.0f}, max_distance, BackfacePolicy::Ignore);
}

// Prevent MIPS -Os/-ffast-math optimizer from inlining and corrupting the
// GroundHit struct return value (world.cpp:133-134).
__attribute__((noinline))
CeilingHit QueryCeilingSource(const Room& room, const Vec3& origin, float max_distance) {
    GroundHit ground = RaycastRoomSource(room, origin, {0.0f, 1.0f, 0.0f}, max_distance, BackfacePolicy::Ignore);
    if (!ground.hit) return CeilingHit{};
    return CeilingHit{
        .hit = true,
        .point = ground.point,
        .normal = ground.normal,
        .distance = ground.distance,
        .face_id = ground.face_id,
        .owner_id = ground.owner_id,
        .owner_velocity = ground.owner_velocity,
    };
}

// Prevent MIPS -Os/-ffast-math optimizer from inlining and corrupting the
// GroundHit struct return value (world.cpp:133-134).
__attribute__((noinline))
GroundHit ProbeFloorDebug(const Room& room, const Vec3& position, float half_height,
                          float probe_distance, float radius,
                          CollisionQueryDiagnostics* diagnostics) {
    const Vec3 feet = {position.x, position.y - half_height, position.z};
    const float s = radius * 0.70710678f;
    const Vec3 offsets[9] = {
        {0.0f, 0.0f, 0.0f},
        { radius, 0.0f, 0.0f},
        {-radius, 0.0f, 0.0f},
        {0.0f, 0.0f,  radius},
        {0.0f, 0.0f, -radius},
        { s, 0.0f,  s},
        {-s, 0.0f,  s},
        { s, 0.0f, -s},
        {-s, 0.0f, -s},
    };

    GroundHit best;
    for (const Vec3& offset : offsets) {
        if (diagnostics) ++diagnostics->floor_sweep_hits;
        const Vec3 origin = {feet.x + offset.x, feet.y, feet.z + offset.z};
        GroundHit hit = QueryFloorSource(room, origin, probe_distance);
        if (!hit.hit) continue;
        if (diagnostics) ++diagnostics->floor_raycast_hits;
        if (!best.hit ||
            hit.distance < best.distance - kRayEpsilon ||
            (std::fabs(hit.distance - best.distance) <= kRayEpsilon && hit.face_id < best.face_id)) {
            best = hit;
        }
    }

    return best;
}

int QueryWalls(const Room& room, const Vec3& point, float radius, WallHit* out_hits, int max_hits,
               CollisionQueryDiagnostics* diagnostics) {
    int count = 0;

    // Static world geometry: always use collmesh.
    if (room.coll_mesh) {
        const float slice_offsets[3] = {
            -radius * 0.5f,
            0.0f,
            radius * 0.5f,
        };
        for (float slice_offset : slice_offsets) {
            WallHit slice_hits[kMaxWallHits];
            const int slice_count = QueryWallsMesh(room, *room.coll_mesh,
                                                   {point.x, point.y + slice_offset, point.z},
                                                   radius, slice_hits, kMaxWallHits);
            for (int i = 0; i < slice_count && count < max_hits; ++i) {
                bool duplicate = false;
                for (int j = 0; j < count; ++j) {
                    if (out_hits[j].face_id == slice_hits[i].face_id) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                out_hits[count++] = slice_hits[i];
                if (diagnostics) ++diagnostics->wall_candidate_hits;
            }
        }
        if (count >= max_hits) return count;
    }

    // Dynamic colliders (moving platforms): check collider array.
    const float radius_sq = radius * radius;

    for (int i = 0; i < room.collider_count && count < max_hits; ++i) {
        const Collider& c = room.colliders[i];
        if (!c.solid) continue;
        if (room.coll_mesh && c.owner_id == kStaticOwner) continue;

        // Skip floor/ceiling planes; for Box colliders the per-face normal is
        // derived from the closest-point direction below, so the per-collider
        // hint cannot reject the whole box.
        if (c.type == ColliderType::Plane &&
            (c.normal.y <= -0.999f || c.normal.y >= 0.999f)) {
            continue;
        }

        AABB query_bounds;
        query_bounds.min = {point.x - radius, point.y - radius, point.z - radius};
        query_bounds.max = {point.x + radius, point.y + radius, point.z + radius};

        if (c.type == ColliderType::Box) {
            // Interior overlap: pick the shallowest horizontal face to push out
            // along. Vertical faces are floor/ceiling and excluded from walls.
            const bool inside = c.bounds.Contains(point);
            const MovingSurface* surface = FindMovingSurface(room, c.owner_id);
            const Vec3 owner_velocity = surface != nullptr ? surface->rider_velocity : c.velocity;

            if (inside) {
                const float face_depths[4] = {
                    point.x - c.bounds.min.x,  // pushed left -> normal -X
                    c.bounds.max.x - point.x,  // pushed right -> normal +X
                    point.z - c.bounds.min.z,  // pushed -Z
                    c.bounds.max.z - point.z,  // pushed +Z
                };
                const Vec3 face_normals[4] = {
                    {-1.0f, 0.0f, 0.0f},
                    { 1.0f, 0.0f, 0.0f},
                    { 0.0f, 0.0f, -1.0f},
                    { 0.0f, 0.0f,  1.0f},
                };
                int best_axis = 0;
                for (int k = 1; k < 4; ++k) {
                    if (face_depths[k] < face_depths[best_axis]) best_axis = k;
                }

                WallHit hit;
                hit.hit = true;
                hit.point = point;
                hit.normal = face_normals[best_axis];
                hit.plane_normal = face_normals[best_axis];  // box faces are planes
                hit.pushout = face_depths[best_axis] + radius;
                hit.face_id = c.face_id >= 0 ? c.face_id : i;
                hit.owner_id = c.owner_id;
                hit.owner_velocity = owner_velocity;
                out_hits[count++] = hit;
                if (diagnostics) ++diagnostics->wall_candidate_hits;
                continue;
            }

            const float closest_x = std::max(c.bounds.min.x, std::min(point.x, c.bounds.max.x));
            const float closest_y = std::max(c.bounds.min.y, std::min(point.y, c.bounds.max.y));
            const float closest_z = std::max(c.bounds.min.z, std::min(point.z, c.bounds.max.z));

            const float dx = point.x - closest_x;
            const float dy = point.y - closest_y;
            const float dz = point.z - closest_z;
            const float dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq > radius_sq || dist_sq < kRayEpsilon) continue;

            const float dist = std::sqrt(dist_sq);
            const float pushout = radius - dist;

            const Vec3 normal = {dx / dist, dy / dist, dz / dist};
            // Reject floor/ceiling-shaped contacts; those are handled by
            // ground/ceiling queries, not the wall popout path.
            if (normal.y >= 0.85f || normal.y <= -0.85f) continue;

            WallHit hit;
            hit.hit = true;
            hit.point = {closest_x, closest_y, closest_z};
            hit.normal = normal;
            {   // Exterior box contact: the contact plane is the dominant
                // horizontal axis face (y-dominant cases are rejected above).
                Vec3 pn = {0.0f, 0.0f, 0.0f};
                const float ax = std::fabs(dx), ay = std::fabs(dy), az = std::fabs(dz);
                if (ax >= ay && ax >= az)      pn = {dx > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
                else if (az >= ay)             pn = {0.0f, 0.0f, dz > 0.0f ? 1.0f : -1.0f};
                else                           pn = {0.0f, dy > 0.0f ? 1.0f : -1.0f, 0.0f};
                hit.plane_normal = pn;
            }
            hit.pushout = pushout;
            hit.face_id = c.face_id >= 0 ? c.face_id : i;
            hit.owner_id = c.owner_id;
            hit.owner_velocity = owner_velocity;
            out_hits[count++] = hit;
            if (diagnostics) ++diagnostics->wall_candidate_hits;
        }
    }

    return count;
}

WallHit QueryWallNearest(const Room& room, const Vec3& point, float radius) {
    WallHit hits[kMaxWallHits];
    const int count = QueryWalls(room, point, radius, hits, kMaxWallHits);
    if (count == 0) return WallHit{};

    WallHit best = hits[0];
    for (int i = 1; i < count; ++i) {
        if (hits[i].pushout > best.pushout) {
            best = hits[i];
        }
    }
    return best;
}

bool QueryWallPresent(const Room& room, const Vec3& point, float radius) {
    WallHit hits[kMaxWallHits];
    return QueryWalls(room, point, radius, hits, kMaxWallHits) > 0;
}

WallHit QueryWallClosestToNormal(const Room& room, const Vec3& point, float radius, const Vec3& normal) {
    WallHit hits[kMaxWallHits];
    const int count = QueryWalls(room, point, radius, hits, kMaxWallHits);
    if (count == 0) return WallHit{};

    WallHit best = hits[0];
    float best_dot = (best.normal.x * normal.x) + (best.normal.y * normal.y) + (best.normal.z * normal.z);
    for (int i = 1; i < count; ++i) {
        const float dot = (hits[i].normal.x * normal.x) + (hits[i].normal.y * normal.y) + (hits[i].normal.z * normal.z);
        if (dot > best_dot) {
            best = hits[i];
            best_dot = dot;
        }
    }
    return best;
}

int QueryWallsFacePlane(const Room& room, const Vec3& point, float radius,
                        WallHit* out_hits, int max_hits,
                        CollisionQueryDiagnostics* diagnostics) {
    int count = 0;
    // Static world geometry: plane-projected wall query (OG parity). This is
    // the source of truth for climb grip identity and wall contact.
    if (room.coll_mesh) {
        count = QueryWallsFacePlaneMesh(room, *room.coll_mesh, point, radius,
                                        out_hits, max_hits, diagnostics);
        if (count >= max_hits) return count;
    }

    // Dynamic colliders (moving platforms): fall back to the sphere path so
    // riders on moving boxes still resolve wall contact.
    if (room.collider_count > 0) {
        WallHit sphere_hits[kMaxWallHits];
        const int sphere_count = QueryWalls(room, point, radius, sphere_hits, kMaxWallHits);
        for (int i = 0; i < sphere_count && count < max_hits; ++i) {
            bool duplicate = false;
            for (int j = 0; j < count; ++j) {
                if (out_hits[j].face_id == sphere_hits[i].face_id) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            out_hits[count++] = sphere_hits[i];
            if (diagnostics) ++diagnostics->wall_candidate_hits;
        }
    }
    return count;
}

WallHit QueryWallClosestToFace(const Room& room, const Vec3& point, float radius, const Vec3& normal) {
    WallHit hits[kMaxWallHits];
    const int count = QueryWallsFacePlane(room, point, radius, hits, kMaxWallHits);
    if (count == 0) return WallHit{};

    WallHit best = hits[0];
    float best_dot = (best.plane_normal.x * normal.x) + (best.plane_normal.y * normal.y) + (best.plane_normal.z * normal.z);
    for (int i = 1; i < count; ++i) {
        const float dot = (hits[i].plane_normal.x * normal.x) + (hits[i].plane_normal.y * normal.y) + (hits[i].plane_normal.z * normal.z);
        if (dot > best_dot) {
            best = hits[i];
            best_dot = dot;
        }
    }
    return best;
}

// ── Unified climb wall queries (climb-parity-3 Inc 1) ───────────────

// OG ClimbNormalCheck (Player.cs ClimbCheckAt gate): a face is climbable when
// its normal is near-vertical, |n.y| < 0.35.
static bool ClimbNormalCheck(const Vec3& n) {
    return std::fabs(n.y) < 0.35f;
}

WallHit QueryClimbWall(const Room& room, const Vec3& point, float radius, const Vec3& prefer_dir) {
    WallHit hits[kMaxWallHits];
    const int count = QueryWallsFacePlane(room, point, radius, hits, kMaxWallHits);
    if (count == 0) return WallHit{};

    // Prefer-rank like OG SolidWallCheckClosestToNormal (World.cs:577-594):
    // the prefer direction only RANKS candidates, it does NOT gate them.
    WallHit best = hits[0];
    float best_dot = (best.plane_normal.x * prefer_dir.x) +
                     (best.plane_normal.y * prefer_dir.y) +
                     (best.plane_normal.z * prefer_dir.z);
    for (int i = 1; i < count; ++i) {
        const float dot = (hits[i].plane_normal.x * prefer_dir.x) +
                          (hits[i].plane_normal.y * prefer_dir.y) +
                          (hits[i].plane_normal.z * prefer_dir.z);
        if (dot > best_dot) {
            best = hits[i];
            best_dot = dot;
        }
    }
    // QueryWallsFacePlaneMesh already gates |pn.y| < 0.35 for every returned
    // hit; re-check so the wrapper's contract holds even if the inner query
    // ever loosens (OG ClimbNormalCheck parity on the returned NORMAL).
    if (!ClimbNormalCheck(best.plane_normal)) return WallHit{};
    return best;
}

bool QueryClimbWallPresent(const Room& room, const Vec3& point, float radius, const Vec3& prefer_dir) {
    return QueryClimbWall(room, point, radius, prefer_dir).hit;
}

bool QueryWallRay(const Room& room, const Vec3& origin, const Vec3& direction,
                  float max_distance, WallHit* out_hit) {
    if (!room.coll_mesh) {
        // Dynamic-collider rooms (no static mesh) are not supported for climb
        // corner rays — OG corners run on static Solids/meshes.
        return false;
    }
    using namespace physics;
    // BackfaceCull::Ignore: inverted-winding baked faces must be found
    // (see .agents/common-mistakes/og-map-polygon-winding.md); the orientation
    // check below supplies the cull discrimination instead.
    uint32_t nodes = 0;
    const RayHit h = RaycastMesh(*room.coll_mesh, origin, direction, max_distance,
                                 BackfaceCull::Ignore, room.query_counters ? &nodes : nullptr);
    if (room.query_counters) {
        ++room.query_counters->raycasts;
        room.query_counters->bvh_nodes_touched += nodes;
    }
    if (!h.hit) return false;

    // Face-plane normal oriented toward the ray origin (baked winding can be
    // reversed — same fix as QueryWallsMesh/QueryWallsFacePlaneMesh).
    Vec3 pn;
    {
        const CollTriangle& ct = room.coll_mesh->triangles[h.face_id];
        const Vec3& a = room.coll_mesh->world_verts[ct.i0];
        Vec3 vn = Norm(Cross(Sub(room.coll_mesh->world_verts[ct.i1], a),
                             Sub(room.coll_mesh->world_verts[ct.i2], a)));
        const Vec3 to_origin = Sub(origin, a);
        if (Dot(vn, to_origin) < 0.0f) {
            vn = {-vn.x, -vn.y, -vn.z};
        }
        pn = vn;
    }

    // OG ClimbNormalCheck on the ray hit: slopes are not walls.
    if (!ClimbNormalCheck(pn)) return false;

    // Orientation: the hit must face the ray origin side (skip backfaces the
    // Ignore policy let through, i.e. leave outward geometry).
    if ((pn.x * direction.x) + (pn.y * direction.y) + (pn.z * direction.z) >= 0.0f) return false;

    if (out_hit) {
        const OwnerInfo owner = ResolveOwner(*room.coll_mesh, room, h.face_id);
        *out_hit = WallHit{
            .hit = true,
            .point = h.point,
            .normal = pn,
            .plane_normal = pn,
            .pushout = 0.0f,  // rays do not carry penetration depth
            .face_id = h.face_id,
            .owner_id = owner.id,
            .owner_velocity = owner.velocity,
        };
    }
    return true;
}

const MovingSurface* FindMovingSurface(const Room& room, int owner_id) {
    if (owner_id < 0) return nullptr;
    for (int i = 0; i < room.moving_surface_count; ++i) {
        if (room.moving_surfaces[i].id == owner_id) {
            return &room.moving_surfaces[i];
        }
    }
    return nullptr;
}

MovingSurface* FindMovingSurfaceMutable(Room& room, int owner_id) {
    if (owner_id < 0) return nullptr;
    for (int i = 0; i < room.moving_surface_count; ++i) {
        if (room.moving_surfaces[i].id == owner_id) {
            return &room.moving_surfaces[i];
        }
    }
    return nullptr;
}

void AdvanceMovingSurfaces(Room& room, float delta_seconds) {
    for (int s = 0; s < room.moving_surface_count; ++s) {
        MovingSurface& surface = room.moving_surfaces[s];
        const Vec3 delta = {
            surface.position.x - surface.last_position.x,
            surface.position.y - surface.last_position.y,
            surface.position.z - surface.last_position.z,
        };
        surface.displacement = delta;
        if (delta_seconds > 0.0f) {
            surface.rider_velocity = {delta.x / delta_seconds, delta.y / delta_seconds, delta.z / delta_seconds};
        }
        surface.last_position = surface.position;

        for (int i = 0; i < room.collider_count; ++i) {
            Collider& c = room.colliders[i];
            if (c.owner_id != surface.id) continue;
            c.bounds.min.x += delta.x;
            c.bounds.max.x += delta.x;
            c.bounds.min.y += delta.y;
            c.bounds.max.y += delta.y;
            c.bounds.min.z += delta.z;
            c.bounds.max.z += delta.z;
            c.velocity = surface.rider_velocity;
        }
    }
}

// ── WorldCollision (Inc 6) ─────────────────────────────────────────

WorldCollision::~WorldCollision() {
    Reset();
}

bool WorldCollision::Load(const char* path) {
    Reset();
    mesh_ = physics::LoadCollMesh(path);
    if (!mesh_) {
        return false;
    }
    return true;
}

void WorldCollision::Reset() {
    if (mesh_) {
        physics::FreeCollMesh(mesh_);
        mesh_ = nullptr;
    }
}

static Room MakeCounterRoom(const physics::CollMesh* mesh, CollisionQueryCounters* counters) {
    Room r{};
    r.coll_mesh = const_cast<physics::CollMesh*>(mesh);
    r.query_counters = counters;
    return r;
}

GroundHit WorldCollision::QueryFloor(const Vec3& origin, float max_distance) const {
    if (!mesh_) return GroundHit{};
    Room r = MakeCounterRoom(mesh_, const_cast<CollisionQueryCounters*>(&counters_));
    return RaycastRoomMesh(r, *mesh_, origin, {0.0f, -1.0f, 0.0f},
                           max_distance, BackfacePolicy::Ignore);
}

CeilingHit WorldCollision::QueryCeiling(const Vec3& origin, float max_distance) const {
    if (!mesh_) return CeilingHit{};
    Room r = MakeCounterRoom(mesh_, const_cast<CollisionQueryCounters*>(&counters_));
    GroundHit ground = RaycastRoomMesh(r, *mesh_, origin, {0.0f, 1.0f, 0.0f},
                                       max_distance, BackfacePolicy::Ignore);
    if (!ground.hit) return CeilingHit{};
    return CeilingHit{
        .hit = true,
        .point = ground.point,
        .normal = ground.normal,
        .distance = ground.distance,
        .face_id = ground.face_id,
        .owner_id = ground.owner_id,
        .owner_velocity = ground.owner_velocity,
    };
}

int WorldCollision::QueryWalls(const Vec3& point, float radius,
                               WallHit* out_hits, int max_hits) const {
    if (!mesh_) return 0;
    Room r = MakeCounterRoom(mesh_, const_cast<CollisionQueryCounters*>(&counters_));
    return QueryWallsMesh(r, *mesh_, point, radius, out_hits, max_hits);
}

WallHit WorldCollision::QueryWallNearest(const Vec3& point, float radius) const {
    WallHit hits[kMaxWallHits];
    const int count = QueryWalls(point, radius, hits, kMaxWallHits);
    if (count == 0) return WallHit{};
    WallHit best = hits[0];
    for (int i = 1; i < count; ++i) {
        if (hits[i].pushout > best.pushout) best = hits[i];
    }
    return best;
}

}  // namespace madeline_cube
