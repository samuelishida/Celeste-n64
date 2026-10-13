#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(__GLIBC__) && !defined(N64)
#include <fenv.h>
#endif

#include "../src/user/gameplay/world/room_data.hpp"
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

Room MakeThinPlatformRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {0.0f, 0.0f, -8.0f}, .max = {2.0f, 0.0f, 8.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 10,
    };
    return room;
}

Room MakeThinWallRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {4.0f, 0.0f, -2.0f}, .max = {4.5f, 4.0f, 2.0f}},
        .solid = true,
        .face_id = 20,
    };
    return room;
}

void PrintDiagnostics(const char* label, const CollisionQueryDiagnostics& diag) {
    std::printf("[%s] floor_sweep_hits=%d floor_raycast_hits=%d wall_candidate_hits=%d\n",
                label, diag.floor_sweep_hits, diag.floor_raycast_hits, diag.wall_candidate_hits);
}

bool QueryContractFixtures() {
    using namespace physics;
    const Vec3 og{2, 7, 13};
    const Vec3 world = FromOgAxes(og), back = ToOgAxes(world);
    if (!Near(world.y, 13) || !Near(world.z, -7) || !Near(back.y, 7)) return false;
    MovementConfig config;
    const Vec3 center{3, 30, 0};
    const Vec3 feet = BodyFeet(center, config.player_half_height);
    if (!Near(BodyCenter(feet, config.player_half_height).y, center.y) ||
        !Near(ProbeAboveFeet(center, config.player_half_height, 3).y, feet.y+3)) return false;

    // A diagonal finite wall triangle: its AABB includes (z=8,y=8),
    // but the horizontal slice at y=8 ends at z=2. A plane/AABB proxy
    // would invent a contact six units outside actual geometry.
    CollHeader header{};
    header.quant_scale = 1;
    header.vertex_count = 3; header.triangle_count = 1; header.bvh_node_count = 1;
    CollTriangle triangle{0,2,1,MAT_SOLID|MAT_CLIMBABLE,0,0};
    CollBvhNode node{{0,0,0},{0,10,10},0,1};
    Vec3 vertices[3] = {{0,0,0},{0,0,10},{0,10,0}};
    CollMesh mesh{};
    mesh.header=&header; mesh.triangles=&triangle; mesh.bvh_nodes=&node; mesh.world_verts=vertices;
    Room room{}; room.coll_mesh=&mesh;
    CollisionQueryCounters counters{}; room.query_counters=&counters;
    const WallHit tangent = QueryClimbWall(room, {4,5,2}, 4, {1,0,0});
    if (!tangent.hit || tangent.face_id != 0 || !Near(tangent.pushout,0) || !Near(tangent.normal.x,1)) return false;
    if (QueryClimbWall(room, {3,8,8},4,{1,0,0}).hit ||
        QueryClimbWall(room, {3,10.01f,0},4,{1,0,0}).hit) return false;
    // Authored winding determines solid sidedness. Reversal rejects the
    // previous front-side point and accepts the opposite side with raw -X.
    triangle.i1=1; triangle.i2=2;
    const WallHit reversed = QueryClimbWall(room,{4,5,2},4,{1,0,0});
    if (reversed.hit) return false;
    const WallHit back_hit = QueryClimbWall(room,{-4,5,2},4,{-1,0,0});
    if (!back_hit.hit || !Near(back_hit.plane_normal.x,-1)) return false;
    triangle.i1=2; triangle.i2=1;
    // Anchor at body radius 3 is an equilibrium while grip reach 4 stays live.
    const WallHit rest = QueryWallPushout(room,{config.player_wall_radius,5,2},config.player_wall_radius);
    if (!rest.hit || !Near(rest.pushout,0) || !QueryClimbWall(room,{3,5,2},config.climb_check_dist,{1,0,0}).hit) return false;
    const float outside_ulp = std::nextafter(3.0f, 4.0f);
    const WallHit ulp = QueryWallPushout(room, {outside_ulp,5,2}, 3);
    if (ulp.hit) return false;
    if (QueryWallPushout(room, {3.001f,5,2}, 3).hit) return false;
    // Equal-distance faces choose stable id, independent of BVH leaf order.
    CollTriangle ties[2] = {triangle, triangle};
    ties[0].face_id=1; ties[1].face_id=0;
    header.triangle_count=2; node.count_or_zero=2; mesh.triangles=ties;
    const RayHit tie = RaycastMesh(mesh,{3,5,2},{-1,0,0},4);
    const SweepSphereHit sweep_tie = SweepSphereMesh(mesh,{3,5,2},{-1,0,0},3,1);
    if (!tie.hit || tie.face_id != 0 || !sweep_tie.hit || sweep_tie.face_id != 0) return false;
    header.triangle_count=1; node.count_or_zero=1; mesh.triangles=&triangle;
    // Two triangles from the same polygon fan: triangle zero's slice ends
    // at z=5, triangle one's slice contains z=8. OG chooses the nearest
    // triangle on the face BEFORE ranking face normals.
    Vec3 fan_vertices[4]={{0,0,0},{0,0,10},{0,10,0},{0,10,10}};
    CollTriangle fan[2]={{0,2,1,MAT_SOLID,0,0},{1,2,3,MAT_SOLID,1,0}};
    header.vertex_count=4; header.triangle_count=2; node.count_or_zero=2;
    mesh.world_verts=fan_vertices; mesh.triangles=fan;
    const WallHit fan_hit=QueryClimbWall(room,{3,5,8},5,{1,0,0});
    if (!fan_hit.hit || fan_hit.face_id != 1 || !Near(fan_hit.point.z,8)) return false;
    header.vertex_count=3; header.triangle_count=1; node.count_or_zero=1;
    mesh.world_verts=vertices; mesh.triangles=&triangle;
    triangle.material |= MAT_ONEWAY;
    if (QueryClimbWall(room,{3,5,2},4,{1,0,0}).hit) return false;
    triangle.material = MAT_SOLID;
    vertices[2]=vertices[1];
    if (QueryClimbWall(room,{3,0,2},4,{1,0,0}).hit) return false;
    // Grip issues exactly one BVH overlap, zero hidden ceiling rays per query.
    if (counters.raycasts != 0 || counters.overlap_queries != 12) return false;
    std::printf("[collision-query] contract: bounded slice, reversed winding, tangency, anchors, one-way, degenerate; overlaps=%u rays=%u\n", counters.overlap_queries,counters.raycasts);
    return true;
}

