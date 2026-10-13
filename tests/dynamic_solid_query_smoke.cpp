#include <cassert>
#include <cmath>
#include <cstdio>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

namespace {

Vec3 Add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 ToWorldPoint(const Vec3& local, const DynamicSolidView& view) {
    const float c = std::cos(view.yaw_radians);
    const float s = std::sin(view.yaw_radians);
    return {view.position.x + c * local.x + s * local.z,
            view.position.y + local.y,
            view.position.z - s * local.x + c * local.z};
}

Vec3 ToWorldVector(const Vec3& local, const DynamicSolidView& view) {
    const float c = std::cos(view.yaw_radians);
    const float s = std::sin(view.yaw_radians);
    return {c * local.x + s * local.z, local.y,
            -s * local.x + c * local.z};
}

struct FaceProbe {
    uint16_t actor = 0;
    uint32_t face = 0;
    Vec3 center;
    Vec3 normal;
    int stable_face_id = -1;
};

struct LocalWallFace {
    uint16_t actor = 0;
    uint32_t face = 0;
    Vec3 center;
    Vec3 normal;
};

bool FindTwoLocalWallFaces(const DynamicSolidGeometry& geometry,
                           LocalWallFace out[2]) {
    const DynamicSolidFace* faces = geometry.Faces();
    const DynamicSolidVertex* vertices = geometry.Vertices();
    int count = 0;
    for (uint16_t ai = 0; ai < geometry.ActorCount(); ++ai) {
        const DynamicSolidSpec& actor = geometry.Actors()[ai];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & physics::MAT_SOLID) ||
                face.vertex_count < 3) continue;
            const Vec3 raw = physics::Cross(
                physics::Sub(vertices[face.first_vertex + 1].position,
                             vertices[face.first_vertex].position),
                physics::Sub(vertices[face.first_vertex + 2].position,
                             vertices[face.first_vertex].position));
            if (physics::Len2(raw) < 1e-8f) continue;
            const Vec3 normal = physics::Norm(raw);
            if (std::fabs(normal.y) > 0.01f) continue;
            bool actor_already_used = false;
            for (int i = 0; i < count; ++i)
                actor_already_used |= out[i].actor == ai;
            if (actor_already_used) continue;
            Vec3 center{};
            for (uint32_t v = 0; v < face.vertex_count; ++v) {
                const Vec3& p = vertices[face.first_vertex + v].position;
                center.x += p.x / face.vertex_count;
                center.y += p.y / face.vertex_count;
                center.z += p.z / face.vertex_count;
            }
            out[count++] = {ai, fi, center, normal};
            if (count == 2) return true;
        }
    }
    return false;
}

void PlaceLocalWallFace(DynamicSolidGeometry& geometry,
                        const LocalWallFace& face, const Vec3& target,
                        const Vec3& target_normal = {1.0f, 0.0f, 0.0f}) {
    const float yaw = std::atan2(face.normal.z, face.normal.x) -
                      std::atan2(target_normal.z, target_normal.x);
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const Vec3 position{
        target.x - c * face.center.x - s * face.center.z,
        target.y - face.center.y,
        target.z + s * face.center.x - c * face.center.z};
    assert(geometry.SetTransform(geometry.Actors()[face.actor].source_id,
                                 position, yaw));
}

int FirstOwnerOrder(const WallHit* hits, int count, uint16_t actor) {
    for (int i = 0; i < count; ++i)
        if (hits[i].owner_id == (0x40000000 | actor)) return i;
    return -1;
}

int PositiveModulo(int value) {
    const int remainder = value % 100;
    return remainder < 0 ? remainder + 100 : remainder;
}

