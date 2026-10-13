#include "gameplay/world/world.hpp"

#include <cmath>
#include <algorithm>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"

namespace madeline_cube {

namespace {

constexpr float kRayEpsilon = 0.0001f;
constexpr int kOgGridCellSize = 200;
constexpr int kOgGridCount = 100;
// Sentinel: colliders added at runtime (moving platforms) have owner_id >= 0.
// Static colliders from the legacy path have owner_id == -1 and are skipped
// when a CollMesh is present (CollMesh handles all static geometry).
constexpr int kStaticOwner = -1;

struct OwnerInfo { int id; Vec3 velocity; };

struct WallQueryOrder {
    bool has_og_solid = false;
    uint32_t query_cell = 0xFFFFFFFFu;
    uint32_t insertion_ordinal = 0xFFFFFFFFu;
    uint16_t source_face_id = 0xFFFFu;
    int face_id = 0x7FFFFFFF;
};

struct SourceRayQueryBounds {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
};

struct SourceRayOrder {
    bool has_og_solid = false;
    uint32_t query_cell = 0xFFFFFFFFu;
    uint32_t insertion_ordinal = 0xFFFFFFFFu;
    uint16_t source_face_id = 0;
    int face_id = -1;
};

int PositiveGridModulo(int value) {
    const int remainder = value % kOgGridCount;
    return remainder < 0 ? remainder + kOgGridCount : remainder;
}

bool GridRangeContainsWrappedCell(int start, int end, int cell) {
    const int width = end - start;
    return width >= kOgGridCount ||
        (width > 0 && PositiveGridModulo(cell - start) < width);
}

bool TryOgRayOrder(float solid_min_x, float solid_min_y,
                   float solid_max_x, float solid_max_y,
                   uint32_t insertion_ordinal, uint16_t source_face_id,
                   const SourceRayQueryBounds& query,
                   SourceRayOrder& order) {
    const int query_left = static_cast<int>(std::floor(
        (query.min_x - 1.0f) / kOgGridCellSize));
    const int query_top = static_cast<int>(std::floor(
        (query.min_y - 1.0f) / kOgGridCellSize));
    const int query_right = static_cast<int>(std::ceil(
        (query.max_x + 1.0f) / kOgGridCellSize));
    const int query_bottom = static_cast<int>(std::ceil(
        (query.max_y + 1.0f) / kOgGridCellSize));
    const int query_height = query_bottom - query_top;
    if (query_right <= query_left || query_height <= 0) return false;

    const int solid_left = static_cast<int>(std::floor(
        (solid_min_x - 1.0f) / kOgGridCellSize));
    const int solid_top = static_cast<int>(std::floor(
        (solid_min_y - 1.0f) / kOgGridCellSize));
    const int solid_right = static_cast<int>(std::ceil(
        (solid_max_x + 1.0f) / kOgGridCellSize));
    const int solid_bottom = static_cast<int>(std::ceil(
        (solid_max_y + 1.0f) / kOgGridCellSize));
    for (int x = query_left; x < query_right; ++x) {
        if (!GridRangeContainsWrappedCell(solid_left, solid_right, x)) continue;
        for (int y = query_top; y < query_bottom; ++y) {
            if (!GridRangeContainsWrappedCell(solid_top, solid_bottom, y)) continue;
            order = {
                .has_og_solid = true,
                .query_cell = static_cast<uint32_t>(x - query_left) *
                    static_cast<uint32_t>(query_height) +
                    static_cast<uint32_t>(y - query_top),
                .insertion_ordinal = insertion_ordinal,
                .source_face_id = source_face_id,
                .face_id = -1,
            };
            return true;
        }
    }
    return false;
}

SourceRayQueryBounds MakeSourceRayQueryBounds(const Vec3& origin,
                                               const Vec3& end) {
    return {
        .min_x = std::fmin(origin.x, end.x),
        .min_y = -std::fmax(origin.z, end.z),
        .max_x = std::fmax(origin.x, end.x),
        .max_y = -std::fmin(origin.z, end.z),
    };
}

SourceRayOrder MakeStaticRayOrder(const physics::CollMesh& mesh, int face_id,
                                 const SourceRayQueryBounds& query) {
    SourceRayOrder order{};
    if (!mesh.header || !mesh.triangles || face_id < 0 ||
        static_cast<uint32_t>(face_id) >= mesh.header->triangle_count) return order;
    const physics::CollTriangle& triangle = mesh.triangles[face_id];
    order.source_face_id = triangle.source_face_id;
    order.face_id = face_id;
    const physics::CollSolidGridRecord* solid =
        physics::SolidGridRecordOf(mesh, face_id);
    if (solid && TryOgRayOrder(solid->min_x, solid->min_y,
                               solid->max_x, solid->max_y,
                               solid->solid_id, triangle.source_face_id,
                               query, order)) {
        order.face_id = face_id;
    }
    return order;
}

SourceRayOrder MakeDynamicRayOrder(const DynamicSolidView& view,
                                   uint16_t source_face_id, int face_id,
                                   const SourceRayQueryBounds& query) {
    SourceRayOrder order{};
    order.source_face_id = source_face_id;
    order.face_id = face_id;
    if (TryOgRayOrder(view.bounds.min.x, -view.bounds.max.z,
                      view.bounds.max.x, -view.bounds.min.z,
                      view.reinsert_ordinal, source_face_id, query, order)) {
        order.face_id = face_id;
    }
    return order;
}

bool SourceRayOrderIsLater(const SourceRayOrder& candidate,
                           const SourceRayOrder& best) {
    if (candidate.has_og_solid != best.has_og_solid)
        return candidate.has_og_solid;
    if (candidate.has_og_solid) {
        if (candidate.query_cell != best.query_cell)
            return candidate.query_cell > best.query_cell;
        if (candidate.insertion_ordinal != best.insertion_ordinal)
            return candidate.insertion_ordinal > best.insertion_ordinal;
        if (candidate.source_face_id != best.source_face_id)
            return candidate.source_face_id > best.source_face_id;
        // OG stops at first intersecting fan triangle within one authored face.
        // CMSH face order follows that fan order, independent of BVH traversal.
        return candidate.face_id < best.face_id;
    }
    if (candidate.source_face_id != best.source_face_id)
        return candidate.source_face_id > best.source_face_id;
    // Legacy CMSH has no authored-face provenance; preserve traversal order.
    return candidate.source_face_id == 0 && candidate.face_id > best.face_id;
}

bool SourceRayCandidateWins(float distance, const SourceRayOrder& order,
                            const GroundHit& best,
                            const SourceRayOrder& best_order) {
    if (!best.hit || distance < best.distance) return true;
    return distance == best.distance &&
           SourceRayOrderIsLater(order, best_order);
}

bool TryOgWallBoundsOrder(float min_x, float min_plane_y,
                          float max_x, float max_plane_y,
                          uint32_t insertion_ordinal, uint16_t source_face_id,
                          const Vec3& point, float radius,
                          WallQueryOrder& order) {
    // OG GridPartition.Cells inflates both query and inserted solid bounds by
    // one world unit, then floors minima / ceils maxima. Query loops X outer,
    // Y inner; its first shared wrapped cell determines solid encounter order.
    const double query_min_x = point.x - radius - 1.0;
    const double query_max_x = point.x + radius + 1.0;
    const double query_min_y = -point.z - radius - 1.0;
    const double query_max_y = -point.z + radius + 1.0;
    const int query_left = static_cast<int>(std::floor(query_min_x / kOgGridCellSize));
    const int query_top = static_cast<int>(std::floor(query_min_y / kOgGridCellSize));
    const int query_right = static_cast<int>(std::ceil(query_max_x / kOgGridCellSize));
    const int query_bottom = static_cast<int>(std::ceil(query_max_y / kOgGridCellSize));
    const int query_height = query_bottom - query_top;
    if (query_right <= query_left || query_height <= 0) return false;

    const int solid_left = static_cast<int>(std::floor((min_x - 1.0) / kOgGridCellSize));
    const int solid_top = static_cast<int>(std::floor((min_plane_y - 1.0) / kOgGridCellSize));
    const int solid_right = static_cast<int>(std::ceil((max_x + 1.0) / kOgGridCellSize));
    const int solid_bottom = static_cast<int>(std::ceil((max_plane_y + 1.0) / kOgGridCellSize));
    for (int x = query_left; x < query_right; ++x) {
        if (!GridRangeContainsWrappedCell(solid_left, solid_right, x)) continue;
        for (int y = query_top; y < query_bottom; ++y) {
            if (!GridRangeContainsWrappedCell(solid_top, solid_bottom, y)) continue;
            const uint32_t query_cell = static_cast<uint32_t>(x - query_left) *
                static_cast<uint32_t>(query_height) + static_cast<uint32_t>(y - query_top);
            order = {
                .has_og_solid = true,
                .query_cell = query_cell,
                .insertion_ordinal = insertion_ordinal,
                .source_face_id = source_face_id,
                .face_id = -1,
            };
            return true;
        }
    }
    return false;
}

bool TryOgWallQueryOrder(const physics::CollMesh& mesh, int face_id,
                         const Vec3& point, float radius,
                         WallQueryOrder& order) {
    const physics::CollSolidGridRecord* solid =
        physics::SolidGridRecordOf(mesh, face_id);
    if (!solid) return false;
    if (!TryOgWallBoundsOrder(solid->min_x, solid->min_y,
                              solid->max_x, solid->max_y,
                              solid->solid_id,
                              mesh.triangles[face_id].source_face_id,
                              point, radius, order)) return false;
    order.face_id = face_id;
    return true;
}

WallQueryOrder MakeWallQueryOrder(const physics::CollMesh& mesh, int face_id,
                                 const Vec3& point, float radius) {
    WallQueryOrder order;
    if (TryOgWallQueryOrder(mesh, face_id, point, radius, order)) return order;
    const uint16_t source_face_id = face_id >= 0 &&
        face_id < static_cast<int>(mesh.header->triangle_count)
        ? mesh.triangles[face_id].source_face_id : 0;
    // Unmodeled entity solids follow worldspawn chunks in OG. Their exact
    // actor transforms/order remain outside this static-world metadata slice.
    order.source_face_id = source_face_id != 0 ? source_face_id : 0xFFFFu;
    order.face_id = face_id;
    return order;
}

WallQueryOrder MakeRoomWallQueryOrder(const Room& room, int face_id,
                                      const Vec3& point, float radius) {
    if (room.dynamic_solids && room.coll_mesh) {
        const physics::CollSolidGridRecord* solid =
            physics::SolidGridRecordOf(*room.coll_mesh, face_id);
        const DynamicSolidView* view = solid
            ? room.dynamic_solids->FindViewBySolidId(solid->solid_id) : nullptr;
        if (view) {
            WallQueryOrder order;
            const uint16_t source_face_id =
                room.coll_mesh->triangles[face_id].source_face_id;
            if (TryOgWallBoundsOrder(view->bounds.min.x, -view->bounds.max.z,
                                     view->bounds.max.x, -view->bounds.min.z,
                                     view->reinsert_ordinal, source_face_id,
                                     point, radius, order)) {
                order.face_id = face_id;
                return order;
            }
        }
    }
    return MakeWallQueryOrder(*room.coll_mesh, face_id, point, radius);
}

bool WallQueryOrderBefore(const WallQueryOrder& a, const WallQueryOrder& b) {
    if (a.has_og_solid != b.has_og_solid) return a.has_og_solid;
    if (a.has_og_solid) {
        if (a.query_cell != b.query_cell) return a.query_cell < b.query_cell;
        if (a.insertion_ordinal != b.insertion_ordinal)
            return a.insertion_ordinal < b.insertion_ordinal;
    }
    if (a.source_face_id != b.source_face_id)
        return a.source_face_id < b.source_face_id;
    return a.face_id < b.face_id;
}

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

