#pragma once

#include "gameplay/player/movement_config.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"
#include "gameplay/rom_telemetry.hpp"

namespace madeline_cube {

struct MapSpecV2;

class PlayerController {
public:
    struct StepContext {
        PlayerMoveIntent movement;
        const MapSpecV2* hazard_map = nullptr;
        bool climb_post_move = false;
        Vec2 climb_tick_input{};
        Vec3 camera_forward{};
        Vec3 camera_target_forward{};
        // Ordered movement diagnostics for host/device parity checks.
        unsigned movement_order = 0; // decimal stages: 1 kinematic, 2 velocity, 3 final
        unsigned state_handler_count = 0;
        float raw_input_length = 0.0f;
        Vec3 move_input;
        bool has_move_input = false;
        Vec3 dash_momentum = {};
        // Dash enter applies OG's launch vector immediately. The transition
        // new state handler begins on the following tick.
        bool dash_started_this_tick = false;
        // Set by StatePhase when a ledge-hop mantle fired this tick.
        bool mantle_triggered = false;
        // Set by StatePhase when a corner ease started this tick (Inc 5
        // telemetry: fires once per corner adopt, validates Inc 3 on device).
        bool corner_triggered = false;
    };

    explicit PlayerController(MovementConfig config = {});

    void Step(
        PlayerState& state,
        const PlayerInput& input,
        const Vec3& camera_forward,
        float delta_seconds,
        const Room* room = nullptr
    ) const;

    // OG Player.RefillDash: raise dashes to amount only when below it, and
    // start the source 0.05 s refill flash only on a successful refill.
    bool RefillDash(PlayerState& state, int amount = 1) const;

    void BeginMoveTick(PlayerState& state) const { mover_.BeginTick(state); }

    // OG World.HitStun consumes a fixed tick without advancing world state.
    // Requests commit at tick end so dash/refill writes preserve source order.
    bool ConsumeWorldHitstopTick(PlayerState& state, float delta_seconds) const;
    void RequestWorldHitstop(PlayerState& state, float duration) const;
    void CommitWorldHitstopRequest(PlayerState& state) const;

    // Apply one already-advanced moving surface's rider displacement before
    // this tick's input/state update, matching OG Solid.MoveTo ordering.
    void MovingSurfacePhase(PlayerState& state, const Room& room) const;
    // OG IRidePlatforms methods shared by room surfaces and actor-owned solids.
    void RidingPlatformSetVelocity(PlayerState& state,
                                   const Vec3& platform_velocity,
                                   float carry_storage_time = 0.10f) const;
    void RidingPlatformMoved(PlayerState& state, const Room& room,
                             const Vec3& displacement) const;

    StepContext TimerInputPhase(
        PlayerState& state,
        const PlayerInput& input,
        const Vec3& camera_forward,
        float delta_seconds
    ) const;
    void StatePhase(
        PlayerState& state,
        const PlayerInput& input,
        StepContext& context,
        float delta_seconds,
        const Room* room = nullptr
    ) const;
    // Complete-tick compatibility wrapper. Map-pack scheduling uses
    // MoveUpdatePhase, dispatches world actors, then calls
    // LateMoveContactPhase at Player's source-ordered LateUpdate slot.
    void MovePhase(
        PlayerState& state,
        float delta_seconds,
        const Room* room = nullptr,
        StepContext* context = nullptr
    ) const;
    // Split Player.Update movement from Player.LateUpdate ground contact so
    // world actors can run between those source phases. MovePhase remains the
    // compatibility wrapper for callers that need a complete tick.
    void MoveUpdatePhase(
        PlayerState& state,
        float delta_seconds,
        const Room* room = nullptr,
        StepContext* context = nullptr
    ) const;
    void LateMoveContactPhase(
        PlayerState& state,
        float delta_seconds,
        const Room* room = nullptr
    ) const;
    // Post-LateUpdate bookkeeping (was_grounded snapshot, presentation scale,
    // facing, animation clock). Kept separate for host and scene schedule.
    void LateContactPhase(
        PlayerState& state,
        float delta_seconds,
        const Room* room = nullptr
    ) const;
    // OG Spring parity (Player.cs:919-937): the world calls this when the
    // player overlaps a spring board. Exits Climbing/Dashing (state = Normal),
    // snaps her onto the board (approach spring + 3 up, XY approach 4),
    // launches at spring_jump_speed with a hold window, refills the dash,
    // and cancels the ground snap so the launch isn't pulled back.
    void Spring(PlayerState& state, const Vec3& spring_position) const;

private:
    void FinishClimbTrace(const PlayerState& state) const;
    MovementConfig config_;
    // MovementProfile member DELETED (review S4/C1): one config struct owns
    // every feel constant now. The single mover lives here so scene/respawn
    // and tests can share the SAME instance (S1).
    MoveService mover_;

    // Event-window diagnostics are mutable because the controller API is
    // const, while one controller still owns an ordered fixed-tick pipeline.
    mutable uint32_t climb_trace_tick_ = 0;
    mutable ClimbTraceFrame climb_trace_pending_{};
    mutable bool climb_trace_pending_valid_ = false;
    mutable bool climb_trace_query_sampled_ = false;
    mutable uint8_t climb_trace_event_ = 0;
    mutable int16_t climb_trace_release_reason_ = 0;
    mutable uint8_t climb_trace_grip_miss_ticks_ = 0;

    // ── Inc 2: real state machine (controls-physics-port) ──
    // StatePhase dispatches to the per-state St*Update handlers; enter/exit
    // hooks fire once synchronously after a handler changes state; the new
    // handler is never dispatched again during that tick.
    void TransitionState(PlayerState& state, PlayerMovementState next) const;
    void OnStateEnter(PlayerState& state, PlayerMovementState prev, PlayerMovementState next) const;
    void OnStateExit(PlayerState& state, PlayerMovementState prev, PlayerMovementState next) const;
    // OG TryDash parity (Player.cs:939): shared by the Normal and Skidding
    // updates (OG runs TryDash from both StNormalUpdate and StSkiddingUpdate).
    bool TryDashAccept(PlayerState& state, const PlayerInput& input, StepContext& context) const;
    void StNormalUpdate(
        PlayerState& state,
        const PlayerInput& input,
        StepContext& context,
        float delta_seconds,
        const Room* room
    ) const;
    void StDashingUpdate(
        PlayerState& state,
        const PlayerInput& input,
        StepContext& context,
        float delta_seconds,
        const Room* room
    ) const;
    void StSkiddingUpdate(
        PlayerState& state,
        const PlayerInput& input,
        StepContext& context,
        float delta_seconds,
        const Room* room
    ) const;
    bool TryClimb(PlayerState& state, const Vec3& relative_input,
                  const Room* room, StepContext* deferred = nullptr) const;
    void StClimbingPostMove(PlayerState& state, StepContext& context,
                           float delta_seconds, const Room* room) const;
    void StClimbingUpdate(
        PlayerState& state,
        const PlayerInput& input,
        StepContext& context,
        float delta_seconds,
        const Room* room
    ) const;

public:
    // Shared-mover access (review S1): the ONE mover instance. The scene and
    // RespawnSystem pass this to spawn/respawn contact resolution instead of
    // constructing a second, implicitly-matching MoveService.
    const MoveService& Mover() const { return mover_; }
};

}  // namespace madeline_cube
