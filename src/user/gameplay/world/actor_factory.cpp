#include "gameplay/world/actor_factory.hpp"

namespace madeline_cube {

ActorId SpawnActor(ActorWorld& world, const ActorSpawn& spawn) {
    Actor* actor = nullptr;
    if ((spawn.actor_flags & static_cast<uint8_t>(~0x01u)) != 0) return 0;
    if (spawn.actor_flags != 0 && spawn.placeholder_id != 3) return 0;

    if (spawn.placeholder_id == 2) {
        actor = world.EmplaceOwned<StrawberryActor>();
    } else if (spawn.placeholder_id == 3) {
        RefillActor* refill = world.EmplaceOwned<RefillActor>();
        if (refill) refill->SetDouble((spawn.actor_flags & 0x01u) != 0);
        actor = refill;
    } else if (spawn.placeholder_id == 7) {
        actor = world.EmplaceOwned<SpringActor>();
    }
    if (!actor) return 0;

    actor->position = spawn.position;
    actor->placeholder_id = spawn.placeholder_id;
    actor->source_id = spawn.source_id;
    return world.IdOf(actor);
}
}