    normal = collider.normal;
    return true;
}

bool RaycastBox(const Collider& collider, const Vec3& origin, const Vec3& direction, float max_distance,
                float& distance, Vec3& normal) {
    float near_time = 0.0f;
    float far_time = max_distance;
    Vec3 near_normal;
    Vec3 far_normal;

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
        if (axis_far < far_time) {
            far_time = axis_far;
            far_normal = {-axis_normal.x,-axis_normal.y,-axis_normal.z};
        }
        if (near_time > far_time) return false;
    }

    if (near_time < 0.0f || near_time > max_distance) return false;
    const bool has_entry = near_normal.x != 0 || near_normal.y != 0 || near_normal.z != 0;
    distance = has_entry ? near_time : far_time;
    normal = has_entry ? near_normal : far_normal;
    if (normal.x == 0 && normal.y == 0 && normal.z == 0) return false;
    return true;
}

bool IsCloser(float candidate_dist, int candidate_face_id, const GroundHit& best) {
    if (!best.hit) return true;
    if (candidate_dist < best.distance - kRayEpsilon) return true;
    return Abs(candidate_dist - best.distance) <= kRayEpsilon && candidate_face_id < best.face_id;
}

Vec3 WorldToActorLocalPoint(const Vec3& world, const DynamicSolidView& view) {
    const float dx = world.x - view.position.x;
    const float dz = world.z - view.position.z;
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    return {cosine * dx - sine * dz, world.y - view.position.y,
            sine * dx + cosine * dz};
}