uint32_t FirstSharedGridCell(float min_x, float min_y, float max_x,
                             float max_y, const Vec3& point, float radius) {
    const int q_left = static_cast<int>(std::floor((point.x - radius - 1.0) / 200.0));
    const int q_top = static_cast<int>(std::floor((-point.z - radius - 1.0) / 200.0));
    const int q_right = static_cast<int>(std::ceil((point.x + radius + 1.0) / 200.0));
    const int q_bottom = static_cast<int>(std::ceil((-point.z + radius + 1.0) / 200.0));
    const int q_height = q_bottom - q_top;
    const int s_left = static_cast<int>(std::floor((min_x - 1.0) / 200.0));
    const int s_top = static_cast<int>(std::floor((min_y - 1.0) / 200.0));
    const int s_right = static_cast<int>(std::ceil((max_x + 1.0) / 200.0));
    const int s_bottom = static_cast<int>(std::ceil((max_y + 1.0) / 200.0));
    for (int x = q_left; x < q_right; ++x) {
        const bool hit_x = s_right - s_left >= 100 ||
            (s_right > s_left && PositiveModulo(x - s_left) < s_right - s_left);
        if (!hit_x) continue;
        for (int y = q_top; y < q_bottom; ++y) {
            const bool hit_y = s_bottom - s_top >= 100 ||
                (s_bottom > s_top && PositiveModulo(y - s_top) < s_bottom - s_top);
            if (!hit_y) continue;
            return static_cast<uint32_t>(x - q_left) *
                   static_cast<uint32_t>(q_height) +
                   static_cast<uint32_t>(y - q_top);
        }
    }
    return 0xFFFFFFFFu;
}

uint32_t FirstSharedRayGridCell(float solid_min_x, float solid_min_y,
                                float solid_max_x, float solid_max_y,
                                const Vec3& origin, const Vec3& direction,
                                float max_distance) {
    const Vec3 end = Add(origin, {direction.x * max_distance,
                                  direction.y * max_distance,
                                  direction.z * max_distance});
    const int query_left = static_cast<int>(std::floor(
        (std::fmin(origin.x, end.x) - 1.0f) / 200.0f));
    const int query_top = static_cast<int>(std::floor(
        (-std::fmax(origin.z, end.z) - 1.0f) / 200.0f));
    const int query_right = static_cast<int>(std::ceil(
        (std::fmax(origin.x, end.x) + 1.0f) / 200.0f));
    const int query_bottom = static_cast<int>(std::ceil(
        (-std::fmin(origin.z, end.z) + 1.0f) / 200.0f));
    const int query_height = query_bottom - query_top;
    const int solid_left = static_cast<int>(std::floor(
        (solid_min_x - 1.0f) / 200.0f));
    const int solid_top = static_cast<int>(std::floor(
        (solid_min_y - 1.0f) / 200.0f));
    const int solid_right = static_cast<int>(std::ceil(
        (solid_max_x + 1.0f) / 200.0f));
    const int solid_bottom = static_cast<int>(std::ceil(
        (solid_max_y + 1.0f) / 200.0f));
    for (int x = query_left; x < query_right; ++x) {
        const bool hit_x = solid_right - solid_left >= 100 ||
            (solid_right > solid_left &&
             PositiveModulo(x - solid_left) < solid_right - solid_left);
        if (!hit_x) continue;
        for (int y = query_top; y < query_bottom; ++y) {
            const bool hit_y = solid_bottom - solid_top >= 100 ||
                (solid_bottom > solid_top &&
                 PositiveModulo(y - solid_top) < solid_bottom - solid_top);
            if (!hit_y) continue;
            return static_cast<uint32_t>(x - query_left) *
                   static_cast<uint32_t>(query_height) +
                   static_cast<uint32_t>(y - query_top);
        }
    }
    return 0xFFFFFFFFu;
}

Vec3 ToLocalPoint(const Vec3& world, const DynamicSolidView& view) {
    const float c = std::cos(view.yaw_radians);
    const float s = std::sin(view.yaw_radians);
    const float dx = world.x - view.position.x;
    const float dz = world.z - view.position.z;
    return {c * dx - s * dz, world.y - view.position.y,
            s * dx + c * dz};
}

