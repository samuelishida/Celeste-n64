#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/render/lod_math.hpp"
#include "gameplay/world/artifact_hash.hpp"

using namespace madeline_cube;

namespace {

void U8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void U16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void U32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void F32(std::vector<uint8_t>& b, float f) {
    union { float f; uint32_t u; } bits = {f};
    U32(b, bits.u);
}
void Vec(std::vector<uint8_t>& b, float x, float y, float z) {
    F32(b, x); F32(b, y); F32(b, z);
}

std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> b{'D', 'S', 'O', 'L'};
    U16(b, 1); U16(b, 1); U32(b, 1); U32(b, 3);
    U32(b, 9); U16(b, 2); U8(b, 1); U8(b, 0);
    Vec(b, 10, 20, 30); F32(b, 0.25f);
    U32(b, 0); U32(b, 1); U32(b, 0); U32(b, 3);
    U32(b, 9); U16(b, 0); U16(b, 0); U16(b, 1); U16(b, 7);
    U32(b, 0); U16(b, 3); U16(b, 0);
    Vec(b, 0, 0, 0); Vec(b, 0, 1, 0); F32(b, 0); F32(b, 0);
    Vec(b, 0, 0, 1); Vec(b, 0, 1, 0); F32(b, 1); F32(b, 0);
    Vec(b, 1, 0, 0); Vec(b, 0, 1, 0); F32(b, 0); F32(b, 1);
    return b;
}

std::string Write(const std::vector<uint8_t>& bytes) {
    char path[] = "/tmp/dynamic-solid-dsol-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
    return path;
}

}  // namespace