Vec3 WorldToActorLocalVector(const Vec3& world, const DynamicSolidView& view) {
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    return {cosine * world.x - sine * world.z, world.y,
            sine * world.x + cosine * world.z};
}

Vec3 ActorLocalToWorldPoint(const Vec3& local, const DynamicSolidView& view) {
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    return {view.position.x + cosine * local.x + sine * local.z,
            view.position.y + local.y,
            view.position.z - sine * local.x + cosine * local.z};
}

Vec3 ActorLocalToWorldVector(const Vec3& local, const DynamicSolidView& view) {
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    return {cosine * local.x + sine * local.z, local.y,
            -sine * local.x + cosine * local.z};
}

bool IsDynamicProxyTriangle(const physics::CollMesh& mesh, int triangle_index,
                            void* user_data) {
    const Room& room = *static_cast<const Room*>(user_data);
    if (!room.dynamic_solids) return false;
    const physics::CollSolidGridRecord* solid =
        physics::SolidGridRecordOf(mesh, triangle_index);
    return solid && room.dynamic_solids->OwnsSolidId(solid->solid_id);
}

bool IncludeStaticTriangle(const physics::CollMesh& mesh, int triangle_index,
                           void* user_data) {
    return !IsDynamicProxyTriangle(mesh, triangle_index, user_data);
}

bool BoundsOverlapSegment(const AABB& bounds, const Vec3& origin,
                          const Vec3& direction, float max_distance) {
    const Vec3 end = AddScaled(origin, direction, max_distance);
    const physics::AABB segment_bounds{
        {std::fmin(origin.x, end.x), std::fmin(origin.y, end.y),
         std::fmin(origin.z, end.z)},
        {std::fmax(origin.x, end.x), std::fmax(origin.y, end.y),
         std::fmax(origin.z, end.z)}};
    return physics::AABBOverlap({bounds.min, bounds.max}, segment_bounds);
}

