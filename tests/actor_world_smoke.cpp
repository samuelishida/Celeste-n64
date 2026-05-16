#include <cassert>
#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/actor_factory.hpp"
using namespace madeline_cube;
struct Recyclable : Actor { bool recycled=false; void OnRecycle() override { recycled=true; } };
int main(){
 ActorWorld world; StrawberryActor berry; RefillActor refill; SpringActor spring; Recyclable dead;
 spring.position={2,0,0}; refill.position={5,0,0};
 auto berry_id=world.Add(berry); world.Add(refill); world.Add(spring); auto dead_id=world.Add(dead);
 assert(world.Count()==0); world.ResolvePending(); assert(world.Count()==4);
 assert(world.Get<StrawberryActor>()==&berry); assert(world.All<PickupTrait>().count==3);
 assert(world.OverlapsFirst<PickupTrait>({2,0,0})==static_cast<PickupTrait*>(&spring));
 assert(berry_id!=0); world.Destroy(dead_id); assert(!dead.recycled); world.ResolvePending(); assert(dead.recycled && world.Count()==3);
 ActorWorld spawned; StrawberryActor spawned_berry; RefillActor spawned_refill; SpringActor spawned_spring;
 ActorSpawn spawn{{9,0,0}, 7}; assert(SpawnActor(spawned, spawn, spawned_berry, spawned_refill, spawned_spring)!=0);
 spawned.ResolvePending(); assert(spawned.Get<SpringActor>()==&spawned_spring);
 return 0;
}