bool AuthoredNormalFixtures() {
    using namespace physics;
    CollHeader header{}; header.quant_scale=1; header.vertex_count=6;
    header.triangle_count=2; header.bvh_node_count=1;
    Vec3 vertices[6]={{0,0,0},{0,0,10},{0,10,0},
                      {-1,0,0},{-1,10,0},{-1,0,10}};
    CollTriangle triangles[2]={{0,1,2,MAT_SOLID,0,0},
                              {3,4,5,MAT_SOLID,1,0}};
    CollBvhNode node{{-1,0,0},{0,10,10},0,2};
    CollMesh mesh{}; mesh.header=&header; mesh.world_verts=vertices;
    mesh.triangles=triangles; mesh.bvh_nodes=&node;
    Room room{}; room.coll_mesh=&mesh;
    // Near face is a backface. It must be culled before nearest selection,
    // leaving the farther authored front face available to the gameplay ray.
    const GroundHit front=RaycastRoomSource(room,{3,5,2},{-1,0,0},5,BackfacePolicy::Ignore);
    const GroundHit both=RaycastRoomSource(room,{3,5,2},{-1,0,0},5,BackfacePolicy::Include);
    if (!front.hit || front.face_id!=1 || !Near(front.normal.x,1) ||
        !both.hit || both.face_id!=0 || !Near(both.normal.x,-1)) return false;
    const RayHit raw=RaycastMesh(mesh,{3,5,2},{-1,0,0},5,BackfaceCull::Ignore);
    if (!raw.hit || !Near(raw.normal.x,-1)) return false;
    WallHit ray{};
    if (!QueryWallRay(room,{3,5,2},{-2,0,0},3,&ray) || ray.face_id!=1 ||
        !Near(ray.point.x,-1)) return false; // preserve nonunit ray parameter

    // General walls admit sloped faces; ClimbCheck rejects only after ranking.
    Vec3 slope_vertices[3]={{-10,0,0},{10,0,0},{0,8,-6}};
    CollTriangle slope_triangle{0,1,2,MAT_SOLID,0,0};
    CollBvhNode slope_node{{-10,0,-6},{10,8,0},0,1};
    header.vertex_count=3; header.triangle_count=1;
    mesh.world_verts=slope_vertices; mesh.triangles=&slope_triangle; mesh.bvh_nodes=&slope_node;
    const WallHit slope=QueryWallPushout(room,{0,4,-2},3);
    if (!slope.hit || !Near(slope.plane_normal.y,.6f) ||
        QueryClimbWall(room,{0,4,-2},3,{0,.6f,.8f}).hit ||
        QueryWallPushout(room,{0,4,-4},3).hit) return false;
    if (!QueryWallRay(room,{0,4,-2},{0,-.6f,-.8f},3,&ray) ||
        !Near(ray.plane_normal.y,.6f)) return false;

    // Dynamic boxes use an actual horizontal face slice, not a sphere near
    // their top edge. General rays also resolve dynamic authored faces.
    Room dynamic{};
    dynamic.colliders[dynamic.collider_count++]={
        .type=ColliderType::Box,.bounds={.min={-2,0,-2},.max={2,10,2}},
        .solid=true,.face_id=20,
    };
    if (QueryClimbWall(dynamic,{3,10.01f,0},4,{1,0,0}).hit ||
        !QueryClimbWall(dynamic,{3,5,0},4,{1,0,0}).hit ||
        !QueryWallRay(dynamic,{3,5,0},{-1,0,0},4,&ray) ||
        ray.face_id!=20 || !Near(ray.plane_normal.x,1)) return false;
    std::printf("[collision-query] authored normals/front-back/nearest-before-cull/slope/dynamic-slice PASS\n");
    return true;
}