GroundHit RaycastDynamicSolids(const Room& room, const Vec3& origin,
                               const Vec3& direction, float max_distance,
                               BackfacePolicy backface, GroundHit best) {
    const DynamicSolidGeometry* geometry = room.dynamic_solids;
    if (!geometry || !room.coll_mesh || max_distance < 0.0f) return best;
    const SourceRayQueryBounds query = MakeSourceRayQueryBounds(
        origin, AddScaled(origin, direction, max_distance));
    SourceRayOrder best_order = best.hit
        ? MakeStaticRayOrder(*room.coll_mesh, best.face_id, query)
        : SourceRayOrder{};
    const DynamicSolidSpec* actors = geometry->Actors();
    const DynamicSolidView* views = geometry->Views();
    const DynamicSolidFace* faces = geometry->Faces();
    const DynamicSolidVertex* vertices = geometry->Vertices();
    for (uint16_t actor_index = 0; actor_index < geometry->ActorCount(); ++actor_index) {
        const DynamicSolidView& view = views[actor_index];
        if (!view.collidable) continue;
        if (!BoundsOverlapSegment(view.bounds, origin, direction, max_distance)) continue;
        const Vec3 local_origin = WorldToActorLocalPoint(origin, view);
        const Vec3 local_direction = WorldToActorLocalVector(direction, view);
        const Vec3 local_end = AddScaled(local_origin, local_direction, max_distance);
        const DynamicSolidSpec& actor = actors[actor_index];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & physics::MAT_SOLID) ||
                face.cms_source_face_id == 0) continue;
            const int stable_face_id = face.collision_face_id == 0xFFFFu
                ? -1 : static_cast<int>(face.collision_face_id);
            if (stable_face_id < 0) continue;
            for (uint32_t fan = 1; fan + 1 < face.vertex_count; ++fan) {
                const Vec3& a = vertices[face.first_vertex].position;
                const Vec3& b = vertices[face.first_vertex + fan].position;
                const Vec3& c = vertices[face.first_vertex + fan + 1].position;
                const physics::TriHit hit = physics::SegmentTriangle(
                    local_origin, local_end, a, b, c,
                    backface == BackfacePolicy::Ignore
                        ? physics::BackfaceCull::Cull
                        : physics::BackfaceCull::Ignore);
                if (!hit.hit) continue;
                const float distance = hit.t * max_distance;
                const SourceRayOrder order = MakeDynamicRayOrder(
                    view, face.cms_source_face_id, stable_face_id, query);
                if (!SourceRayCandidateWins(distance, order, best,
                                           best_order)) continue;
                const Vec3 normal = ActorLocalToWorldVector(
                    physics::Norm(hit.normal), view);
                best = GroundHit{
                    .hit = true,
                    .point = ActorLocalToWorldPoint(hit.point, view),
                    .normal = normal,
                    .distance = distance,
                    .face_id = stable_face_id,
                    .owner_id = 0x40000000 | actor_index,
                    .owner_velocity = view.velocity,
                };
                best_order = order;
            }
        }
    }
    return best;
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
    const Vec3 end = AddScaled(origin, direction, max_distance);
    const SourceRayQueryBounds source_query =
        MakeSourceRayQueryBounds(origin, end);
    const physics::AABB query{
        {std::fmin(origin.x, end.x) - 1.0f,
         std::fmin(origin.y, end.y) - 1.0f,
         std::fmin(origin.z, end.z) - 1.0f},
        {std::fmax(origin.x, end.x) + 1.0f,
         std::fmax(origin.y, end.y) + 1.0f,
         std::fmax(origin.z, end.z) + 1.0f}};
    struct QueryState {
        const Room* room;
        const CollMesh* mesh;
        Vec3 origin;
        Vec3 end;
        Vec3 direction;
        float max_distance;
        BackfaceCull cull;
        SourceRayQueryBounds source_query;
        GroundHit best;
        SourceRayOrder best_order;
    } state{&room, &mesh, origin, end, direction, max_distance,
            backface == BackfacePolicy::Ignore
                ? BackfaceCull::Cull : BackfaceCull::Ignore,
            source_query, GroundHit{}, SourceRayOrder{}};
    const auto visit = [](int face_id, void* data) -> bool {
        QueryState& state = *static_cast<QueryState*>(data);
        const CollMesh& mesh = *state.mesh;
        if (!IncludeStaticTriangle(mesh, face_id,
                                   const_cast<Room*>(state.room))) return true;
        const CollTriangle& triangle = mesh.triangles[face_id];
        const TriHit hit = SegmentTriangle(
            state.origin, state.end,
            mesh.world_verts[triangle.i0],
            mesh.world_verts[triangle.i1],
            mesh.world_verts[triangle.i2], state.cull);
        if (!hit.hit) return true;
        const float distance = hit.t * state.max_distance;
        const SourceRayOrder order = MakeStaticRayOrder(
            mesh, face_id, state.source_query);
        if (!SourceRayCandidateWins(distance, order, state.best,
                                    state.best_order)) return true;
        const OwnerInfo owner = ResolveOwner(mesh, *state.room, face_id);
        state.best = GroundHit{
            .hit = true,
            .point = hit.point,
            .normal = Norm(hit.normal),
            .distance = distance,
            .face_id = face_id,
            .owner_id = owner.id,
            .owner_velocity = owner.velocity,
        };
        state.best_order = order;
        return true;
    };
    uint32_t nodes = 0;
    uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
    VisitOverlappingTriangles(mesh, query, visit, &state, nodes_ptr);
    if (room.query_counters) {
        ++room.query_counters->raycasts;
        room.query_counters->bvh_nodes_touched += nodes;
    }
    return RaycastDynamicSolids(room, origin, direction, max_distance,
                                backface, state.best);
}

static void InsertOrderedWallHit(const Room& room, const Vec3& point,
                                 float radius, const WallHit& candidate,
                                 WallHit* out_hits, int max_hits, int& count) {
    const uint16_t source_face = room.coll_mesh && candidate.face_id >= 0 &&
        static_cast<uint32_t>(candidate.face_id) < room.coll_mesh->header->triangle_count
        ? room.coll_mesh->triangles[candidate.face_id].source_face_id : 0;
    for (int i = 0; i < count; ++i) {
        const int previous_id = out_hits[i].face_id;
        const uint16_t previous_source = room.coll_mesh && previous_id >= 0 &&
            static_cast<uint32_t>(previous_id) < room.coll_mesh->header->triangle_count
            ? room.coll_mesh->triangles[previous_id].source_face_id : 0;
        if ((source_face != 0 && source_face == previous_source) ||
            (source_face == 0 && candidate.face_id == previous_id)) {
            if (candidate.pushout > out_hits[i].pushout)
                out_hits[i] = candidate;
            return;
        }
    }
    const WallQueryOrder key = MakeRoomWallQueryOrder(
        room, candidate.face_id, point, radius);
    int insert_at = count;
    while (insert_at > 0) {
        const WallQueryOrder previous_key = MakeRoomWallQueryOrder(
            room, out_hits[insert_at - 1].face_id, point, radius);
        if (!WallQueryOrderBefore(key, previous_key)) break;
        if (insert_at < max_hits) out_hits[insert_at] = out_hits[insert_at - 1];
        --insert_at;
    }
    if (insert_at < max_hits) {
        out_hits[insert_at] = candidate;
        if (count < max_hits) ++count;
    }
}

