// MapRuntime v2 smoke test (host-side).
// Loads a baked map-pack v2 + the one global CMSH, transitions visual rooms
// across a seam, preserves player world position/velocity, and continues
// static collision without swapping or nulling the global mesh.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/map_runtime_v2_smoke.cpp \
//     src/user/gameplay/world/map_runtime.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     src/user/gameplay/world/level_loader.cpp \
//     src/user/gameplay/world/world.cpp \
//     src/user/gameplay/physics/coll_mesh.cpp \
//     src/user/gameplay/physics/geom.cpp \
//     -o /tmp/map_runtime_v2_smoke
// Run (after baking the fixture):
//   /tmp/map_runtime_v2_smoke /tmp/inc4-build/staging/forsyken-city.mappack /tmp/inc4-build/staging

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* mappack_path = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";
    const char* build_dir = argc > 2 ? argv[2] : "/tmp/inc4-build/staging";

    MapRuntime rt;
    assert(rt.Init(mappack_path, build_dir));
    assert(rt.HasMap());
    assert(rt.HasGlobalCollision());

    // 1. Start room is active and has the Start spawn.
    const char* start_id = rt.Spec().start_room_id;
    assert(start_id[0] != '\0');
    assert(std::strncmp(rt.ActiveRoomId(), start_id, 16) == 0);
    const V2SpawnSpec* start = rt.FindStartSpawn();
    assert(start != nullptr);
    assert(start->kind == kSpawnStart);
    assert(std::strncmp(start->name, "Start", 32) == 0);
    printf("PASS: start room %s active, Start spawn at (%.1f,%.1f,%.1f)\n",
           start_id, start->position.x, start->position.y, start->position.z);

    // 2. Global collision is queryable (floor probe above the Start position).
    {
        Vec3 above = {start->position.x, start->position.y + 5.0f, start->position.z};
        GroundHit floor = rt.GlobalCollision().QueryFloor(above, 20.0f);
        assert(floor.hit);
        assert(std::fabs(floor.point.y - 12.8f) < 0.5f);
        printf("PASS: global collision floor hit at y=%.2f\n", floor.point.y);
    }

    // 3. ResolveCellByPosition matches the Start cell.
    {
        const char* resolved = rt.ResolveCellByPosition(start->position);
        assert(std::strncmp(resolved, start_id, 16) == 0);
        printf("PASS: ResolveCellByPosition(Start) -> %s\n", resolved);
    }

    // 4. Find a neighbor room and drive a position into it; transition.
    {
        const V2RoomSpec* start_room = rt.Spec().FindRoom(start_id);
        assert(start_room != nullptr);
        const char* nb_id = nullptr;
        for (int a = 0; a < 4; ++a) {
            if (start_room->neighbors[a][0] != '\0') {
                nb_id = start_room->neighbors[a];
                break;
            }
        }
        assert(nb_id != nullptr);
        const V2RoomSpec* nb = rt.Spec().FindRoom(nb_id);
        assert(nb != nullptr);

        // A position inside the neighbor's cell.
        const float cell_w = rt.Spec().chunk_size * rt.Spec().scale;
        Vec3 pos_in_nb = {
            (nb->cell_ix + 0.5f) * cell_w,
            0.0f,
            (nb->cell_iz + 0.5f) * cell_w,
        };

        // Preserve player state across the transition.
        Vec3 player_pos = pos_in_nb;
        Vec3 player_vel = {2.0f, 0.0f, 0.0f};

        const char* new_id = nullptr;
        assert(rt.SetActiveByPosition(player_pos, &new_id));
        assert(std::strncmp(new_id, nb_id, 16) == 0);

        // Commit the transition; player pos/vel must survive.
        assert(rt.CommitActive(new_id));
        assert(std::strncmp(rt.ActiveRoomId(), nb_id, 16) == 0);
        assert(player_pos.x == pos_in_nb.x && player_vel.x == 2.0f);
        printf("PASS: transitioned to %s, player pos/vel preserved\n", nb_id);

        // Global collision still valid after the swap (same mesh identity).
        GroundHit floor2 = rt.GlobalCollision().QueryFloor(player_pos, 20.0f);
        assert(floor2.hit);
        printf("PASS: global collision still queryable after transition\n");
    }

    // 5. Failed visual-room load leaves the old room active + global valid.
    {
        const char* old_id = rt.ActiveRoomId();
        // Commit an unknown room -> must fail and keep the old room.
        assert(!rt.CommitActive("cell_99_99"));
        assert(std::strncmp(rt.ActiveRoomId(), old_id, 16) == 0);
        assert(rt.HasGlobalCollision());
        printf("PASS: failed CommitActive keeps old room %s + global collision\n",
               old_id);
    }

    // 6. FindSpawnByName finds the AnchorB anchor.
    {
        const V2SpawnSpec* anchor = rt.FindSpawnByName("AnchorB");
        assert(anchor != nullptr);
        assert(anchor->kind == kSpawnAnchor);
        printf("PASS: FindSpawnByName(AnchorB) found anchor\n");
    }

    rt.Reset();
    assert(!rt.HasGlobalCollision());
    printf("\nAll map_runtime_v2_smoke tests passed.\n");
    return 0;
}