bool OgVertexSliceKeepsSourceEndpointOrder() {
    using namespace physics;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = 1;
    header.bvh_node_count = 1;
    // OG PlaneTriangleIntersection tests edges A-B, B-C, C-A and lets
    // each later hit overwrite line1. At y=0 these yield A, midpoint, A;
    // Foster Line.ClosestPoint returns A for the resulting zero-length line.
    // Query lies within radius of the complete slice, but not source endpoint A.
    Vec3 vertices[3] = {{0,0,0}, {1,10,0}, {0,-10,10}};
    CollTriangle triangle{0,1,2,MAT_SOLID,0,0};
    CollBvhNode node{{0,-10,0},{1,10,10},0,1};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = &triangle;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    const WallHit hit = QueryClimbWall(room, {3,0,5}, 3, {1,0,0});
    std::printf("[collision-query] OG vertex-slice source endpoint: hit=%d\n",
                static_cast<int>(hit.hit));
    return !hit.hit;
}

bool HorizontalSliceDoesNotDivideByZero() {
    using namespace physics;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = 1;
    header.bvh_node_count = 1;
    // Vertical wall has a horizontal C-A edge. Query at y=1 makes that edge
    // evaluate 1/0 in the old slice loop; C# rejects the resulting infinity,
    // but the N64 FPU traps. Other two edges define a valid finite slice.
    Vec3 vertices[3] = {{0,0,0}, {0,10,0}, {0,0,10}};
    CollTriangle triangle{0,1,2,MAT_SOLID,0,0};
    CollBvhNode node{{0,0,0},{0,10,10},0,1};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = &triangle;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

#if defined(__GLIBC__) && !defined(N64)
    fenv_t previous_environment{};
    fegetenv(&previous_environment);
    feenableexcept(FE_DIVBYZERO | FE_INVALID);
#endif
    const WallHit hit = QueryClimbWall(room, {3,1,0.2f}, 4, {1,0,0});
#if defined(__GLIBC__) && !defined(N64)
    fesetenv(&previous_environment);
#endif
    std::printf("[collision-query] horizontal slice avoids zero divide: hit=%d\n",
                static_cast<int>(hit.hit));
    return hit.hit && Near(hit.pushout, 1.0f);
}