static int QueryWallsMesh(const Room& room, const physics::CollMesh& mesh,
                          const Vec3& point, float radius,
                          WallHit* out_hits, int max_hits) {
    using namespace physics;
    if (!out_hits || max_hits <= 0) return 0;
    const physics::AABB query{
        {point.x - radius, point.y - radius, point.z - radius},
        {point.x + radius, point.y + radius, point.z + radius}};
    struct QueryState {
        const Room* room;
        const CollMesh* mesh;
        const Vec3* point;
        float radius;
        WallHit* out_hits;
        int max_hits;
        int count;
    } state{&room, &mesh, &point, radius, out_hits, max_hits, 0};
    const auto visit = [](int fid, void* data) -> bool {
        QueryState& state = *static_cast<QueryState*>(data);
        const Room& room = *state.room;
        const CollMesh& mesh = *state.mesh;
        if (IsDynamicProxyTriangle(mesh, fid, const_cast<Room*>(&room))) return true;
        const CollTriangle& ct = mesh.triangles[fid];
        if (!(ct.material & MAT_SOLID)) return true;
        const Vec3& a = mesh.world_verts[ct.i0];
        const Vec3& b = mesh.world_verts[ct.i1];
        const Vec3& c = mesh.world_verts[ct.i2];
        const SphereTriHit sh = SphereTriangle(*state.point, state.radius, a, b, c);
        if (!sh.hit) return true;
        const Vec3 delta = Sub(*state.point, sh.closest);
        const float length = std::sqrt(Len2(delta));
        if (length < 1e-7f) return true;
        const Vec3 normal = Scale(delta, 1.0f / length);
        if (normal.y >= 0.85f || normal.y <= -0.85f) return true;
        Vec3 plane_normal = Norm(Cross(Sub(b, a), Sub(c, a)));
        if (Dot(plane_normal, delta) < 0.0f)
            plane_normal = Scale(plane_normal, -1.0f);
        const float pushout = state.radius - sh.dist;
        if (pushout < 0.0f) return true;
        const OwnerInfo owner = ResolveOwner(mesh, room, fid);
        InsertOrderedWallHit(room, *state.point, state.radius,
            WallHit{.hit=true,.point=sh.closest,.normal=normal,
                    .plane_normal=plane_normal,.pushout=pushout,.face_id=fid,
                    .owner_id=owner.id,.owner_velocity=owner.velocity},
            state.out_hits, state.max_hits, state.count);
        return true;
    };
    uint32_t nodes = 0;
    VisitOverlappingTriangles(mesh, query, visit, &state,
                              room.query_counters ? &nodes : nullptr);
    if (room.query_counters) {
        ++room.query_counters->overlap_queries;
        room.query_counters->bvh_nodes_touched += nodes;
    }

    const DynamicSolidGeometry* geometry = room.dynamic_solids;
    if (!geometry) return state.count;
    const DynamicSolidSpec* actors = geometry->Actors();
    const DynamicSolidView* views = geometry->Views();
    const DynamicSolidFace* faces = geometry->Faces();
    const DynamicSolidVertex* vertices = geometry->Vertices();
    const madeline_cube::AABB query_world{query.min, query.max};
    for (uint16_t ai = 0; ai < geometry->ActorCount(); ++ai) {
        const DynamicSolidView& view = views[ai];
        if (!view.collidable) continue;
        if (!view.bounds.Intersects(query_world)) continue;
        const Vec3 local_point = WorldToActorLocalPoint(point, view);
        const DynamicSolidSpec& actor = actors[ai];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & MAT_SOLID) ||
                face.cms_source_face_id == 0) continue;
            const int stable_face = face.collision_face_id == 0xFFFFu
                ? -1 : static_cast<int>(face.collision_face_id);
            if (stable_face < 0) continue;
            WallHit candidate{};
            bool found = false;
            for (uint32_t fan = 1; fan + 1 < face.vertex_count; ++fan) {
                const Vec3& a = vertices[face.first_vertex].position;
                const Vec3& b = vertices[face.first_vertex + fan].position;
                const Vec3& c = vertices[face.first_vertex + fan + 1].position;
                const SphereTriHit sh = SphereTriangle(local_point, radius, a, b, c);
                if (!sh.hit) continue;
                const Vec3 delta = Sub(local_point, sh.closest);
                const float length = std::sqrt(Len2(delta));
                if (length < 1e-7f) continue;
                const Vec3 local_normal = Scale(delta, 1.0f / length);
                if (local_normal.y >= 0.85f || local_normal.y <= -0.85f) continue;
                Vec3 plane_normal = Norm(Cross(Sub(b, a), Sub(c, a)));
                if (Dot(plane_normal, delta) < 0.0f)
                    plane_normal = Scale(plane_normal, -1.0f);
                const float pushout = radius - sh.dist;
                if (pushout < 0.0f || (found && pushout <= candidate.pushout)) continue;
                candidate = WallHit{
                    .hit=true,
                    .point=ActorLocalToWorldPoint(sh.closest, view),
                    .normal=ActorLocalToWorldVector(local_normal, view),
                    .plane_normal=ActorLocalToWorldVector(plane_normal, view),
                    .pushout=pushout,.face_id=stable_face,
                    .owner_id=0x40000000 | ai,.owner_velocity=view.velocity};
                found = true;
            }
            if (found) InsertOrderedWallHit(room, point, radius, candidate,
                                             out_hits, max_hits, state.count);
        }
    }
    return state.count;
}

