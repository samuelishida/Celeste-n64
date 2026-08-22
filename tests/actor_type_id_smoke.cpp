// Inc 4 actor type-id query smoke test (host-side).
// Verifies the type-id based ActorWorld::Get<T>() (n64-optimization Inc 4):
//   - returns the same actor the old dynamic_cast path would (same pointer)
//     for the live strawberry query,
//   - returns null when no actor of the requested type exists,
//   - pins the documented EXACT-type semantics (a base-type query does not
//     match a derived actor),
//   - asserts every concrete actor type-id constant is unique (a collision
//     would silently mis-resolve Get<>).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/actor_type_id_smoke.cpp \
//     src/user/gameplay/world/actor_world.cpp \
//     src/user/gameplay/actor/strawberry_actor.cpp \
//     src/user/gameplay/actor/refill_actor.cpp \
//     src/user/gameplay/actor/spring_actor.cpp \
//     -o /tmp/actor_type_id_smoke

#include <cassert>
#include <cstdio>

#include "gameplay/actor/bobbing_actor.hpp"
#include "gameplay/actor/cassette_actor.hpp"
#include "gameplay/actor/moving_solid_actor.hpp"
#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/solid_actor.hpp"
#include "gameplay/actor/spike_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/world/actor_world.hpp"

using namespace madeline_cube;

namespace {

// Assert every concrete actor type-id constant is pairwise unique. A
// collision here would silently mis-resolve ActorWorld::Get<> (exact-type
// compare), so the constants are pinned in one place (ActorTypeId) and this
// test guards them.
void CheckUniqueIds() {
    const uint16_t ids[] = {
        static_cast<uint16_t>(ActorTypeId::kActor),
        BobbingActor::kTypeId,
        StrawberryActor::kTypeId,
        RefillActor::kTypeId,
        SpringActor::kTypeId,
        SolidActor::kTypeId,
        MovingSolidActor::kTypeId,
        SpikeActor::kTypeId,
        CassetteActor::kTypeId,
    };
    const int n = sizeof(ids) / sizeof(ids[0]);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            assert(ids[i] != ids[j]);
        }
    }
    printf("PASS: %d actor type-id constants are unique\n", n);
}

// Each concrete class's constructor stamps its own kTypeId, and the base
// Actor defaults to kActor.
void CheckCtorStamps() {
    Actor base;
    BobbingActor bobbing;
    StrawberryActor strawberry;
    RefillActor refill;
    SpringActor spring;
    SolidActor solid;
    MovingSolidActor moving_solid;
    SpikeActor spike;
    CassetteActor cassette;

    assert(base.TypeId() == static_cast<uint16_t>(ActorTypeId::kActor));
    assert(bobbing.TypeId() == BobbingActor::kTypeId);
    assert(strawberry.TypeId() == StrawberryActor::kTypeId);
    assert(refill.TypeId() == RefillActor::kTypeId);
    assert(spring.TypeId() == SpringActor::kTypeId);
    assert(solid.TypeId() == SolidActor::kTypeId);
    assert(moving_solid.TypeId() == MovingSolidActor::kTypeId);
    assert(spike.TypeId() == SpikeActor::kTypeId);
    assert(cassette.TypeId() == CassetteActor::kTypeId);
    printf("PASS: constructors stamp the correct type id\n");
}

// The live query: Get<StrawberryActor>() returns the added strawberry (same
// pointer the old dynamic_cast path returned), and null when absent.
void CheckGetSemantics() {
    // Null when no actor of the type exists.
    ActorWorld empty;
    assert(empty.Get<StrawberryActor>() == nullptr);

    // Same pointer as the old dynamic_cast path for the live query.
    ActorWorld world;
    StrawberryActor berry;
    SpringActor spring;
    ActorId berry_id = world.Add(berry);
    ActorId spring_id = world.Add(spring);
    assert(berry_id != 0);
    assert(spring_id != 0);
    world.ResolvePending();  // activates both (calls their Init()).

    assert(world.Get<StrawberryActor>() == &berry);
    assert(world.Get<SpringActor>() == &spring);

    // Exact-type semantics (documented on Get<>): a base-type query does NOT
    // match a derived actor. The old dynamic_cast would have matched
    // StrawberryActor as a BobbingActor; the type-id compare is exact.
    assert(world.Get<BobbingActor>() == nullptr);
    printf("PASS: Get<> returns the same actor and pins exact-type semantics\n");
}

// A world holding only a spring still returns null for the strawberry query.
void CheckGetNullOnOtherType() {
    ActorWorld world;
    SpringActor spring;
    assert(world.Add(spring) != 0);
    world.ResolvePending();
    assert(world.Get<StrawberryActor>() == nullptr);
    assert(world.Get<SpringActor>() == &spring);
    printf("PASS: Get<> returns null when only a different type is present\n");
}

}  // namespace

int main() {
    CheckUniqueIds();
    CheckCtorStamps();
    CheckGetSemantics();
    CheckGetNullOnOtherType();
    printf("actor_type_id_smoke: all checks passed\n");
    return 0;
}
