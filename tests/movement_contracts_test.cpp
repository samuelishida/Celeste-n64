#include <cassert>
#include <cmath>

#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/physics/coll_mesh.hpp"
#include "../src/user/gameplay/player/movement_config.hpp"
#include "../src/user/gameplay/player/player_state.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"

using namespace madeline_cube;

int main() {
    static_assert(static_cast<int>(LocomotionState::Idle) == 0);
    static_assert(static_cast<int>(LocomotionState::Run) == 1);
    static_assert(static_cast<int>(LocomotionState::Jump) == 2);
    static_assert(static_cast<int>(LocomotionState::Dash) == 3);
    static_assert(static_cast<int>(LocomotionState::Climb) == 4);
    static_assert(static_cast<int>(LocomotionState::Fall) == 5);

    // MovementProfile deleted (review S4/C1): feel constants live in
    // MovementConfig alone — pin the OG values on the one source of truth.
    MovementConfig config;
    assert(config.coyote_time == 0.12f);
    assert(config.jump_buffer_time == 0.10f);   // OG device buffer (was 0.08)
    assert(config.dash_hitstop_time == 0.02f);
    assert(config.dash_active_time >= 0.18f && config.dash_active_time <= 0.22f);

    // dash_momentum is an inter-phase communication field on StepContext,
    // not on PlayerState. Verify it defaults to zero.
    PlayerController::StepContext ctx;
    assert(ctx.dash_momentum.x == 0.0f);
    assert(ctx.dash_momentum.y == 0.0f);
    assert(ctx.dash_momentum.z == 0.0f);

    // PlayerState should not have dash_momentum (it moved to StepContext).
    PlayerState player;
    assert(player.locomotion_state == LocomotionState::Idle);

    // OG SweepTestMove returns immediately for zero displacement; only the
    // explicit final Popout repairs an idle position (Player.cs:642-660/373).
    Room wall_room;
    wall_room.colliders[wall_room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {-100, -100, 4}, .max = {100, 100, 5}},
        .solid = true,
        .normal = {0, 0, 1},
        .face_id = 1,
    };
    MoveService resolver;
    PlayerState idle;
    idle.position = {0, 30, 7.5f};
    resolver.BeginTick(idle);
    resolver.SweepMove(idle, wall_room, {}, false);
    assert(idle.position.z == 7.5f && !idle.wall_contact);
    resolver.Popout(idle, wall_room, false);
    assert(std::fabs(idle.position.z - 8) < .001f && idle.wall_contact);

    PlayerController controller;
    // Both requests share the same resolver, with no contact reset between
    // them. The first request hits; the second leaves the wall. Whole-tick
    // contact must survive, while the next tick resets it exactly once.
    PlayerState ordered;
    ordered.position = {0, 30, 8};
    ordered.velocity = {0, 0, 120};
    PlayerController::StepContext two;
    ordered.movement_state = PlayerMovementState::Climbing;
    controller.BeginMoveTick(ordered);
    two.movement.has_climb_delta = true;
    two.movement.climb_delta = {0, 0, -1};
    controller.MovePhase(ordered, 1.0f/60.0f, &wall_room, &two);
    assert(two.movement_order == 123 && ordered.movement_contact_reset_count == 1);
    assert(std::fabs(ordered.position.z - 10) < .001f);
    assert(ordered.wall_contact && ordered.velocity.z == 120);
    PlayerController::StepContext next;
    ordered.movement_state = PlayerMovementState::Normal;
    controller.BeginMoveTick(ordered);
    controller.MovePhase(ordered, 1.0f/60.0f, &wall_room, &next);
    assert(next.movement_order == 23 && ordered.movement_contact_reset_count == 2);
    assert(!ordered.wall_contact);

    // A real Climbing tick produces displacement independently of velocity,
    // then performs post-sweep grip queries and the zero outer sweep.
    PlayerState climb;
    climb.position = {0, 30, 8};
    climb.movement_state = PlayerMovementState::Climbing;
    climb.climb.wall_normal = {0, 0, 1};
    climb.last_facing = climb.target_facing = {0, 0, -1};
    PlayerInput up;
    up.climb_held = true;
    up.move = {0, 1};
    controller.BeginMoveTick(climb);
    auto climb_step = controller.TimerInputPhase(climb, up, {0, 0, -1}, 1.0f/60.0f);
    controller.StatePhase(climb, up, climb_step, 1.0f/60.0f, &wall_room);
    assert(climb.position.y == 30 && climb.velocity.y == 0);
    assert(climb_step.climb_post_move && climb_step.movement.has_climb_delta);
    controller.MovePhase(climb, 1.0f/60.0f, &wall_room, &climb_step);
    assert(climb_step.movement_order == 123 && climb.movement_contact_reset_count == 1);
    assert(climb.position.y > 30 && climb.movement_state == PlayerMovementState::Climbing);

    // OG Popout repairs the body but onGround is owned by late evaluation.
    Room floor_room;
    floor_room.colliders[floor_room.collider_count++]={
        .type=ColliderType::Plane,.bounds={.min={-100,0,-100},.max={100,0,100}},
        .solid=true,.normal={0,1,0},.face_id=20,
    };
    CollisionQueryCounters floor_queries{}; floor_room.query_counters=&floor_queries;
    PlayerState falling;
    falling.position={0,resolver.Config().half_height-.5f,0};
    falling.velocity={0,-20,0};
    resolver.BeginTick(falling);
    resolver.Popout(falling,floor_room,true);
    assert(!falling.grounded && falling.velocity.y==0);
    assert(falling.position.y==resolver.Config().half_height);
    assert(floor_queries.raycasts==0); // dynamic collider path has no CMSH traversal
    // A steep authored front-facing ground ray is still GroundCheck support;
    // FloorNormalCheck (.35) is reserved for late ground snap.
    floor_room.colliders[0].normal={.9797959f,.2f,0};
    floor_room.colliders[0].has_plane_origin=true;
    floor_room.colliders[0].plane_origin={0,0,0};
    floor_room.colliders[0].bounds={.min={-100,-100,-100},.max={100,100,100}};
    falling.position={0,resolver.Config().half_height,0};
    Vec3 steep_normal{};
    assert(resolver.GroundCheck(falling,floor_room,nullptr,&steep_normal));
    assert(steep_normal.y<.35f);

    // Static support metadata comes from the same single GroundCheck ray,
    // rather than a second floor query after positional repair.
    physics::CollHeader floor_header{};
    floor_header.quant_scale=1; floor_header.vertex_count=3;
    floor_header.triangle_count=1; floor_header.bvh_node_count=1;
    Vec3 floor_vertices[3]={{-20,0,-20},{0,0,20},{20,0,-20}};
    physics::CollTriangle floor_triangle{0,1,2,physics::MAT_SOLID,0,0};
    physics::CollBvhNode floor_node{{-20,0,-20},{20,0,20},0,1};
    physics::CollMesh floor_mesh{};
    floor_mesh.header=&floor_header; floor_mesh.world_verts=floor_vertices;
    floor_mesh.triangles=&floor_triangle; floor_mesh.bvh_nodes=&floor_node;
    Room static_floor{}; static_floor.coll_mesh=&floor_mesh;
    CollisionQueryCounters static_queries{}; static_floor.query_counters=&static_queries;
    PlayerState falling_static;
    falling_static.position={0,resolver.Config().half_height-.5f,0};
    resolver.BeginTick(falling_static);
    resolver.Popout(falling_static,static_floor,false);
    assert(!falling_static.grounded && static_queries.raycasts==1);
    assert(falling_static.position.y==resolver.Config().half_height);

    // CameraBasis removed (review C7): zero prod consumers after the motor
    // deletion; the camera reads the room scene-side.
    return 0;
}