// OG World.SolidWallCheck intersects each triangle with the probe's horizontal
// plane, then chooses a finite segment contact. CMSH winding is the authored
// solid-sidedness contract: a build must emit outward triangles. Never flip
// normals according to a query point; malformed bake orientation is an asset bug.
static int QueryWallsFacePlaneMesh(const Room& room, const physics::CollMesh& mesh,
                                    const Vec3& point, float radius,
                                    WallHit* out_hits, int max_hits,
                                    CollisionQueryDiagnostics* diagnostics,
                                    bool /*orient_to_point*/) {
    using namespace physics;
    const float query_radius = radius + std::max(1e-5f, radius * 1e-6f);
    physics::AABB q = {
        { point.x - query_radius, point.y - query_radius, point.z - query_radius },
        { point.x + query_radius, point.y + query_radius, point.z + query_radius },
    };
    if (out_hits == nullptr || max_hits <= 0) return 0;
    uint32_t nodes = 0;
    uint32_t* nodes_ptr = room.query_counters ? &nodes : nullptr;
    struct QueryState {
        const Room* room;
        const physics::CollMesh* mesh;
        const Vec3* point;
        float radius;
        WallHit* out_hits;
        int max_hits;
        CollisionQueryDiagnostics* diagnostics;
        int count;
    } state{&room, &mesh, &point, radius, out_hits, max_hits, diagnostics, 0};

    const auto visit = [](int fid, void* user_data) -> bool {
        QueryState& state = *static_cast<QueryState*>(user_data);
        const Room& room = *state.room;
        const physics::CollMesh& mesh = *state.mesh;
        const Vec3& point = *state.point;
        const float radius = state.radius;
        WallHit* out_hits = state.out_hits;
        const int max_hits = state.max_hits;
        CollisionQueryDiagnostics* diagnostics = state.diagnostics;
        const CollTriangle& ct = mesh.triangles[fid];
        if (IsDynamicProxyTriangle(mesh, fid,
                                   const_cast<Room*>(&room))) return true;
        if (!(ct.material & MAT_SOLID)) return true;

        const Vec3& a = mesh.world_verts[ct.i0];
        const Vec3& b = mesh.world_verts[ct.i1];
        const Vec3& c = mesh.world_verts[ct.i2];

        const Vec3 raw = Cross(Sub(b, a), Sub(c, a));
        if (Len2(raw) <= 1e-12f) return true;
        const Vec3 pn = Norm(raw);
        // OG general wall queries exclude exactly horizontal faces only.
        // ClimbNormalCheck (.35) belongs to the climb caller after ranking.
        if (std::fabs(pn.y) >= 1.0f) return true;
        const float plane_distance = Dot(pn, Sub(point, a));
        if (plane_distance < 0.0f || plane_distance > radius) return true;
        // One-way platforms support descending feet only, never wall grip.
        if (ct.material & MAT_ONEWAY) return true;

        Vec3 closest;
        if (!ClosestTriangleHorizontalSlice(point, a, b, c, closest)) return true;
        const Vec3 delta = Sub(point, closest);
        const float distance2 = Len2(delta);
        if (distance2 > radius * radius) return true;
        const float dist = std::sqrt(distance2);
        const float pushout = std::max(0.0f, radius - dist);

        if (diagnostics) ++diagnostics->wall_candidate_hits;
        OwnerInfo owner = ResolveOwner(mesh, room, fid);
        const WallHit candidate = WallHit{
            .hit = true,
            .point = closest,
            .normal = dist > 1e-6f ? Scale(delta, 1.0f / dist) : pn,
            .plane_normal = pn,
            .pushout = pushout,
            .face_id = fid,
            .owner_id = owner.id,
            .owner_velocity = owner.velocity,
        };
        // OG emits one hit per authored face, after selecting that face's
        // closest fan triangle. New CMSH files preserve this group in
        // source_face_id; legacy files (zero) retain geometric fan merging.
        int same_face = -1;
        for (int j = 0; j < state.count; ++j) {
            const int previous_id = out_hits[j].face_id;
            const uint16_t previous_source_face = previous_id >= 0 &&
                previous_id < static_cast<int>(mesh.header->triangle_count)
                ? mesh.triangles[previous_id].source_face_id : 0;
            const bool same_authored_face = ct.source_face_id != 0 &&
                previous_source_face == ct.source_face_id;
            const bool same_legacy_plane = ct.source_face_id == 0 &&
                previous_source_face == 0 &&
                Dot(out_hits[j].plane_normal, pn) > .99999f &&
                std::fabs(Dot(pn, Sub(out_hits[j].point, closest))) < .001f;
            if (same_authored_face || same_legacy_plane) {
                same_face = j;
                break;
            }
        }
        if (same_face >= 0) {
            if (candidate.pushout > out_hits[same_face].pushout ||
                (candidate.pushout == out_hits[same_face].pushout &&
                 candidate.face_id < out_hits[same_face].face_id))
                out_hits[same_face] = candidate;
            return true;
        }

        // BVH order is spatial. Restore OG GridPartition's first shared-cell,
        // solid insertion, then source face order before its StackList8 cap.
        const WallQueryOrder order_key =
            MakeRoomWallQueryOrder(room, fid, point, radius);
        int insert_at = state.count;
        while (insert_at > 0) {
            const int previous_id = out_hits[insert_at - 1].face_id;
            const WallQueryOrder previous_key =
                MakeRoomWallQueryOrder(room, previous_id, point, radius);
            if (!WallQueryOrderBefore(order_key, previous_key)) break;
            if (insert_at < max_hits)
                out_hits[insert_at] = out_hits[insert_at - 1];
            --insert_at;
        }
        if (insert_at < max_hits) {
            out_hits[insert_at] = candidate;
            if (state.count < max_hits) ++state.count;
        }
        return true;
    };
    VisitOverlappingTriangles(mesh, q, visit, &state, nodes_ptr);
    if (room.query_counters) {
        ++room.query_counters->overlap_queries;
        room.query_counters->bvh_nodes_touched += nodes;
    }
    return state.count;
}