bool OgWallReachBoundaryIsStrict() {
    using namespace physics;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = 1;
    header.bvh_node_count = 1;
    Vec3 vertices[3] = {{0,0,0}, {0,0,10}, {0,10,0}};
    CollTriangle triangle{0,2,1,MAT_SOLID,0,0};
    CollBvhNode node{{0,0,0},{0,10,10},0,1};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = &triangle;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    // OG rejects both at the plane-distance gate and after finite-slice
    // distance comparison. Broadphase may include them; acceptance may not.
    const float plane_overrun = std::nextafter(3.0f, 4.0f);
    const WallHit beyond_plane = QueryWallPushout(
        room, {plane_overrun,5,2}, 3);
    const WallHit beyond_slice = QueryWallPushout(
        room, {3,5,5.002f}, 3);
    std::printf("[collision-query] OG strict wall reach: plane=%d slice=%d\n",
                static_cast<int>(beyond_plane.hit),
                static_cast<int>(beyond_slice.hit));
    return !beyond_plane.hit && !beyond_slice.hit;
}

bool DistinctCoplanarSourceFaces() {
    using namespace physics;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 8;
    header.triangle_count = 4;
    header.bvh_node_count = 1;
    // Two separate authored quads share one plane and a seam at z=3. Their
    // triangle/BVH order is deliberately opposite canonical source-face order;
    // distinct coplanar polygons must remain distinct and tie ranking stable.
    Vec3 vertices[8] = {
        {0,0,3}, {0,10,3}, {0,10,10}, {0,0,10}, // source face 2
        {0,0,0}, {0,10,0}, {0,10,3}, {0,0,3},   // source face 1
    };
    CollTriangle triangles[4] = {
        {0,1,2,MAT_SOLID,0,2}, {0,2,3,MAT_SOLID,1,2},
        {4,5,6,MAT_SOLID,2,1}, {4,6,7,MAT_SOLID,3,1},
    };
    CollBvhNode node{{0,0,0},{0,10,10},0,4};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    WallHit hits[kMaxWallHits]{};
    const int count = QueryWallsFacePlane(room, {4,5,3}, 5, hits,
                                         kMaxWallHits, nullptr, false);
    bool saw_first = false;
    bool saw_second = false;
    for (int i = 0; i < count; ++i) {
        const uint16_t source_face = triangles[hits[i].face_id].source_face_id;
        saw_first |= source_face == 1;
        saw_second |= source_face == 2;
    }
    const WallHit nearest = QueryWallPushout(room, {4,5,3}, 5);
    const bool source_first_wins = nearest.hit &&
        triangles[nearest.face_id].source_face_id == 1 && Near(nearest.pushout, 1.0f);
    std::printf("[collision-query] coplanar source faces: hits=%d first=%d second=%d tie_first=%d\n",
                count, saw_first, saw_second, source_first_wins);
    return count == 2 && saw_first && saw_second && source_first_wins;
}

