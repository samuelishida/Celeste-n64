#pragma once

#include "gameplay/player/movement_config.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

class PlayerController {
public:
    struct StepContext {
        float raw_input_length = 0.0f;
        Vec3 move_input;
        bool has_move_input = false;
        Vec3 dash_momentum = {};
        // Dash enter applies OG's launch vector immediately. The transition
        // cascade must not run the active dash/hitstop update on that frame.
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

    // Apply one already-advanced moving surface's rider displacement before
    // this tick's input/state update, matching OG Solid.MoveTo ordering.
    void MovingSurfacePhase(PlayerState& state, const Room& room) const;

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
    // OG LateUpdate equivalent (og-mover-overhaul Inc 2): after the state
    // handler edits velocity, SweepMove applies it through the single mover,
    // then LateContact resolves ground/ceiling contacts, refreshes coyote +
    // dash refill, and runs the cooldown-gated ground snap. Called once per
    // tick by the scene.
    void MovePhase(
        PlayerState& state,
        float delta_seconds,
        const Room* room = nullptr
    ) const;
    // Old post-step bookkeeping (was_grounded snapshot). Keep the name for
    // callers that used to call LateContactPhase after StatePhase; the scene
    // now calls MovePhase which internalizes contact resolution.
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
    MovementConfig config_;
    // MovementProfile member DELETED (review S4/C1): one config struct owns
    // every feel constant now. The single mover lives here so scene/respawn
    // and tests can share the SAME instance (S1).
    MoveService mover_;

    // ── Inc 2: real state machine (controls-physics-port) ──
    // StatePhase dispatches to the per-state St*Update handlers; enter/exit
    // hooks fire on each transition. The bodies are the pre-FSM hand-switched
    // blocks moved verbatim (no logic reordering).
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
