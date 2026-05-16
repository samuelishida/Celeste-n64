#include <cassert>
#include "gameplay/actor/solid_actor.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/actor/moving_solid_actor.hpp"
using namespace madeline_cube;
int main(){
 Room room;
 room.colliders[room.collider_count++]={.type=ColliderType::Plane,.bounds={{-2,-5,-2},{2,-5,2}},.solid=true,.normal={0,1,0},.face_id=1,.owner_id=1};
 ActorWorld world; SolidActor solid; solid.owner_id=77; solid.AddFace({.type=ColliderType::Plane,.bounds={{-2,0,-2},{2,0,2}},.solid=true,.normal={0,1,0},.face_id=9});
 world.Add(solid); world.ResolvePending(); room.actor_world=&world;
 auto hit=QueryFloorSource(room,{0,3,0},10);
 assert(hit.hit&&hit.owner_id==77&&hit.face_id==9&&hit.point.y==0);
 MovingSolidActor moving; moving.owner_id=88; moving.AddFace({.type=ColliderType::Plane,.bounds={{3,1,-2},{7,1,2}},.solid=true,.normal={0,1,0},.face_id=10});
 moving.last_position={0,0,0}; moving.target_position={1,0,0}; moving.Advance(0.5f);
 ActorWorld moving_world; moving_world.Add(moving); moving_world.ResolvePending(); Room moving_room; moving_room.actor_world=&moving_world;
 auto moved=QueryFloorSource(moving_room,{5,3,0},5); assert(moved.hit&&moved.owner_velocity.x==2.0f);
 return 0;
}
