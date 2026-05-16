#include "gameplay/world/actor_factory.hpp"
namespace madeline_cube {
ActorId SpawnActor(ActorWorld& world, const ActorSpawn& spawn, StrawberryActor& strawberry, RefillActor& refill, SpringActor& spring) {
    Actor* actor = nullptr;
    if (spawn.placeholder_id == 2) actor = &strawberry;
    else if (spawn.placeholder_id == 3) actor = &refill;
    else if (spawn.placeholder_id == 7) actor = &spring;
    if (!actor) return 0;
    actor->position = spawn.position;
    actor->placeholder_id = spawn.placeholder_id;
    return world.Add(*actor);
}
}
