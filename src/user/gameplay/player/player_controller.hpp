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

private:
    MovementConfig config_;
    // MovementProfile member DELETED (review S4/C1): one config struct owns
    // every feel constant now. The single mover lives here so scene/respawn
    // and tests can share the SAME instance (S1).
    MoveService mover_;

public:
    // Shared-mover access (review S1): the ONE mover instance. The scene and
    // RespawnSystem pass this to spawn/respawn contact resolution instead of
    // constructing a second, implicitly-matching MoveService.
    const MoveService& Mover() const { return mover_; }
};

}  // namespace madeline_cube