bool SourceOrderSurvivesLargeOverlap() {
    using namespace physics;
    constexpr int kTriangleCount = 257;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = kTriangleCount;
    header.bvh_node_count = 1;
    Vec3 vertices[3] = {{0,0,0}, {0,10,0}, {0,10,10}};
    CollTriangle triangles[kTriangleCount]{};
    for (int i = 0; i < kTriangleCount; ++i) {
        triangles[i] = {0,1,2,MAT_SOLID,static_cast<uint16_t>(i),
                        static_cast<uint16_t>(kTriangleCount - i)};
    }
    CollBvhNode node{{0,0,0},{0,10,10},0,kTriangleCount};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    WallHit hits[kMaxWallHits]{};
    const int count = QueryWallsFacePlane(room, {4,5,5}, 5, hits,
                                         kMaxWallHits);
    bool first_eight = count == kMaxWallHits;
    for (int i = 0; i < count; ++i) {
        const uint16_t expected = static_cast<uint16_t>(i + 1);
        first_eight = first_eight &&
            triangles[hits[i].face_id].source_face_id == expected;
    }
    const WallHit nearest = QueryWallPushout(room, {4,5,5}, 5);
    const bool earliest_wins = nearest.hit &&
        triangles[nearest.face_id].source_face_id == 1;
    std::printf("[collision-query] overlap >256: hits=%d first8=%d earliest=%d\n",
                count, first_eight, earliest_wins);
    return first_eight && earliest_wins;
}

bool SphereWallQueryRespectsHitCapacity() {
    using namespace physics;
    constexpr int kTriangleCount = kMaxWallHits + 1;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = kTriangleCount;
    header.bvh_node_count = 1;
    Vec3 vertices[3] = {{0,0,0}, {0,10,0}, {0,0,10}};
    CollTriangle triangles[kTriangleCount]{};
    for (int i = 0; i < kTriangleCount; ++i)
        triangles[i] = {0,1,2,MAT_SOLID,static_cast<uint16_t>(i),0};
    CollBvhNode node{{0,0,0},{0,10,10},0,kTriangleCount};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    WallHit hits[kMaxWallHits]{};
    const int count = QueryWalls(room, {4,5,5}, 5, hits, kMaxWallHits);
    std::printf("[collision-query] sphere wall capacity: count=%d capacity=%d\n",
                count, kMaxWallHits);
    return count == kMaxWallHits;
}

bool SphereWallQueryScansBeyondOldCandidateCap() {
    using namespace physics;
    constexpr int kTriangleCount = 257;
    CollHeader header{};
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = kTriangleCount;
    header.bvh_node_count = 1;
    Vec3 vertices[3] = {{0,0,0}, {0,10,0}, {0,10,10}};
    CollTriangle triangles[kTriangleCount]{};
    for (int i = 0; i < kTriangleCount; ++i) {
        // Reverse source order in BVH leaf. Face 1 is visited last, beyond the
        // historical 256-candidate cutoff, but must still rank first.
        triangles[i] = {0,1,2,MAT_SOLID,static_cast<uint16_t>(i),
                        static_cast<uint16_t>(kTriangleCount - i)};
    }
    CollBvhNode node{{0,0,0},{0,10,10},0,kTriangleCount};
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    Room room{};
    room.coll_mesh = &mesh;

    WallHit hits[kMaxWallHits]{};
    const int count = QueryWalls(room, {4,5,5}, 5, hits, kMaxWallHits);
    bool earliest_eight = count == kMaxWallHits;
    for (int i = 0; i < count; ++i) {
        const uint16_t expected = static_cast<uint16_t>(i + 1);
        earliest_eight = earliest_eight &&
            triangles[hits[i].face_id].source_face_id == expected;
    }
    std::printf("[collision-query] sphere overlap >256: hits=%d earliest8=%d\n",
                count, earliest_eight);
    return earliest_eight;
}