static int QueryDynamicWallsFacePlane(const Room& room, const Vec3& point,
                                      float radius, WallHit* out_hits,
                                      int max_hits, int initial_count,
                                      CollisionQueryDiagnostics* diagnostics) {
    const DynamicSolidGeometry* geometry = room.dynamic_solids;
    if (!geometry || !room.coll_mesh || !out_hits || max_hits <= 0)
        return initial_count;
    int count = initial_count;
    const DynamicSolidSpec* actors = geometry->Actors();
    const DynamicSolidView* views = geometry->Views();
    const DynamicSolidFace* faces = geometry->Faces();
    const DynamicSolidVertex* vertices = geometry->Vertices();
    const float query_radius = radius + std::max(1e-5f, radius * 1e-6f);
    for (uint16_t actor_index = 0; actor_index < geometry->ActorCount(); ++actor_index) {
        const DynamicSolidSpec& actor = actors[actor_index];
        const DynamicSolidView& view = views[actor_index];
        if (!view.collidable) continue;
        if (point.x + query_radius < view.bounds.min.x ||
            point.x - query_radius > view.bounds.max.x ||
            point.y + query_radius < view.bounds.min.y ||
            point.y - query_radius > view.bounds.max.y ||
            point.z + query_radius < view.bounds.min.z ||
            point.z - query_radius > view.bounds.max.z) continue;
        const Vec3 local_point = WorldToActorLocalPoint(point, view);
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & physics::MAT_SOLID) ||
                (face.collision_material & physics::MAT_ONEWAY) ||
                face.cms_source_face_id == 0) continue;
            const int stable_face_id = face.collision_face_id == 0xFFFFu
                ? -1 : static_cast<int>(face.collision_face_id);
            if (stable_face_id < 0) continue;

            WallHit candidate{};
            bool face_hit = false;
            for (uint32_t fan = 1; fan + 1 < face.vertex_count; ++fan) {
                const Vec3& a = vertices[face.first_vertex].position;
                const Vec3& b = vertices[face.first_vertex + fan].position;
                const Vec3& c = vertices[face.first_vertex + fan + 1].position;
                const Vec3 raw = physics::Cross(physics::Sub(b, a),
                                                physics::Sub(c, a));
                if (physics::Len2(raw) <= 1e-12f) continue;
                const Vec3 local_normal = physics::Norm(raw);
                if (std::fabs(local_normal.y) >= 1.0f) continue;
                const float plane_distance = physics::Dot(
                    local_normal, physics::Sub(local_point, a));
                if (plane_distance < 0.0f || plane_distance > radius)
                    continue;
                Vec3 closest;
                if (!physics::ClosestTriangleHorizontalSlice(
                        local_point, a, b, c, closest)) continue;
                const Vec3 delta = physics::Sub(local_point, closest);
                const float distance2 = physics::Len2(delta);
                if (distance2 > radius * radius) continue;
                const float distance = std::sqrt(distance2);
                const float pushout = std::max(0.0f, radius - distance);
                if (face_hit && pushout <= candidate.pushout) continue;
                const Vec3 plane_normal = ActorLocalToWorldVector(local_normal, view);
                const Vec3 contact_normal = distance > 1e-6f
                    ? ActorLocalToWorldVector(physics::Scale(delta, 1.0f / distance), view)
                    : plane_normal;
                candidate = WallHit{
                    .hit = true,
                    .point = ActorLocalToWorldPoint(closest, view),
                    .normal = contact_normal,
                    .plane_normal = plane_normal,
                    .pushout = pushout,
                    .face_id = stable_face_id,
                    .owner_id = 0x40000000 | actor_index,
                    .owner_velocity = view.velocity,
                };
                face_hit = true;
            }
            if (!face_hit) continue;
            if (diagnostics) ++diagnostics->wall_candidate_hits;

            WallQueryOrder key;
            const AABB& bounds = view.bounds;
            if (!TryOgWallBoundsOrder(bounds.min.x, -bounds.max.z,
                                      bounds.max.x, -bounds.min.z,
                                      view.reinsert_ordinal,
                                      face.cms_source_face_id,
                                      point, radius, key)) continue;
            key.face_id = candidate.face_id;
            int insert_at = count;
            while (insert_at > 0) {
                const WallHit& previous = out_hits[insert_at - 1];
                const WallQueryOrder previous_key =
                    MakeRoomWallQueryOrder(room, previous.face_id, point, radius);
                if (!WallQueryOrderBefore(key, previous_key)) break;
                if (insert_at < max_hits)
                    out_hits[insert_at] = out_hits[insert_at - 1];
                --insert_at;
            }
            if (insert_at < max_hits) {
                out_hits[insert_at] = candidate;
                if (count < max_hits) ++count;
            }
        }
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

