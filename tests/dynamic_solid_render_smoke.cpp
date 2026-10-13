#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <tuple>

#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

namespace {

bool Close(float a, float b) { return std::fabs(a - b) < 2e-4f; }

bool SameBounds(const AABB& a, const AABB& b) {
    return Close(a.min.x, b.min.x) && Close(a.min.y, b.min.y) &&
           Close(a.min.z, b.min.z) && Close(a.max.x, b.max.x) &&
           Close(a.max.y, b.max.y) && Close(a.max.z, b.max.z);
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    MapRuntime runtime;
    assert(runtime.Init(argv[1], argv[2]));
    const DynamicSolidGeometry& geometry = runtime.DynamicGeometry();
    assert(geometry.IsLoaded());
    assert(geometry.ActorCount() == 118);
    assert(geometry.FaceCount() == 853);
    assert(geometry.VertexCount() == 3519);

    std::set<std::tuple<uint32_t, uint16_t, uint16_t>> source_faces;
    const Vec3 camera{123.0f, 45.0f, -67.0f};
    uint32_t checked_vertices = 0;
    for (uint16_t ai = 0; ai < geometry.ActorCount(); ++ai) {
        const DynamicSolidSpec& actor = geometry.Actors()[ai];
        const DynamicSolidView& view = geometry.Views()[ai];
        assert(view.source_id == actor.source_id);
        assert(view.solid_id == actor.solid_id);
        assert(view.visible && view.collidable && !view.update_off_screen);
        assert(Close(view.position.x, actor.position.x));
        assert(Close(view.position.y, actor.position.y));
        assert(Close(view.position.z, actor.position.z));
        assert(Close(view.yaw_radians, actor.yaw_radians));

        const AABB collision_bounds = view.bounds;
        DynamicSolidView visual = view;
        visual.visual_offset = {0.75f, -0.5f, 1.25f};
        assert(SameBounds(visual.bounds, collision_bounds));
        visual.visible = false;
        visual.update_off_screen = true;
        assert(!DynamicSolidVisibleInCamera(visual, camera, {124, 45, -67},
                                            45, 5, 800));
        assert(visual.collidable);  // hidden render state does not disable collision
        assert(visual.update_off_screen);  // update policy is independent too

        const float c = std::cos(actor.yaw_radians);
        const float s = std::sin(actor.yaw_radians);
        for (uint32_t fi = actor.dsol_first_face;
             fi < actor.dsol_first_face + actor.dsol_face_count; ++fi) {
            const DynamicSolidFace& face = geometry.Faces()[fi];
            assert(face.source_id == actor.source_id);
            assert(face.vertex_count >= 3 && face.vertex_count <= 68);
            assert(source_faces.emplace(face.source_id, face.brush_index,
                                        face.face_index).second);
            for (uint32_t vi = face.first_vertex;
                 vi < face.first_vertex + face.vertex_count; ++vi) {
                const DynamicSolidVertex& vertex = geometry.Vertices()[vi];
                const Vec3 rendered = TransformDynamicSolidRenderPoint(
                    view, vertex.position, camera);
                const Vec3 expected{
                    actor.position.x + c * vertex.position.x + s * vertex.position.z - camera.x,
                    actor.position.y + vertex.position.y - camera.y,
                    actor.position.z - s * vertex.position.x + c * vertex.position.z - camera.z};
                assert(Close(rendered.x, expected.x));
                assert(Close(rendered.y, expected.y));
                assert(Close(rendered.z, expected.z));
                assert(vertex.u * 32.0f >= -32768.0f &&
                       vertex.u * 32.0f <= 32767.0f);
                assert(vertex.v * 32.0f >= -32768.0f &&
                       vertex.v * 32.0f <= 32767.0f);
                ++checked_vertices;
            }
        }
    }
    assert(source_faces.size() == geometry.FaceCount());
    assert(checked_vertices == geometry.VertexCount());
    std::printf("PASS: dynamic_solid_render_smoke actors=%u faces=%lu vertices=%u\n",
                geometry.ActorCount(), static_cast<unsigned long>(source_faces.size()),
                checked_vertices);
}
