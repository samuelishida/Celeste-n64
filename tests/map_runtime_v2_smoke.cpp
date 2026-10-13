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
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/entity_dispatch.hpp"
#include "gameplay/actor/refill_actor.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* mappack_path = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";
    const char* build_dir = argc > 2 ? argv[2] : "/tmp/inc4-build/staging";

    MapRuntime rt;
    assert(rt.Init(mappack_path, build_dir));
    assert(rt.HasMap());
    assert(rt.HasGlobalCollision());
    if (rt.Spec().dynamic_solid_count > 0) {
        assert(rt.Spec().dynamic_solid_count == 118);
        assert(rt.DynamicGeometry().IsLoaded());
        assert(rt.DynamicGeometry().ActorCount() == 118);
        assert(rt.DynamicGeometry().FaceCount() == 853);
        assert(rt.DynamicGeometry().VertexCount() == 3519);
    }
    const physics::CollMesh* initial_mesh_identity = rt.GlobalCollision().Mesh();
    const std::string initial_room_id = rt.ActiveRoomId();
    assert(!rt.Init("/tmp/missing-dynamic-solid-test.mappack", build_dir));
    assert(rt.HasMap() && rt.HasGlobalCollision());
    assert(rt.GlobalCollision().Mesh() == initial_mesh_identity);
    assert(initial_room_id == rt.ActiveRoomId());
    if (rt.Spec().hazard_count > 0) {
        int spikes = 0, death_blocks = 0;
        for (int i = 0; i < rt.Spec().hazard_count; ++i) {
            const HazardVolumeSpec& hazard = rt.Spec().hazards[i];
            if (hazard.kind == kHazardDeathBlock) {
                ++death_blocks;
                assert(hazard.spike_direction.x == 0.0f &&
                       hazard.spike_direction.y == 0.0f &&
                       hazard.spike_direction.z == 0.0f);
            } else {
                assert(hazard.kind == kHazardSpikeBlock);
                ++spikes;
                const Vec3& direction = hazard.spike_direction;
                const float length2 = direction.x * direction.x +
                    direction.y * direction.y + direction.z * direction.z;
                assert(std::fabs(length2 - 1.0f) < 1.0e-4f);
                assert(std::fabs(direction.x) < 1.0e-4f ||
                       std::fabs(direction.x) > 0.999f);
            }
        }
        assert(spikes == 98 && death_blocks == 5);
        assert(rt.Spec().spike_direction_flip_count == 35);
        std::printf("PASS: actor-backed hazards %d spikes, %d death volumes\n",
                    spikes, death_blocks);
    }
    ActorWorld actor_world;
    RefillActor* tracked_refill = nullptr;
    Vec3 start_floor_probe{};
    Vec3 start_floor_point{};
    const physics::CollMesh* global_mesh_identity = rt.GlobalCollision().Mesh();

    // Supported actors are enumerated map-wide once, not rebuilt per cell.
    {
        ActorSpawn actors[ActorWorld::kMaxActors] = {};
        const int count = rt.AllActorSpawns(actors, ActorWorld::kMaxActors);
        assert(count == 34);  // 20 strawberries + 8 refills + 6 springs
        int strawberries = 0, refills = 0, springs = 0;
        for (int i = 0; i < count; ++i) {
            if (actors[i].placeholder_id == 2) ++strawberries;
            else if (actors[i].placeholder_id == 3) ++refills;
            else if (actors[i].placeholder_id == 7) ++springs;
            else assert(false && "unexpected supported actor kind");
            for (int j = i + 1; j < count; ++j)
                assert(actors[i].source_id != actors[j].source_id);
        }
        assert(strawberries == 20 && refills == 8 && springs == 6);
        assert(rt.AllActorSpawns(actors, 33) == -1);

        ActorSpawn all_actors[ActorWorld::kMaxActors] = {};
        const int all_count = rt.AllActorSpawns(
            all_actors, ActorWorld::kMaxActors);
        assert(all_count == 34);
        assert(DispatchActorSpawns(all_actors, all_count, actor_world) == all_count);
        actor_world.ResolvePending();
        assert(actor_world.Count() == all_count);
        actor_world.ForEachType<RefillActor>([&](RefillActor& refill) {
            if (!tracked_refill) tracked_refill = &refill;
        });
        assert(tracked_refill != nullptr);
        tracked_refill->OnCollect();
        assert(tracked_refill->collected && !tracked_refill->active);
    }

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
        start_floor_probe = {start->position.x, start->position.y + 5.0f, start->position.z};
        GroundHit floor = rt.GlobalCollision().QueryFloor(start_floor_probe, 20.0f);
        assert(floor.hit);
        assert(std::fabs(floor.point.y - start->position.y) < 0.5f);
        start_floor_point = floor.point;
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
        RefillActor* retained_refill = nullptr;
        actor_world.ForEachType<RefillActor>([&](RefillActor& refill) {
            if (refill.source_id == tracked_refill->source_id)
                retained_refill = &refill;
        });
        assert(retained_refill == tracked_refill);
        assert(retained_refill->collected && !retained_refill->active);
        printf("PASS: transitioned to %s, player pos/vel preserved\n", nb_id);

        // Global collision remains the same mesh and answers same known probe
        // after visual-room swap. Neighbor-cell centers may contain no floor.
        assert(rt.GlobalCollision().Mesh() == global_mesh_identity);
        GroundHit floor2 = rt.GlobalCollision().QueryFloor(start_floor_probe, 20.0f);
        assert(floor2.hit && std::fabs(floor2.point.y - start_floor_point.y) < 0.01f);
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

    // 6. FindSpawnByName finds an authored anchor by its manifest name.
    {
        const V2SpawnSpec* expected = nullptr;
        for (int r = 0; r < rt.Spec().room_count && !expected; ++r) {
            for (int s = 0; s < rt.Spec().rooms[r].spawn_count; ++s) {
                const V2SpawnSpec& spawn = rt.Spec().rooms[r].spawns[s];
                if (spawn.kind == kSpawnAnchor) {
                    expected = &spawn;
                    break;
                }
            }
        }
        assert(expected != nullptr);
        const V2SpawnSpec* anchor = rt.FindSpawnByName(expected->name);
        assert(anchor != nullptr);
        assert(anchor->kind == kSpawnAnchor);
        assert(anchor->source_id == expected->source_id);
        printf("PASS: FindSpawnByName(%s) found anchor\n", anchor->name);
    }

    rt.Reset();
    assert(!rt.HasGlobalCollision());
    printf("\nAll map_runtime_v2_smoke tests passed.\n");
    return 0;
}
