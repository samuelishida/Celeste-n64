#pragma once
#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/world.hpp"
namespace madeline_cube {
ActorId SpawnActor(ActorWorld& world, const ActorSpawn& spawn, StrawberryActor& strawberry, RefillActor& refill, SpringActor& spring);
}
