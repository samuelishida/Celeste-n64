#include <cassert>
#include <cmath>
#include <cstdio>
#include "gameplay/player/player_controller.hpp"
#include "gameplay/runtime/math.hpp"
using namespace madeline_cube;
constexpr float dt = 1.0f/60.0f;
static void near(float a,float b,float eps=0.003f) { if(std::fabs(a-b)>eps) { std::printf("expected %.6f got %.6f\n",b,a); std::fflush(stdout); assert(false); } }
static Room Wall() { Room r; r.colliders[r.collider_count++]={.type=ColliderType::Box,.bounds={.min={-10,-100,-100},.max={0,100,100}},.solid=true,.face_id=1}; return r; }
static Room Floor() { Room r; r.colliders[r.collider_count++]={.type=ColliderType::Plane,.bounds={.min={-100,0,-100},.max={100,0,100}},.solid=true,.normal={0,1,0},.face_id=2}; return r; }
int main() {
    PlayerController c; const Vec3 f{0,0,1};
    // Player Jump -> LateUpdate: flag remains the previous tick's support
    // during state animation selection; landing/scale/facing update afterward.
    Room floor=Floor(); PlayerState p; p.position={0,11.25f,0}; p.grounded=p.contact.was_grounded=true;
    p.coyote_time_remaining=.12f; p.contact.coyote_height=p.position.y;
    PlayerInput j; j.jump_pressed=j.jump_held=true;
    c.Step(p,j,f,dt,&floor); near(p.velocity.y,90); near(p.position.y,12.75f);
    near(p.model_scale.y,1.4f-dt/.8f); near(p.model_scale.x,.6f+dt/.8f);
    assert(!p.grounded && p.animation.Current()==PlayerAnimState::Idle);
    PlayerInput hold; hold.jump_held=true; c.Step(p,hold,f,dt,&floor);
    assert(p.animation.Current()==PlayerAnimState::Run && p.animation.Count()==1);
    near(p.animation.At(0).time,dt); c.Step(p,hold,f,dt,&floor); near(p.animation.At(0).time,dt);
    // Climb animation uses translated input, not actual motion; source climb40.
    Room wall=Wall(); PlayerState q; q.position={3,30,0}; q.target_facing=q.last_facing={-1,0,0};
    q.facing=f; PlayerInput grip; grip.climb_held=true;
    c.Step(q,grip,{-1,0,0},dt,&wall); assert(q.movement_state==PlayerMovementState::Climbing);
    assert(q.animation.Current()==PlayerAnimState::ClimbIdle); near(q.animation.rate,1.8f);
    assert(DotXZ(q.facing,q.target_facing)<.5f); // smooth source facing, no entry snap.
    grip.move={0,1}; const float y=q.position.y; c.Step(q,grip,{-1,0,0},dt,&wall);
    near(q.position.y-y,40*dt); assert(q.animation.Current()==PlayerAnimState::ClimbUp);
    grip.move={0,-1}; c.Step(q,grip,{-1,0,0},dt,&wall); assert(q.animation.Current()==PlayerAnimState::ClimbIdle);
    grip.move={.2f,0}; c.Step(q,grip,{-1,0,0},dt,&wall); assert(q.animation.Current()==PlayerAnimState::ClimbIdle);
    grip.move={.25f,0}; c.Step(q,grip,{-1,0,0},dt,&wall); assert(q.animation.Current()==PlayerAnimState::ClimbUp);
    // Synchronous climb exit -> Normal enter -> WallJump preserves its hold.
    PlayerInput wj; wj.climb_held=wj.jump_pressed=wj.jump_held=true;
    c.Step(q,wj,{-1,0,0},dt,&wall); assert(q.movement_state==PlayerMovementState::Normal);
    near(q.velocity.x,83.2f); near(q.velocity.y,90); near(q.hold_jump_time_remaining,.1f);
    near(q.animation.rate,1); assert(q.animation.Current()==PlayerAnimState::Idle);
    // A jump during a corner launches opposite interpolated targetFacing,
    // not opposite the already adopted destination wall normal.
    PlayerState corner_jump; corner_jump.movement_state=PlayerMovementState::Climbing;
    corner_jump.climb.wall_normal={1,0,0}; corner_jump.target_facing={-.6f,0,-.8f};
    PlayerInput corner_input; corner_input.climb_held=corner_input.jump_pressed=true;
    c.Step(corner_jump,corner_input,f,dt);
    near(corner_jump.velocity.x,.6f*83.2f); near(corner_jump.velocity.z,.8f*83.2f);
    // Dash launches on previous support without cancelling source ground snap.
    PlayerState d; d.position={0,11.25f,0}; d.grounded=d.contact.was_grounded=true;
    d.animation.Play(PlayerAnimState::Idle); PlayerInput dash; dash.dash_pressed=true;
    c.Step(d,dash,f,dt,&floor); assert(d.movement_state==PlayerMovementState::Dashing);
    near(d.velocity.z,140); near(d.contact.ground_snap_cooldown_remaining,0);
    assert(d.animation.Current()==PlayerAnimState::Idle);
    const float clock=d.animation.At(0).time;
    c.Step(d,PlayerInput{},f,dt,&floor); near(d.animation.At(0).time,clock);
    c.Step(d,PlayerInput{},f,dt,&floor); near(d.animation.At(0).time,clock);
    c.Step(d,PlayerInput{},f,dt,&floor); assert(d.animation.Current()==PlayerAnimState::Dash);
    // Landing uses previous falling speed and begins recovery the same tick.
    PlayerState land; land.position={0,12.25f,0}; land.velocity.y=-120;
    c.Step(land,PlayerInput{},f,dt,&floor); assert(land.grounded);
    near(land.model_scale.x,1.4f-dt/.8f); near(land.model_scale.y,.6f+dt/.8f);
    // OG forward snap is applied to the input before camera projection.
    const Vec3 cardinal=RelativeMoveInput({.1f,1},f,f); near(cardinal.x,0); near(cardinal.z,1);
    std::puts("og_behavior_trace_smoke PASS");
}