bool RayTieUsesLastOgGridCandidate() {
    using namespace physics;
    CollHeader header{};
    header.version = 2;
    header.quant_scale = 1.0f;
    header.vertex_count = 3;
    header.triangle_count = 2;
    header.bvh_node_count = 1;
    header.solid_grid_count = 2;
    Vec3 vertices[3] = {{0,0,0}, {10,0,10}, {10,0,0}};
    CollTriangle triangles[2] = {
        {0,1,2,MAT_SOLID,0,1},
        {0,1,2,MAT_SOLID,1,2},
    };
    CollBvhNode node{{0,0,0},{10,0,10},0,2};
    // Both solids occupy same GridPartition cell and produce identical ray
    // distance. OG enumerates solid 0 then solid 1; SolidRayCast's strict
    // farther-than rejection lets later solid 1 replace tied hit.
    CollSolidGridRecord solids[2] = {
        {0,1,1,0,0.0f,-10.0f,10.0f,0.0f},
        {1,2,2,0,0.0f,-10.0f,10.0f,0.0f},
    };
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    mesh.solid_grid = solids;
    Room room{};
    room.coll_mesh = &mesh;

    const GroundHit hit = RaycastRoomSource(
        room, {5,10,5}, {0,-1,0}, 10, BackfacePolicy::Ignore);
    std::printf("[collision-query] ray grid tie: face=%d expected=1\n", hit.face_id);
    return hit.hit && hit.face_id == 1;
}

bool RayNearerHitBeatsLaterGridCandidate() {
    using namespace physics;
    CollHeader header{};
    header.version = 2;
    header.quant_scale = 1.0f;
    header.vertex_count = 6;
    header.triangle_count = 2;
    header.bvh_node_count = 1;
    header.solid_grid_count = 2;
    constexpr float kNearOffset = 0.00005f;
    Vec3 vertices[6] = {
        {0,kNearOffset,0}, {10,kNearOffset,10}, {10,kNearOffset,0},
        {0,0,0}, {10,0,10}, {10,0,0},
    };
    CollTriangle triangles[2] = {
        {0,1,2,MAT_SOLID,0,1},
        {3,4,5,MAT_SOLID,1,2},
    };
    CollBvhNode node{{0,0,0},{10,1,10},0,2};
    CollSolidGridRecord solids[2] = {
        {0,1,1,0,0.0f,-10.0f,10.0f,0.0f},
        {1,2,2,0,0.0f,-10.0f,10.0f,0.0f},
    };
    CollMesh mesh{};
    mesh.header = &header;
    mesh.world_verts = vertices;
    mesh.triangles = triangles;
    mesh.bvh_nodes = &node;
    mesh.solid_grid = solids;
    Room room{};
    room.coll_mesh = &mesh;

    const GroundHit hit = RaycastRoomSource(
        room, {5,10,5}, {0,-1,0}, 10, BackfacePolicy::Ignore);
    std::printf("[collision-query] ray near tie: face=%d expected=0 delta=%.5f\n",
                hit.face_id, kNearOffset);
    return hit.hit && hit.face_id == 0 &&
           std::fabs(hit.distance - (10.0f - kNearOffset)) < 0.00001f;
}

