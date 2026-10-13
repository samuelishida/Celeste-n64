// ActorWorld-owned, non-aliasing actor instances and bounded lifecycle.
#include <cassert>
#include <cstdio>

#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/world/actor_factory.hpp"
#include "gameplay/world/entity_dispatch.hpp"

using namespace madeline_cube;

namespace {
struct TrackedActor : Actor {
    static int destroyed;
    ~TrackedActor() override { ++destroyed; }
};
int TrackedActor::destroyed = 0;

struct OrderedActor : Actor {
    OrderedActor(int value, int* events, int* count)
        : value(value), events(events), count(count) {}
    void Update(float) override { events[(*count)++] = value; }
    void LateUpdate(float) override { events[(*count)++] = value + 100; }
    int value;
    int* events;
    int* count;
};
}  // namespace

int main() {
    static_assert(sizeof(ActorWorld) < 7000,
                  "64-slot actor storage exceeded planned memory budget");

    // Registration/room iteration order can differ from OG entity order.
    // Player occupies source slot 20. Point actors must interleave on both
    // world passes while every Update finishes before any LateUpdate begins.
    {
        ActorWorld ordered_world;
        int events[10] = {};
        int event_count = 0;
        OrderedActor after(40, events, &event_count);
        OrderedActor middle(30, events, &event_count);
        OrderedActor first(10, events, &event_count);
        OrderedActor after_middle(25, events, &event_count);
        after.source_id = 40;
        middle.source_id = 30;
        first.source_id = 10;
        after_middle.source_id = 25;
        assert(ordered_world.Add(after) != 0);
        assert(ordered_world.Add(middle) != 0);
        assert(ordered_world.Add(first) != 0);
        assert(ordered_world.Add(after_middle) != 0);
        ordered_world.ResolvePending();
        constexpr uint32_t kPlayerSourceId = 20;
        const float dt = 1.0f / 60.0f;
        ordered_world.UpdateBefore(kPlayerSourceId, dt);
        events[event_count++] = 20;  // Player.Update
        ordered_world.UpdateAfter(kPlayerSourceId, dt);
        ordered_world.LateUpdateBefore(kPlayerSourceId, dt);
        events[event_count++] = 120; // Player.LateUpdate
        ordered_world.LateUpdateAfter(kPlayerSourceId, dt);
        const int expected[] = {10, 20, 25, 30, 40, 110, 120, 125, 130, 140};
        assert(event_count == 10);
        for (int i = 0; i < event_count; ++i) assert(events[i] == expected[i]);
    }

    ActorWorld world;
    ActorSpawn spawns[3] = {};
    for (int i = 0; i < 3; ++i) {
        spawns[i].placeholder_id = 3;
        spawns[i].source_id = static_cast<uint32_t>(100 + i);
        spawns[i].position = {static_cast<float>(i * 10), 2.0f, 3.0f};
    }
    spawns[1].actor_flags = 1;
    assert(DispatchActorSpawns(spawns, 3, world) == 3);
    world.ResolvePending();
    assert(world.Count() == 3);

    RefillActor* seen[3] = {};
    int count = 0;
    world.ForEachType<RefillActor>([&](RefillActor& actor) {
        assert(count < 3);
        seen[count++] = &actor;
    });
    assert(count == 3);
    assert(seen[0] != seen[1] && seen[1] != seen[2] && seen[0] != seen[2]);
    for (int i = 0; i < 3; ++i) {
        assert(seen[i]->source_id == static_cast<uint32_t>(100 + i));
        assert(seen[i]->position.x == static_cast<float>(i * 10));
        assert(seen[i]->IsDouble() == (i == 1));
    }

    // Dormant refill timers still tick; other inactive actors stay opt-in.
    RefillActor* dormant = seen[0];
    dormant->OnCollect();
    assert(!dormant->active);
    world.Update(4.1f);
    assert(dormant->active);
    assert(dormant->source_id == 100);
    assert(seen[2]->source_id == 102);

    const ActorId survivor_id = world.IdOf(seen[2]);
    world.Destroy(world.IdOf(seen[1]));
    world.ResolvePending();
    assert(world.Count() == 2);
    assert(world.IdOf(seen[2]) == survivor_id);
    assert(seen[2]->source_id == 102);

    StrawberryActor borrowed;
    assert(world.Add(borrowed) != 0);
    assert(world.Add(borrowed) == 0);  // duplicate external pointer rejected
    world.ResolvePending();
    assert(world.Count() == 3);
    world.Clear();
    assert(world.Count() == 0);
    assert(world.IdOf(&borrowed) == 0);

    {
        ActorWorld tracked_world;
        TrackedActor* tracked = tracked_world.EmplaceOwned<TrackedActor>();
        assert(tracked != nullptr);
        tracked_world.ResolvePending();
        tracked_world.Destroy(tracked_world.IdOf(tracked));
        tracked_world.ResolvePending();
        assert(TrackedActor::destroyed == 1);
        tracked_world.Clear();
        assert(TrackedActor::destroyed == 1);
    }

    ActorWorld full;
    for (int i = 0; i < ActorWorld::kMaxActors; ++i)
        assert(full.EmplaceOwned<SpringActor>() != nullptr);
    assert(full.EmplaceOwned<SpringActor>() == nullptr);
    full.ResolvePending();
    assert(full.Count() == ActorWorld::kMaxActors);

    std::puts("PASS: actor_owned_pool_smoke");
}