physics::SweepSphereHit SweepSphereRoom(const Room& room, const Vec3& origin,
                                        const Vec3& direction, float radius,
                                        float max_distance) {
    using namespace physics;
    SweepSphereHit best;
    if (!room.coll_mesh || max_distance < 0.0f || radius < 0.0f) return best;
    uint32_t nodes = 0;
    best = SweepSphereMesh(*room.coll_mesh, origin, direction, radius,
                           max_distance,
                           room.query_counters ? &nodes : nullptr,
                           IncludeStaticTriangle, const_cast<Room*>(&room));
    if (room.query_counters) {
        ++room.query_counters->sphere_sweeps;
        room.query_counters->bvh_nodes_touched += nodes;
    }

    const DynamicSolidGeometry* geometry = room.dynamic_solids;
    if (!geometry) return best;
    const DynamicSolidSpec* actors = geometry->Actors();
    const DynamicSolidView* views = geometry->Views();
    const DynamicSolidFace* faces = geometry->Faces();
    const DynamicSolidVertex* vertices = geometry->Vertices();
    const Vec3 end = AddScaled(origin, direction, max_distance);
    const madeline_cube::AABB swept_bounds{
        {std::fmin(origin.x, end.x) - radius,
         std::fmin(origin.y, end.y) - radius,
         std::fmin(origin.z, end.z) - radius},
        {std::fmax(origin.x, end.x) + radius,
         std::fmax(origin.y, end.y) + radius,
         std::fmax(origin.z, end.z) + radius}};
    for (uint16_t actor_index = 0; actor_index < geometry->ActorCount(); ++actor_index) {
        const DynamicSolidView& view = views[actor_index];
        if (!view.collidable) continue;
        if (!view.bounds.Intersects(swept_bounds)) continue;
        const Vec3 local_origin = WorldToActorLocalPoint(origin, view);
        const Vec3 local_direction = WorldToActorLocalVector(direction, view);
        const DynamicSolidSpec& actor = actors[actor_index];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & MAT_SOLID) ||
                face.cms_source_face_id == 0) continue;
            const int stable_face_id = face.collision_face_id == 0xFFFFu
                ? -1 : static_cast<int>(face.collision_face_id);
            if (stable_face_id < 0) continue;
            for (uint32_t fan = 1; fan + 1 < face.vertex_count; ++fan) {
                const Vec3& a = vertices[face.first_vertex].position;
                const Vec3& b = vertices[face.first_vertex + fan].position;
                const Vec3& c = vertices[face.first_vertex + fan + 1].position;
                float hit_distance = -1.0f;
                SphereTriHit sphere_hit = SphereTriangle(local_origin, radius,
                                                         a, b, c);
                if (sphere_hit.hit) {
                    hit_distance = 0.0f;
                } else if (max_distance > 0.0f) {
                    for (int sample = 1; sample <= 8; ++sample) {
                        const float distance =
                            (static_cast<float>(sample) / 8.0f) * max_distance;
                        const Vec3 center = AddScaled(local_origin, local_direction,
                                                      distance);
                        sphere_hit = SphereTriangle(center, radius, a, b, c);
                        if (sphere_hit.hit) {
                            hit_distance = distance;
                            break;
                        }
                    }
                }
                if (hit_distance < 0.0f) continue;
                const float t = max_distance > 0.0f
                    ? hit_distance / max_distance : 0.0f;
                if (best.hit && (t > best.t ||
                    (t == best.t && stable_face_id >= best.face_id))) continue;
                best = SweepSphereHit{
                    .hit = true,
                    .t = t,
                    .point = ActorLocalToWorldPoint(sphere_hit.closest, view),
                    .normal = ActorLocalToWorldVector(sphere_hit.normal, view),
                    .face_id = stable_face_id,
                    .material = face.collision_material,
                };
            }
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
                        CollisionQueryDiagnostics* diagnostics,
                        bool orient_to_point) {
    int count = 0;
    // Static world geometry: plane-projected wall query (OG parity). This is
    // the source of truth for climb grip identity and wall contact.
    if (room.coll_mesh) {
        count = QueryWallsFacePlaneMesh(room, *room.coll_mesh, point, radius,
                                        out_hits, max_hits, diagnostics,
                                        orient_to_point);
    }

    // Dynamic DSOL faces share the same finite horizontal slice and are
    // merged into the bounded result using live OG grid-cell/insertion order.
    count = QueryDynamicWallsFacePlane(room, point, radius, out_hits,
                                       max_hits, count, diagnostics);
    if (count >= max_hits) return count;

    // Dynamic AABBs use the same horizontal slice contract. Do not mix the
    // static sphere contact family into this plane/ranking query.
    for (int i = 0; i < room.collider_count && count < max_hits; ++i) {
        const Collider& c = room.colliders[i];
        if (!c.solid || c.type != ColliderType::Box ||
            (room.coll_mesh && IsStaticCollider(c)) ||
            point.y < c.bounds.min.y || point.y > c.bounds.max.y) continue;
        const Vec3 normals[4] = {{-1,0,0},{1,0,0},{0,0,-1},{0,0,1}};
        const Vec3 plane_points[4] = {
            {c.bounds.min.x,point.y,c.bounds.min.z},
            {c.bounds.max.x,point.y,c.bounds.min.z},
            {c.bounds.min.x,point.y,c.bounds.min.z},
            {c.bounds.min.x,point.y,c.bounds.max.z},
        };
        for (int f = 0; f < 4 && count < max_hits; ++f) {
            const Vec3 pn = normals[f];
            const float distance = physics::Dot(pn,physics::Sub(point,plane_points[f]));
            if (distance < 0 || distance > radius) continue;
            Vec3 closest = {std::clamp(point.x,c.bounds.min.x,c.bounds.max.x),
                            point.y,std::clamp(point.z,c.bounds.min.z,c.bounds.max.z)};
            if (f < 2) closest.x = f == 0 ? c.bounds.min.x : c.bounds.max.x;
            else closest.z = f == 2 ? c.bounds.min.z : c.bounds.max.z;
            const Vec3 delta = physics::Sub(point,closest);
            const float d2 = physics::Dot(delta,delta);
            if (d2 > radius*radius) continue;
            const float d = std::sqrt(d2);
            const MovingSurface* surface = FindMovingSurface(room,c.owner_id);
            out_hits[count++] = WallHit{
                .hit=true,.point=closest,
                .normal=d>1e-6f ? physics::Scale(delta,1/d) : pn,
                .plane_normal=pn,.pushout=radius-d,
                .face_id=c.face_id>=0 ? c.face_id : i,.owner_id=c.owner_id,
                .owner_velocity=surface ? surface->rider_velocity : c.velocity,
            };
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

// OG SolidWallCheckNearest parity (World.cs:556-571): the plane-projected wall
// with the LARGEST pushout (most overlap). Reuses the unified plane query
// (QueryWallsFacePlane) so authored sidedness + finite geometry bounding stay
// single-sourced (M3: no near-duplicate wall family).
WallHit QueryWallPushout(const Room& room, const Vec3& point, float radius) {
    WallHit hits[kMaxWallHits];
    // General authored front-side wall hits; positional response uses the
    // finite horizontal slice direction and impact response the plane normal.
    const int count = QueryWallsFacePlane(room, point, radius, hits, kMaxWallHits,
                                          nullptr, false);
    if (count == 0) return WallHit{};

    WallHit best = hits[0];
    for (int i = 1; i < count; ++i) {
        // Reversed comparison: keep the MOST pushout (deepest overlap), like OG.
        if (hits[i].pushout > best.pushout) {
            best = hits[i];
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
    // Authored front-side candidates rank first; the selected face is then
    // gated by ClimbNormalCheck, exactly as the OG caller does.
    const int count = QueryWallsFacePlane(room, point, radius, hits, kMaxWallHits,
                                          nullptr, true);
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
        // OG ranks polygon faces after choosing their nearest fan triangle.
        // CMSH emits triangles separately: equivalent coplanar normals must
        // choose the closest finite slice before the normal-ranking tie.
        if (dot > best_dot) {
            best = hits[i];
            best_dot = dot;
        }
    }
    // Apply the climb gate only after the general query has selected its
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
    if (out_hit) *out_hit = {};
    const GroundHit hit = RaycastRoomSource(room,origin,direction,max_distance,
                                             BackfacePolicy::Ignore);
    if (!hit.hit) return false;
    // General solid ray: callers decide whether the authored normal is climbable.
    if (out_hit) *out_hit = WallHit{
        .hit=true,.point=hit.point,.normal=hit.normal,.plane_normal=hit.normal,
        .pushout=0,.face_id=hit.face_id,.owner_id=hit.owner_id,
        .owner_velocity=hit.owner_velocity,
    };
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

bool WorldCollision::LoadFromBuffer(const uint8_t* data, size_t size) {
    Reset();
    mesh_ = physics::LoadCollMeshFromBuffer(data, size);
    return mesh_ != nullptr;
}

void WorldCollision::Reset() {
    if (mesh_) {
        physics::FreeCollMesh(mesh_);
        mesh_ = nullptr;
    }
}

void WorldCollision::Swap(WorldCollision& other) {
    physics::CollMesh* mesh = mesh_;
    mesh_ = other.mesh_;
    other.mesh_ = mesh;
    const CollisionQueryCounters counters = counters_;
    counters_ = other.counters_;
    other.counters_ = counters;
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