bool RunSmokeFixtures() {
    bool ok = QueryContractFixtures() && HorizontalSliceDoesNotDivideByZero() &&
              AuthoredNormalFixtures() &&
              OgVertexSliceKeepsSourceEndpointOrder() &&
              OgWallReachBoundaryIsStrict() &&
              DistinctCoplanarSourceFaces() && SourceOrderSurvivesLargeOverlap() &&
              SphereWallQueryRespectsHitCapacity() &&
              SphereWallQueryScansBeyondOldCandidateCap() &&
              RayTieUsesLastOgGridCandidate() &&
              RayNearerHitBeatsLaterGridCandidate();

    {
        const Room room = BuildLedgeFixtureRoom();
        CollisionQueryDiagnostics diag;
        const GroundHit floor = ProbeFloorDebug(room, {0.5f, 2.0f, 0.0f}, 1.0f, 20.0f, 1.0f, &diag);
        // OG GroundCheck needs a real support hit; this is a bounded floor
        // collider, not a synthetic grounded flag. The expected point/normal
        // comes from BuildLedgeFixtureRoom's upper y=0 plane.
        ok = ok && floor.hit && floor.face_id == 0 && Near(floor.point.y, 0.0f) &&
             Near(floor.normal.y, 1.0f);
        std::printf("[collision-query] ledge_support: hit=%d face=%d y=%.3f\n",
                    (int)floor.hit, floor.face_id, floor.point.y);
        PrintDiagnostics("collision-query", diag);
    }

    {
        Room room = MakeThinWallRoom();
        CollisionQueryDiagnostics diag;
        WallHit hits[kMaxWallHits];
        const int count = QueryWalls(room, {2.0f, 2.0f, 0.0f}, 3.0f, hits, kMaxWallHits, &diag);
        const WallHit nearest = QueryWallNearest(room, {2.0f, 2.0f, 0.0f}, 3.0f);
        // A finite box wall is used here, so this checks an actual wall face
        // and stable face identity rather than an infinite plane surrogate.
        ok = ok && count > 0 && nearest.hit && nearest.face_id == 20 &&
             Near(std::fabs(nearest.plane_normal.x), 1.0f);
        std::printf("[collision-query] thin_wall: hits=%d nearest_pushout=%.3f face=%d\n",
                    count, nearest.pushout, nearest.face_id);
        PrintDiagnostics("collision-query", diag);
    }

    {
        const Room room = BuildSlopeFixtureRoom();
        CollisionQueryDiagnostics diag;
        const GroundHit floor = ProbeFloorDebug(room, {-25.0f, 60.0f, 0.0f}, 1.0f, 80.0f, 1.0f, &diag);
        ok = ok && floor.hit && Near(floor.normal.x, 0.70710678f) && Near(floor.normal.y, 0.70710678f);
        std::printf("[collision-query] slope_support: hit=%d face=%d normal=(%.3f,%.3f,%.3f)\n",
                    (int)floor.hit, floor.face_id, floor.normal.x, floor.normal.y, floor.normal.z);
        PrintDiagnostics("collision-query", diag);
    }

    return ok;
}

int DiagnoseSupportFailure() {
    const Room room = MakeThinPlatformRoom();
    CollisionQueryDiagnostics diag;
    const GroundHit support = ProbeFloorDebug(room, {2.5f, 5.0f, 0.0f}, 1.0f, 10.0f, 0.75f, &diag);
    if (support.hit) {
        std::printf("[collision-query] support_probe hit face=%d y=%.3f\n", support.face_id, support.point.y);
        PrintDiagnostics("collision-query", diag);
        return 0;
    }
    const char* source = diag.floor_sweep_hits == 0 && diag.floor_raycast_hits == 0
        ? "support detection"
        : "response";
    std::printf("[collision-query] support_failure_probe missed -> %s\n", source);
    PrintDiagnostics("collision-query", diag);
    return 1;
}

int DiagnoseWallFailure() {
    Room room = MakeThinWallRoom();
    CollisionQueryDiagnostics diag;
    WallHit hits[kMaxWallHits];
    const int count = QueryWalls(room, {3.1f, 2.0f, 0.0f}, 0.3f, hits, kMaxWallHits, &diag);
    if (count == 0) {
        std::printf("[collision-query] wall_failure_probe missed -> wall candidate generation\n");
        PrintDiagnostics("collision-query", diag);
        return 1;
    }
    const WallHit nearest = QueryWallNearest(room, {3.1f, 2.0f, 0.0f}, 0.3f);
    if (!nearest.hit || nearest.pushout <= 0.0f) {
        std::printf("[collision-query] wall_failure_probe candidate found but response failed\n");
        PrintDiagnostics("collision-query", diag);
        return 1;
    }
    std::printf("[collision-query] wall_failure_probe hit face=%d pushout=%.3f\n", nearest.face_id, nearest.pushout);
    PrintDiagnostics("collision-query", diag);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke_only = argc <= 1 || std::strcmp(argv[1], "--smoke") == 0;
    if (smoke_only) {
        const bool ok = RunSmokeFixtures();
        std::printf("[collision-query] smoke=%s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    if (std::strcmp(argv[1], "--diagnose-support") == 0) {
        return DiagnoseSupportFailure();
    }

    if (std::strcmp(argv[1], "--diagnose-wall") == 0) {
        return DiagnoseWallFailure();
    }

    std::fprintf(stderr, "usage: %s [--smoke|--diagnose-support|--diagnose-wall]\n", argv[0]);
    return 2;
}
