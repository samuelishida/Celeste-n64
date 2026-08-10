// Inc 7 active-room scene contract test (host-side).
// Exercises an instrumented scene adapter that records the room pointer/id
// used by motor, camera, respawn, actor, and renderer calls; asserts they all
// match the active view before and after a transition.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/active_room_scene_contract.cpp \
//     src/user/gameplay/world/map_runtime.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     src/user/gameplay/world/level_loader.cpp \
//     src/user/gameplay/world/world.cpp \
//     src/user/gameplay/physics/coll_mesh.cpp \
//     src/user/gameplay/physics/geom.cpp \
//     -o /tmp/active_room_scene_contract
// Run (after baking the fixture):
//   /tmp/active_room_scene_contract /tmp/inc4-build/staging/forsyken-city.mappack /tmp/inc4-build/staging

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "gameplay/world/map_runtime.hpp"

using namespace madeline_cube;

// Instrumented scene adapter: records which room id each subsystem used.
struct SceneAdapter {
    MapRuntime rt;
    std::string motor_room;
    std::string camera_room;
    std::string respawn_room;
    std::string actor_room;
    std::string renderer_room;

    bool Init(const char* mappack, const char* build_dir) {
        return rt.Init(mappack, build_dir);
    }

    // Simulate one subsystem reading the active room.
    void MotorUsesActive() {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        motor_room = a->id;
    }
    void CameraUsesActive() {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        camera_room = a->id;
    }
    void RespawnUsesActive() {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        respawn_room = a->id;
    }
    void ActorUsesActive() {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        actor_room = a->id;
    }
    void RendererUsesActive() {
        const ActiveRoomView* a = rt.Active();
        assert(a != nullptr);
        renderer_room = a->id;
    }

    bool AllMatch(const char* expected) const {
        return motor_room == expected && camera_room == expected &&
               respawn_room == expected && actor_room == expected &&
               renderer_room == expected;
    }
};

int main(int argc, char** argv) {
    const char* mappack = argc > 1 ? argv[1]
        : "/tmp/inc4-build/staging/forsyken-city.mappack";
    const char* build_dir = argc > 2 ? argv[2] : "/tmp/inc4-build/staging";

    SceneAdapter scene;
    assert(scene.Init(mappack, build_dir));

    const char* start_id = scene.rt.Spec().start_room_id;
    assert(start_id[0] != '\0');

    // All subsystems read the active room (start room).
    scene.MotorUsesActive();
    scene.CameraUsesActive();
    scene.RespawnUsesActive();
    scene.ActorUsesActive();
    scene.RendererUsesActive();
    assert(scene.AllMatch(start_id));
    printf("PASS: all subsystems use active room %s\n", start_id);

    // Find a neighbor and transition.
    const V2RoomSpec* start_room = scene.rt.Spec().FindRoom(start_id);
    assert(start_room != nullptr);
    const char* nb_id = nullptr;
    for (int a = 0; a < 4; ++a) {
        if (start_room->neighbors[a][0] != '\0') {
            nb_id = start_room->neighbors[a];
            break;
        }
    }
    assert(nb_id != nullptr);

    // Drive a position into the neighbor and commit.
    const V2RoomSpec* nb = scene.rt.Spec().FindRoom(nb_id);
    assert(nb != nullptr);
    const float cell_w = scene.rt.Spec().chunk_size * scene.rt.Spec().scale;
    Vec3 pos_in_nb = {(nb->cell_ix + 0.5f) * cell_w, 0.0f,
                      (nb->cell_iz + 0.5f) * cell_w};
    const char* new_id = nullptr;
    assert(scene.rt.SetActiveByPosition(pos_in_nb, &new_id));
    assert(std::strncmp(new_id, nb_id, 16) == 0);
    assert(scene.rt.CommitActive(new_id));

    // After the transition, all subsystems must use the NEW active room.
    scene.MotorUsesActive();
    scene.CameraUsesActive();
    scene.RespawnUsesActive();
    scene.ActorUsesActive();
    scene.RendererUsesActive();
    assert(scene.AllMatch(nb_id));
    printf("PASS: after transition, all subsystems use active room %s\n", nb_id);

    // Global collision is still the same mesh (compatibility pointer).
    assert(scene.rt.Active()->room.coll_mesh == scene.rt.GlobalCollision().Mesh());
    printf("PASS: active room exposes the global collision mesh\n");

    scene.rt.Reset();
    printf("\nAll active_room_scene_contract tests passed.\n");
    return 0;
}