int main() {
    const auto bytes = Fixture();
    const std::string path = Write(bytes);
    MapSpecV2 spec{};
    spec.dynamic_solid_count = 1;
    spec.dynamic_solids[0].source_id = 9;
    spec.dynamic_solids[0].solid_id = 2;
    spec.dynamic_solids[0].kind = 1;
    spec.dynamic_solids[0].position = {10, 20, 30};
    spec.dynamic_solids[0].yaw_radians = 0.25f;
    spec.dynamic_solids[0].cms_first_source_face_id = 1;
    spec.dynamic_solids[0].cms_face_count = 1;
    spec.dynamic_solids[0].dsol_first_face = 0;
    spec.dynamic_solids[0].dsol_face_count = 1;
    spec.dynamic_solids[0].dsol_first_vertex = 0;
    spec.dynamic_solids[0].dsol_vertex_count = 3;
    assert(artifact_hash::Crc32File(path.c_str(), spec.dsol_crc32,
                                    spec.dsol_size));

    physics::CollHeader collision_header{};
    collision_header.triangle_count = 1;
    collision_header.solid_grid_count = 1;
    physics::CollTriangle collision_triangle{};
    collision_triangle.source_face_id = 1;
    collision_triangle.material = physics::MAT_SOLID | physics::MAT_ONEWAY;
    physics::CollSolidGridRecord collision_solid{};
    collision_solid.solid_id = 2;
    collision_solid.first_source_face_id = 1;
    collision_solid.last_source_face_id = 1;
    physics::CollMesh collision{};
    collision.header = &collision_header;
    collision.triangles = &collision_triangle;
    collision.solid_grid = &collision_solid;

    DynamicSolidGeometry geometry;
    assert(geometry.Load(path.c_str(), spec, collision));
    assert(geometry.IsLoaded() && geometry.ActorCount() == 1);
    assert(geometry.FaceCount() == 1 && geometry.VertexCount() == 3);
    assert(geometry.Actors()[0].source_id == 9);
    for (uint16_t actor = 0; actor < geometry.ActorCount(); ++actor) {
        assert(geometry.Actors()[actor].source_id ==
               geometry.Views()[actor].source_id);
    }
    assert(geometry.Faces()[0].cms_source_face_id == 1);
    assert(geometry.Faces()[0].material_id == 7);
    assert(geometry.Faces()[0].collision_material ==
           (physics::MAT_SOLID | physics::MAT_ONEWAY));
    assert(geometry.Vertices()[1].position.z == 1.0f);

    DynamicSolidView render_view = geometry.Views()[0];
    const Vec3 camera{2.0f, 3.0f, 4.0f};
    const Vec3 local{0.0f, 0.0f, 1.0f};
    const Vec3 render_origin = DynamicSolidRenderOrigin(render_view, camera);
    const Vec3 rendered = TransformDynamicSolidRenderPoint(
        render_view, local, camera);
    assert(std::fabs(rendered.x - (render_origin.x + std::sin(0.25f))) < 1e-6f);
    assert(std::fabs(rendered.y - render_origin.y) < 1e-6f);
    assert(std::fabs(rendered.z - (render_origin.z + std::cos(0.25f))) < 1e-6f);
    const AABB collision_bounds = render_view.bounds;
    render_view.visual_offset = {0.5f, -0.25f, 0.75f};
    const Vec3 shaken = TransformDynamicSolidRenderPoint(
        render_view, local, camera);
    assert(std::fabs((shaken.x - rendered.x) - 0.5f) < 1e-6f);
    assert(std::fabs((shaken.y - rendered.y) + 0.25f) < 1e-6f);
    assert(std::fabs((shaken.z - rendered.z) - 0.75f) < 1e-6f);
    assert(render_view.bounds.min.x == collision_bounds.min.x &&
           render_view.bounds.max.z == collision_bounds.max.z);
    render_view.visible = false;
    assert(!DynamicSolidVisibleInCamera(render_view, camera, {3, 3, 4},
                                        45.0f, 5.0f, 800.0f));
    assert(render_view.collidable);

    // Keep a rideable solid visible around camera look target even when its
    // narrow AABB sits just outside the conservative horizontal cone. Camera
    // target is player center + look-at height; player feet sit ~23u below it.
    render_view.visible = true;
    render_view.bounds = {{24.0f, -10.0f, -1.0f},
                          {26.0f, -8.0f, 1.0f}};
    assert(!CellAabbInNearCone({0.0f, 100.0f, -20.0f},
                               {0.0f, 12.0f, 0.0f}, 45.0f, 5.0f, 800.0f,
                               render_view.bounds));
    assert(DynamicSolidVisibleInCamera(render_view,
                                       {0.0f, 100.0f, -20.0f},
                                       {0.0f, 12.0f, 0.0f},
                                       45.0f, 5.0f, 800.0f));

    // A climbing camera pitches down. An XZ-only cone rejects this platform
    // as a near-90-degree lateral target even though its AABB intersects the
    // real 3D view frustum. Runtime visibility must follow the actual camera.
    render_view.bounds = {{70.0f, -11.0f, -1.0f},
                          {72.0f, -9.0f, 1.0f}};
    const Vec3 climbing_camera{0.0f, 100.0f, -20.0f};
    const Vec3 climbing_target{0.0f, 12.0f, 0.0f};
    const Vec3 camera_up{0.0f, 1.0f, 0.0f};
    assert(!CellAabbInNearCone(climbing_camera, climbing_target,
                              45.0f, 5.0f, 800.0f, render_view.bounds));
    assert(CellAabbInFrustum3D(climbing_camera, climbing_target, camera_up,
                               45.0f, 4.0f / 3.0f, 5.0f, 800.0f,
                               render_view.bounds));
    assert(DynamicSolidVisibleInCamera(render_view, climbing_camera,
                                       climbing_target, 45.0f, 5.0f, 800.0f));

    render_view.bounds = {{40.0f, -10.0f, -1.0f},
                          {42.0f, -8.0f, 1.0f}};
    assert(!CellAabbInNearCone({0.0f, 100.0f, -20.0f},
                               {0.0f, 12.0f, 0.0f}, 45.0f, 5.0f, 800.0f,
                               render_view.bounds));
    assert(DynamicSolidVisibleInCamera(render_view,
                                       {0.0f, 100.0f, -20.0f},
                                       {0.0f, 12.0f, 0.0f},
                                       45.0f, 5.0f, 800.0f));

    render_view.bounds = {{70.0f, -10.0f, -1.0f},
                          {72.0f, -8.0f, 1.0f}};
    assert(DynamicSolidVisibleInCamera(render_view,
                                       {0.0f, 100.0f, -20.0f},
                                       {0.0f, 12.0f, 0.0f},
                                       45.0f, 5.0f, 800.0f));
    render_view.bounds = {{180.0f, -10.0f, -1.0f},
                          {182.0f, -8.0f, 1.0f}};
    assert(!DynamicSolidVisibleInCamera(render_view,
                                        {0.0f, 100.0f, -20.0f},
                                        {0.0f, 12.0f, 0.0f},
                                        45.0f, 5.0f, 800.0f));

    // Camera is far behind the player: still draw the nearby rideable solid
    // even when it lies outside both the camera cone and target grace.
    render_view.bounds = {{70.0f, -10.0f, -1.0f},
                          {72.0f, -8.0f, 1.0f}};
    const Vec3 fast_player{71.0f, 2.0f, 0.0f};
    assert(DynamicSolidVisibleInCamera(render_view,
                                       {0.0f, 100.0f, -20.0f},
                                       {0.0f, 12.0f, 0.0f},
                                       45.0f, 5.0f, 800.0f,
                                       &fast_player));

    // A solid at the edge of the camera cone must not blink as the camera
    // rotates through the exact cull threshold. Enter with the normal margin;
    // retain visibility with a wider exit margin.
    DynamicSolidView edge_view = render_view;
    edge_view.visible = true;
    edge_view.bounds = {{99.0f, -2.0f, -2.0f},
                        {101.0f, 2.0f, 2.0f}};
    const Vec3 orbit_player{0.0f, 0.0f, 0.0f};
    const Vec3 orbit_target{0.0f, 12.0f, 0.0f};
    bool previous_sample_visible = false;
    float cull_edge = -1.0f;
    for (int i = 0; i <= 1800; ++i) {
        const float angle = static_cast<float>(i) * 0.001f;
        const Vec3 orbit_camera{-100.0f * std::cos(angle), 100.0f,
                                -100.0f * std::sin(angle)};
        const bool visible = DynamicSolidVisibleInCamera(
            edge_view, orbit_camera, orbit_target, 45.0f, 5.0f, 800.0f,
            &orbit_player, false);
        if (previous_sample_visible && !visible) {
            cull_edge = angle;
            break;
        }
        previous_sample_visible = visible;
    }
    assert(cull_edge > 0.0f);
    const float just_outside = cull_edge + 0.003f;
    const Vec3 outside_camera{-100.0f * std::cos(just_outside), 100.0f,
                              -100.0f * std::sin(just_outside)};
    assert(!DynamicSolidVisibleInCamera(edge_view, outside_camera,
                                        orbit_target, 45.0f, 5.0f, 800.0f,
                                        &orbit_player, false));
    assert(DynamicSolidVisibleInCamera(edge_view, outside_camera,
                                       orbit_target, 45.0f, 5.0f, 800.0f,
                                       &orbit_player, true));

    Room room{};
    room.coll_mesh = &collision;
    room.dynamic_solids = &geometry;
    const DynamicSolidView& view = geometry.Views()[0];
    const Vec3 local_center{1.0f / 3.0f, 0.0f, 1.0f / 3.0f};
    const float c = std::cos(view.yaw_radians);
    const float s = std::sin(view.yaw_radians);
    const Vec3 center{view.position.x + c * local_center.x + s * local_center.z,
                      view.position.y,
                      view.position.z - s * local_center.x + c * local_center.z};
    const GroundHit one_way_support = QueryFloorSource(
        room, {center.x, center.y + 1.0f, center.z}, 2.0f);
    assert(one_way_support.hit && one_way_support.face_id == 0);
    const CeilingHit one_way_backside = QueryCeilingSource(
        room, {center.x, center.y - 1.0f, center.z}, 2.0f);
    assert(!one_way_backside.hit);
    const physics::SweepSphereHit one_way_sweep = SweepSphereRoom(
        room, {center.x, center.y + 1.0f, center.z},
        {0.0f, -1.0f, 0.0f}, 0.1f, 2.0f);
    assert(one_way_sweep.hit && one_way_sweep.face_id == 0);
    WallHit one_way_walls[kMaxWallHits]{};
    assert(QueryWallsFacePlane(room, {center.x + 1.0f, center.y, center.z},
                               2.0f, one_way_walls, kMaxWallHits) == 0);

    auto malformed = bytes;
    malformed.push_back(0);
    const std::string bad_path = Write(malformed);
    assert(!geometry.Load(bad_path.c_str(), spec, collision));
    assert(geometry.IsLoaded() && geometry.FaceCount() == 1);
    std::remove(path.c_str());
    std::remove(bad_path.c_str());
    std::puts("PASS: dynamic_solid_geometry_smoke");
}