bool FindFace(const MapRuntime& runtime, bool wall_only, bool floor_only,
              bool ceiling_only, FaceProbe& out) {
    const DynamicSolidGeometry& geometry = runtime.DynamicGeometry();
    const DynamicSolidFace* faces = geometry.Faces();
    const DynamicSolidVertex* vertices = geometry.Vertices();
    const DynamicSolidView* views = geometry.Views();
    const physics::CollMesh* mesh = runtime.GlobalCollision().Mesh();
    for (uint16_t ai = 0; ai < geometry.ActorCount(); ++ai) {
        const DynamicSolidSpec& actor = geometry.Actors()[ai];
        const DynamicSolidView& view = views[ai];
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = faces[fi];
            if (!(face.collision_material & physics::MAT_SOLID) ||
                face.vertex_count < 3 || face.cms_source_face_id == 0) continue;
            Vec3 raw = physics::Cross(
                physics::Sub(vertices[face.first_vertex + 1].position,
                             vertices[face.first_vertex].position),
                physics::Sub(vertices[face.first_vertex + 2].position,
                             vertices[face.first_vertex].position));
            if (physics::Len2(raw) < 1e-8f) continue;
            const Vec3 normal = physics::Norm(raw);
            if (wall_only && std::fabs(normal.y) >= 0.30f) continue;
            if (floor_only && normal.y < 0.95f) continue;
            if (ceiling_only && normal.y > -0.95f) continue;
            Vec3 center{};
            for (uint32_t v = 0; v < face.vertex_count; ++v) {
                const Vec3 world = ToWorldPoint(
                    vertices[face.first_vertex + v].position, view);
                center.x += world.x / face.vertex_count;
                center.y += world.y / face.vertex_count;
                center.z += world.z / face.vertex_count;
            }
            const int stable = physics::SourceFaceTriangleOf(
                *mesh, face.cms_source_face_id);
            if (stable < 0) continue;
            out = {ai, fi, center, ToWorldVector(normal, view), stable};
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc >= 3);
    MapRuntime runtime;
    assert(runtime.Init(argv[1], argv[2]));
    assert(runtime.DynamicGeometry().ActorCount() == 118);
    for (uint16_t actor = 0; actor < runtime.DynamicGeometry().ActorCount(); ++actor) {
        const uint16_t solid_id = runtime.DynamicGeometry().Views()[actor].solid_id;
        assert(runtime.DynamicGeometry().ActorIndexBySolidId(solid_id) == actor);
        assert(runtime.DynamicGeometry().FindViewBySolidId(solid_id) ==
               &runtime.DynamicGeometry().Views()[actor]);
    }
    const Room& room = runtime.Active()->room;
    assert(room.dynamic_solids == &runtime.DynamicGeometry());

    FaceProbe wall{};
    assert(FindFace(runtime, true, false, false, wall));
    const Vec3 wall_origin = Add(wall.center, Vec3{wall.normal.x * 3.0f,
        wall.normal.y * 3.0f, wall.normal.z * 3.0f});
    GroundHit initial = RaycastRoomSource(room, wall_origin,
        {-wall.normal.x, -wall.normal.y, -wall.normal.z}, 6.0f);
    assert(initial.hit && initial.face_id == wall.stable_face_id);
    const GroundHit backface = RaycastRoomSource(
        room, Add(wall.center, {-wall.normal.x * 3.0f,
                                -wall.normal.y * 3.0f,
                                -wall.normal.z * 3.0f}),
        wall.normal, 6.0f);
    assert(!backface.hit || backface.face_id != wall.stable_face_id);

    WallHit plane_hits[kMaxWallHits]{};
    const Vec3 wall_probe = Add(wall.center, Vec3{wall.normal.x * 2.0f,
        wall.normal.y * 2.0f, wall.normal.z * 2.0f});
    const int initial_wall_count = QueryWallsFacePlane(
        room, wall_probe, 4.0f, plane_hits, kMaxWallHits);
    int matching_face_hits = 0;
    for (int i = 0; i < initial_wall_count; ++i)
        matching_face_hits += plane_hits[i].face_id == wall.stable_face_id;
    assert(matching_face_hits == 1);  // static proxy + DSOL never double-hit
    const WallHit climb_wall = QueryClimbWall(room, wall_probe, 4.0f,
                                               wall.normal);
    assert(climb_wall.hit && climb_wall.face_id == wall.stable_face_id);
    const int sphere_wall_count = QueryWalls(
        room, wall_probe, 4.0f, plane_hits, kMaxWallHits);
    matching_face_hits = 0;
    for (int i = 0; i < sphere_wall_count; ++i)
        matching_face_hits += plane_hits[i].face_id == wall.stable_face_id;
    assert(matching_face_hits == 1);

    FaceProbe floor{};
    assert(FindFace(runtime, false, true, false, floor));
    const GroundHit floor_hit = QueryFloorSource(
        room, Add(floor.center, {0.0f, 4.0f, 0.0f}), 8.0f);
    assert(floor_hit.hit && floor_hit.face_id == floor.stable_face_id);

    FaceProbe ceiling{};
    assert(FindFace(runtime, false, false, true, ceiling));
    const CeilingHit ceiling_hit = QueryCeilingSource(
        room, Add(ceiling.center, {0.0f, -4.0f, 0.0f}), 8.0f);
    assert(ceiling_hit.hit && ceiling_hit.face_id == ceiling.stable_face_id);

    bool tested_oneway = false;
    const DynamicSolidGeometry& dynamic_geometry = runtime.DynamicGeometry();
    for (uint32_t i = 0; i < dynamic_geometry.FaceCount() && !tested_oneway; ++i) {
        const DynamicSolidFace& face = dynamic_geometry.Faces()[i];
        if (!(face.collision_material & physics::MAT_ONEWAY) ||
            face.vertex_count < 3 || face.cms_source_face_id == 0) continue;
        const Vec3 raw = physics::Cross(
            physics::Sub(dynamic_geometry.Vertices()[face.first_vertex + 1].position,
                         dynamic_geometry.Vertices()[face.first_vertex].position),
            physics::Sub(dynamic_geometry.Vertices()[face.first_vertex + 2].position,
                         dynamic_geometry.Vertices()[face.first_vertex].position));
        if (physics::Norm(raw).y < 0.95f) continue;
        const DynamicSolidSpec* owner = nullptr;
        for (uint16_t ai = 0; ai < dynamic_geometry.ActorCount(); ++ai) {
            const DynamicSolidSpec& actor = dynamic_geometry.Actors()[ai];
            if (i >= actor.dsol_first_face &&
                i < actor.dsol_first_face + actor.dsol_face_count) {
                owner = &actor;
                break;
            }
        }
        assert(owner);
        const int face_id = physics::SourceFaceTriangleOf(
            *runtime.GlobalCollision().Mesh(), face.cms_source_face_id);
        assert(face_id >= 0);
        const DynamicSolidView& view = *dynamic_geometry.FindViewBySourceId(
            owner->source_id);
        Vec3 local_center{};
        for (uint32_t v = 0; v < face.vertex_count; ++v) {
            const Vec3& p = dynamic_geometry.Vertices()[face.first_vertex + v].position;
            local_center.x += p.x / face.vertex_count;
            local_center.y += p.y / face.vertex_count;
            local_center.z += p.z / face.vertex_count;
        }
        const Vec3 world_center = ToWorldPoint(local_center, view);
        const GroundHit from_above = QueryFloorSource(
            room, Add(world_center, {0.0f, 1.0f, 0.0f}), 2.0f);
        const CeilingHit from_below = QueryCeilingSource(
            room, Add(world_center, {0.0f, -1.0f, 0.0f}), 2.0f);
        assert(from_above.hit && from_above.face_id == face_id);
        assert(!from_below.hit || from_below.face_id != face_id);
        tested_oneway = true;
    }

    const physics::SweepSphereHit sweep = SweepSphereRoom(
        room, Add(wall.center, {wall.normal.x * 5.0f,
                                  wall.normal.y * 5.0f,
                                  wall.normal.z * 5.0f}),
        {-wall.normal.x, -wall.normal.y, -wall.normal.z}, 0.25f, 10.0f);
    assert(sweep.hit && sweep.face_id == wall.stable_face_id);

    const DynamicSolidView before = runtime.DynamicGeometry().Views()[wall.actor];
    const Vec3 moved_position = Add(before.position, {250.0f, 7.0f, -130.0f});
    const float moved_yaw = before.yaw_radians + 0.37f;
    assert(runtime.DynamicGeometry().SetTransform(
        runtime.DynamicGeometry().Actors()[wall.actor].source_id,
        moved_position, moved_yaw));
    const DynamicSolidView& after = runtime.DynamicGeometry().Views()[wall.actor];
    assert(after.reinsert_ordinal > before.reinsert_ordinal);
    assert(after.bounds.min.x > before.bounds.min.x + 200.0f);
    const GroundHit old_pose = RaycastRoomSource(room, wall_origin,
        {-wall.normal.x, -wall.normal.y, -wall.normal.z}, 6.0f);
    assert(!old_pose.hit || old_pose.face_id != wall.stable_face_id);
    const int old_wall_count = QueryWallsFacePlane(
        room, wall_probe, 4.0f, plane_hits, kMaxWallHits);
    for (int i = 0; i < old_wall_count; ++i)
        assert(plane_hits[i].face_id != wall.stable_face_id);

    const DynamicSolidFace& moved_face = runtime.DynamicGeometry().Faces()[wall.face];
    const DynamicSolidVertex* vertices = runtime.DynamicGeometry().Vertices();
    const Vec3 local_a = vertices[moved_face.first_vertex].position;
    const Vec3 local_b = vertices[moved_face.first_vertex + 1].position;
    const Vec3 local_c = vertices[moved_face.first_vertex + 2].position;
    const Vec3 moved_normal = ToWorldVector(physics::Norm(physics::Cross(
        physics::Sub(local_b, local_a), physics::Sub(local_c, local_a))), after);
    Vec3 moved_center{};
    for (uint32_t v = 0; v < moved_face.vertex_count; ++v) {
        const Vec3 p = ToWorldPoint(
            vertices[moved_face.first_vertex + v].position, after);
        moved_center.x += p.x / moved_face.vertex_count;
        moved_center.y += p.y / moved_face.vertex_count;
        moved_center.z += p.z / moved_face.vertex_count;
    }
    const Vec3 moved_origin = Add(moved_center, {moved_normal.x * 3.0f,
        moved_normal.y * 3.0f, moved_normal.z * 3.0f});
    const GroundHit moved_hit = RaycastRoomSource(room, moved_origin,
        {-moved_normal.x, -moved_normal.y, -moved_normal.z}, 6.0f);
    assert(moved_hit.hit && moved_hit.face_id == wall.stable_face_id);
    const Vec3 moved_probe = Add(moved_center, {moved_normal.x * 2.0f,
        moved_normal.y * 2.0f, moved_normal.z * 2.0f});
    const int moved_wall_count = QueryWallsFacePlane(
        room, moved_probe, 4.0f, plane_hits, kMaxWallHits);
    bool moved_wall_found = false;
    for (int i = 0; i < moved_wall_count; ++i)
        moved_wall_found |= plane_hits[i].face_id == wall.stable_face_id;
    assert(moved_wall_found);
    const physics::SweepSphereHit moved_sweep = SweepSphereRoom(
        room, Add(moved_center, {moved_normal.x * 5.0f,
                                 moved_normal.y * 5.0f,
                                 moved_normal.z * 5.0f}),
        {-moved_normal.x, -moved_normal.y, -moved_normal.z}, 0.25f, 10.0f);
    assert(moved_sweep.hit && moved_sweep.face_id == wall.stable_face_id);

    // Same-cell ties use live remove/reinsert append order, including across
    // both positive and negative 100x100-grid wrap seams.
    LocalWallFace pair[2]{};
    assert(FindTwoLocalWallFaces(runtime.DynamicGeometry(), pair));
    const Vec3 positive_seam{20000.5f, 100.0f, -20000.5f};
    PlaceLocalWallFace(runtime.DynamicGeometry(), pair[0], positive_seam);
    PlaceLocalWallFace(runtime.DynamicGeometry(), pair[1], positive_seam);
    const DynamicSolidView& positive_view =
        runtime.DynamicGeometry().Views()[pair[0].actor];
    assert(std::floor((positive_view.bounds.min.x - 1.0f) / 200.0f) < 100.0f);
    assert(std::ceil((positive_view.bounds.max.x + 1.0f) / 200.0f) > 100.0f);
    WallHit ordered_hits[kMaxWallHits]{};
    int ordered_count = QueryWallsFacePlane(
        room, {positive_seam.x + 2.0f, positive_seam.y, positive_seam.z},
        4.0f, ordered_hits, kMaxWallHits);
    int first_a = FirstOwnerOrder(ordered_hits, ordered_count, pair[0].actor);
    int first_b = FirstOwnerOrder(ordered_hits, ordered_count, pair[1].actor);
    assert(first_a >= 0 && first_b >= 0 && first_a < first_b);
    WallHit capped_hit[1]{};
    assert(QueryWallsFacePlane(room,
        {positive_seam.x + 2.0f, positive_seam.y, positive_seam.z},
        4.0f, capped_hit, 1) == 1);
    assert(capped_hit[0].owner_id == (0x40000000 | pair[0].actor));

    const DynamicSolidView& a_view =
        runtime.DynamicGeometry().Views()[pair[0].actor];
    assert(runtime.DynamicGeometry().SetTransform(
        runtime.DynamicGeometry().Actors()[pair[0].actor].source_id,
        Add(a_view.position, {0.05f, 0.0f, 0.0f}), a_view.yaw_radians));
    ordered_count = QueryWallsFacePlane(
        room, {positive_seam.x + 2.0f, positive_seam.y, positive_seam.z},
        4.0f, ordered_hits, kMaxWallHits);
    first_a = FirstOwnerOrder(ordered_hits, ordered_count, pair[0].actor);
    first_b = FirstOwnerOrder(ordered_hits, ordered_count, pair[1].actor);
    assert(first_a >= 0 && first_b >= 0 && first_b < first_a);
    assert(QueryWallsFacePlane(room,
        {positive_seam.x + 2.0f, positive_seam.y, positive_seam.z},
        4.0f, capped_hit, 1) == 1);
    assert(capped_hit[0].owner_id == (0x40000000 | pair[1].actor));

    const Vec3 negative_seam{-0.5f, 100.0f, 0.5f};
    PlaceLocalWallFace(runtime.DynamicGeometry(), pair[1], negative_seam);
    PlaceLocalWallFace(runtime.DynamicGeometry(), pair[0], negative_seam);
    ordered_count = QueryWallsFacePlane(
        room, {negative_seam.x + 2.0f, negative_seam.y, negative_seam.z},
        4.0f, ordered_hits, kMaxWallHits);
    first_a = FirstOwnerOrder(ordered_hits, ordered_count, pair[0].actor);
    first_b = FirstOwnerOrder(ordered_hits, ordered_count, pair[1].actor);
    assert(first_a >= 0 && first_b >= 0 && first_b < first_a);

    // Static and dynamic faces at same probe merge by first shared query cell,
    // then the static CMSH insertion id vs live actor reinsertion serial.
    const physics::CollMesh* mesh = runtime.GlobalCollision().Mesh();
    bool checked_static_dynamic_order = false;
    bool checked_static_dynamic_ray_order = false;
    for (uint32_t tri = 0; tri < mesh->header->triangle_count &&
         (!checked_static_dynamic_order || !checked_static_dynamic_ray_order); ++tri) {
        const physics::CollTriangle& ct = mesh->triangles[tri];
        if (!(ct.material & physics::MAT_SOLID) ||
            (ct.material & physics::MAT_ONEWAY) || ct.source_face_id == 0) continue;
        const physics::CollSolidGridRecord* solid =
            physics::SolidGridRecordOf(*mesh, static_cast<int>(tri));
        if (!solid || runtime.DynamicGeometry().OwnsSolidId(solid->solid_id)) continue;
        const Vec3& a = mesh->world_verts[ct.i0];
        const Vec3& b = mesh->world_verts[ct.i1];
        const Vec3& c = mesh->world_verts[ct.i2];
        const Vec3 raw = physics::Cross(physics::Sub(b, a), physics::Sub(c, a));
        if (physics::Len2(raw) < 1e-8f) continue;
        const Vec3 normal = physics::Norm(raw);
        if (std::fabs(normal.y) > 0.01f) continue;
        const Vec3 center{(a.x + b.x + c.x) / 3.0f,
                          (a.y + b.y + c.y) / 3.0f,
                          (a.z + b.z + c.z) / 3.0f};
        PlaceLocalWallFace(runtime.DynamicGeometry(), pair[0], center, normal);
        const Vec3 probe = Add(center, {normal.x * 2.0f,
                                        normal.y * 2.0f,
                                        normal.z * 2.0f});
        const int count = QueryWallsFacePlane(
            room, probe, 4.0f, ordered_hits, kMaxWallHits);
        int static_index = -1;
        int dynamic_index = -1;
        for (int i = 0; i < count; ++i) {
            if (ordered_hits[i].owner_id == -1 && ordered_hits[i].face_id >= 0 &&
                mesh->triangles[ordered_hits[i].face_id].source_face_id ==
                    ct.source_face_id) static_index = i;
            if (ordered_hits[i].owner_id == (0x40000000 | pair[0].actor))
                dynamic_index = i;
        }
        if (static_index < 0 || dynamic_index < 0) continue;
        if (!checked_static_dynamic_order) {
            const DynamicSolidView& live =
                runtime.DynamicGeometry().Views()[pair[0].actor];
            const uint32_t static_cell = FirstSharedGridCell(
                solid->min_x, solid->min_y, solid->max_x, solid->max_y,
                probe, 4.0f);
            const uint32_t dynamic_cell = FirstSharedGridCell(
                live.bounds.min.x, -live.bounds.max.z,
                live.bounds.max.x, -live.bounds.min.z, probe, 4.0f);
            assert(static_cell != 0xFFFFFFFFu && dynamic_cell != 0xFFFFFFFFu);
            const bool static_first = static_cell != dynamic_cell
                ? static_cell < dynamic_cell
                : solid->solid_id < live.reinsert_ordinal;
            assert(static_first ? static_index < dynamic_index
                                : dynamic_index < static_index);
            assert(QueryWallsFacePlane(room, probe, 4.0f, capped_hit, 1) == 1);
            const bool capped_static = capped_hit[0].owner_id == -1 &&
                mesh->triangles[capped_hit[0].face_id].source_face_id ==
                    ct.source_face_id;
            const bool capped_dynamic = capped_hit[0].owner_id ==
                (0x40000000 | pair[0].actor);
            assert(static_first ? capped_static : capped_dynamic);
            checked_static_dynamic_order = true;
        }

        const Vec3 ray_origin = Add(center, {normal.x * 3.0f,
                                              normal.y * 3.0f,
                                              normal.z * 3.0f});
        const Vec3 ray_direction{-normal.x, -normal.y, -normal.z};
        constexpr float kRayLength = 6.0f;
        const Vec3 ray_end = Add(ray_origin, {ray_direction.x * kRayLength,
                                               ray_direction.y * kRayLength,
                                               ray_direction.z * kRayLength});
        const physics::TriHit static_ray = physics::SegmentTriangle(
            ray_origin, ray_end, a, b, c, physics::BackfaceCull::Cull);
        if (!static_ray.hit) continue;

        DynamicSolidGeometry& geometry = runtime.DynamicGeometry();
        const DynamicSolidFace& dynamic_face = geometry.Faces()[pair[0].face];
        if (!(dynamic_face.collision_material & physics::MAT_SOLID) ||
            dynamic_face.cms_source_face_id == 0 ||
            dynamic_face.collision_face_id == 0xFFFFu) continue;
        const DynamicSolidVertex* dynamic_vertices = geometry.Vertices();
        const float static_distance = static_ray.t * kRayLength;
        const auto closest_dynamic_distance = [&]() {
            const DynamicSolidView& view =
                geometry.Views()[pair[0].actor];
            const Vec3 local_origin = ToLocalPoint(ray_origin, view);
            const Vec3 local_end = ToLocalPoint(ray_end, view);
            float closest = kRayLength + 1.0f;
            for (uint32_t fan = 1; fan + 1 < dynamic_face.vertex_count; ++fan) {
                const physics::TriHit dynamic_ray = physics::SegmentTriangle(
                    local_origin, local_end,
                    dynamic_vertices[dynamic_face.first_vertex].position,
                    dynamic_vertices[dynamic_face.first_vertex + fan].position,
                    dynamic_vertices[dynamic_face.first_vertex + fan + 1].position,
                    physics::BackfaceCull::Cull);
                if (dynamic_ray.hit)
                    closest = std::fmin(closest, dynamic_ray.t * kRayLength);
            }
            return closest;
        };
        float dynamic_distance = closest_dynamic_distance();
        if (dynamic_distance > kRayLength) continue;

        // Move actor by sub-millimeter steps until both independently cast
        // candidates have exactly equal float distance. This drives actual
        // static+dynamic dispatch through OG's strict-tie order branch.
        for (int correction = 0;
             dynamic_distance != static_distance && correction < 8;
             ++correction) {
            const DynamicSolidView& view =
                geometry.Views()[pair[0].actor];
            const float shift = static_distance - dynamic_distance;
            const Vec3 position = Add(view.position, {
                ray_direction.x * shift,
                ray_direction.y * shift,
                ray_direction.z * shift});
            if (!geometry.SetTransform(
                    geometry.Actors()[pair[0].actor].source_id,
                    position, view.yaw_radians)) break;
            dynamic_distance = closest_dynamic_distance();
        }
        if (dynamic_distance != static_distance) continue;
        const DynamicSolidView& dynamic_view =
            geometry.Views()[pair[0].actor];

        const uint32_t static_ray_cell = FirstSharedRayGridCell(
            solid->min_x, solid->min_y, solid->max_x, solid->max_y,
            ray_origin, ray_direction, kRayLength);
        const uint32_t dynamic_ray_cell = FirstSharedRayGridCell(
            dynamic_view.bounds.min.x, -dynamic_view.bounds.max.z,
            dynamic_view.bounds.max.x, -dynamic_view.bounds.min.z,
            ray_origin, ray_direction, kRayLength);
        if (static_ray_cell == 0xFFFFFFFFu ||
            dynamic_ray_cell == 0xFFFFFFFFu) continue;
        const bool dynamic_later = dynamic_ray_cell != static_ray_cell
            ? dynamic_ray_cell > static_ray_cell
            : dynamic_view.reinsert_ordinal != solid->solid_id
                ? dynamic_view.reinsert_ordinal > solid->solid_id
                : dynamic_face.cms_source_face_id > ct.source_face_id;
        const bool expect_dynamic = dynamic_distance < static_distance ||
            (dynamic_distance == static_distance && dynamic_later);
        const GroundHit ray_hit = RaycastRoomSource(
            room, ray_origin, ray_direction, kRayLength,
            BackfacePolicy::Ignore);
        const bool got_dynamic = ray_hit.hit && ray_hit.owner_id ==
            (0x40000000 | pair[0].actor) &&
            ray_hit.face_id == dynamic_face.collision_face_id;
        const physics::CollSolidGridRecord* winning_solid = ray_hit.hit &&
            ray_hit.face_id >= 0
            ? physics::SolidGridRecordOf(*mesh, ray_hit.face_id) : nullptr;
        const bool got_static = ray_hit.hit && winning_solid &&
            winning_solid->solid_id == solid->solid_id;
        if (expect_dynamic ? !got_dynamic : !got_static) continue;
        std::printf("[dynamic-ray-tie] static=%.6f dynamic=%.6f winner=%s\n",
                    static_distance, dynamic_distance,
                    expect_dynamic ? "dynamic" : "static");
        checked_static_dynamic_ray_order = true;
    }
    assert(checked_static_dynamic_order);
    assert(checked_static_dynamic_ray_order);

    std::puts("PASS: dynamic_solid_query_smoke");
}
