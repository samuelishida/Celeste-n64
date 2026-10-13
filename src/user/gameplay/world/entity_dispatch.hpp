#pragma once

#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

// Spawns normalized actor records into unique ActorWorld-owned instances.
int DispatchActorSpawns(const ActorSpawn* spawns, int spawn_count,
                        ActorWorld& world);

// Translates legacy LVL entities, then spawns every supported record.
// placeholder_id in spawns holds the raw classname_id from the baked level;
// this function translates classname_id → actor. Unknown ids are logged and
// skipped; a zero return from SpawnActor is reported as capacity/flag failure.
int DispatchLevelEntities(const Room& room, ActorWorld& world);

}  // namespace madeline_cube
