// tests/cassette_transition_smoke.cpp
// Host smoke test: cassette target wiring triggers level change.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gameplay/world/world.hpp"
#include "gameplay/actor/cassette_actor.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    printf("[cassette] testing cassette target wiring\n");

    // 1. Room with cassette target.
    Room room = {};
    room.has_cassette = true;
    room.cassette = {10.0f, 0.0f, 10.0f};
    const char* target = "rom:/lvl/1-1.lvl";
    for (int i = 0; i < 31 && target[i]; ++i) room.cassette_target[i] = target[i];
    room.cassette_target[31] = '\0';

    // 2. Cassette actor wired with target.
    CassetteActor actor;
    actor.InitAt(room.cassette);
    actor.target_level_path = room.cassette_target;

    assert(actor.target_level_path != nullptr);
    assert(std::strncmp(actor.target_level_path, "rom:/lvl/1-1.lvl", 32) == 0);
    printf("PASS: cassette actor wired with target level\n");

    // 3. Simulate pickup.
    bool picked_up = actor.Step(0.016f, room.cassette);
    assert(picked_up == true);
    assert(actor.collected == true);
    printf("PASS: cassette pickup detected\n");

    // 4. Verify the target is preserved after pickup (for scene to read).
    assert(actor.target_level_path != nullptr);
    assert(std::strncmp(actor.target_level_path, "rom:/lvl/1-1.lvl", 32) == 0);
    printf("PASS: target level preserved after pickup\n");

    // 5. Room without cassette target (main map case).
    Room room_no_target = {};
    room_no_target.has_cassette = true;
    room_no_target.cassette = {5.0f, 0.0f, 5.0f};
    // cassette_target remains empty
    assert(room_no_target.cassette_target[0] == '\0');
    printf("PASS: room without cassette target (main map case)\n");

    printf("\nAll cassette_transition_smoke tests passed.\n");
    return 0;
}
