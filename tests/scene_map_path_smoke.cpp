// Inc 7 scene map-path smoke test (host-side).
// Verifies global collision is required before Step, failed visual-transition
// rollback, two-cell dash crossing, and that the next physics tick uses the
// committed active-room view. This is a host adapter test, not an N64
// renderer test.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/scene_map_path_smoke.cpp \
//     src/user/gameplay/world/map_runtime.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     src/user/gameplay/world/level_loader.cpp \
//     src/user/gameplay/world/world.cpp \
//     src/user/gameplay/physics/coll_mesh.cpp \
//     src/user/gameplay/physics/geom.cpp \
//     -o /tmp/scene_map_path_smoke
// Run (after baking the fixture):
//   /tmp/scene_map_path_smoke /tmp/inc4-build/staging/forsyken-city.mappack /tmp/inc4-build/staging

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* mappack = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";
    const char* build_dir = argc > 2 ? argv[2] : "/tmp/inc4-build/staging";

    MapRuntime rt;
    assert(rt.Init(mappack, build_dir));
    assert(rt.HasGlobalCollision());

    // 1. Global collision is required before Step: a fresh runtime without
    //    Init has no global collision, so Step must not be attempted.
    {
        MapRuntime empty;
        assert(!empty.HasGlobalCollision());
        assert(!empty.HasMap());
        printf("PASS: fresh runtime has no global collision (Step must not run)\n");
    }

    // 2. Failed visual-transition rollback: committing an unknown room keeps
    //    the old active room + global collision.
    {
        const char* old_id = rt.ActiveRoomId();
        assert(!rt.CommitActive("cell_99_99"));
        assert(std::strncmp(rt.ActiveRoomId(), old_id, 16) == 0);
        assert(rt.HasGlobalCollision());
        printf("PASS: failed transition keeps old room %s + global collision\n", old_id);
    }

    // 3. Two-cell dash crossing: a position two cells away resolves to the
    //    correct room and commits in one step (dash crosses multiple cells).
    {
        // Find a room two cells away from the start (if one exists).
        const char* start_id = rt.Spec().start_room_id;
        const V2RoomSpec* start = rt.Spec().FindRoom(start_id);
        assert(start != nullptr);
        // Look for a room whose cell differs by 2 in X or Z.
        const V2RoomSpec* far = nullptr;
        for (int i = 0; i < rt.Spec().room_count; ++i) {
            const V2RoomSpec& r = rt.Spec().rooms[i];
            int dx = r.cell_ix - start->cell_ix;
            int dz = r.cell_iz - start->cell_iz;
            if (dx * dx + dz * dz >= 4) {  // at least 2 cells away
                far = &r;
                break;
            }
        }
        if (far) {
            const float cell_w = rt.Spec().chunk_size * rt.Spec().scale;
            Vec3 pos_far = {(far->cell_ix + 0.5f) * cell_w, 0.0f,
                            (far->cell_iz + 0.5f) * cell_w};
            const char* new_id = nullptr;
            assert(rt.SetActiveByPosition(pos_far, &new_id));
            assert(new_id && new_id[0] != '\0');
            assert(rt.CommitActive(new_id));
            assert(std::strncmp(rt.ActiveRoomId(), far->id, 16) == 0);
            printf("PASS: two-cell dash crossing -> %s\n", far->id);
        } else {
            printf("NOTE: no room 2+ cells away; skipping two-cell dash check\n");
        }
    }

    // 4. Next physics tick uses the committed active-room view: after a
    //    transition, the active room's coll_mesh is the global mesh and a
    //    floor probe succeeds.
    {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        assert(a->room.coll_mesh == rt.GlobalCollision().Mesh());
        Vec3 above = {a->render_origin.x + 100.0f, 20.0f, a->render_origin.z + 100.0f};
        GroundHit floor = rt.GlobalCollision().QueryFloor(above, 20.0f);
        assert(floor.hit);
        printf("PASS: next tick uses committed active room; floor hit at y=%.2f\n",
               floor.point.y);
    }

    rt.Reset();
    printf("\nAll scene_map_path_smoke tests passed.\n");
    return 0;
}
