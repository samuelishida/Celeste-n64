#pragma once

#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

// Spawns actors for every non-PlayerSpawn entity in room.spawns[].
// placeholder_id in spawns holds the raw classname_id from the baked level;
// this function translates classname_id → actor and calls ActorWorld::Add.
// Unknown classname_ids are logged once and skipped.
void DispatchLevelEntities(const Room& room, ActorWorld& world,
                           StrawberryActor& strawberry,
                           RefillActor& refill,
                           SpringActor& spring);

}  // namespace madeline_cube
