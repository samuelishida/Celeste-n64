#pragma once

#include "movement_config.hpp"
#include "player_state.hpp"

namespace madeline_cube {

class PlayerController {
public:
    struct StepContext {
        float raw_input_length = 0.0f;
        Vec3 move_input;
        bool has_move_input = false;
    };

    explicit PlayerController(MovementConfig config = {});

    void Step(
        PlayerState& state,
        const PlayerInput& input,
        const Vec3& camera_forward,
        float delta_seconds
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
        const StepContext& context,
        float delta_seconds
    ) const;
    void LateContactPhase(PlayerState& state) const;

private:
    MovementConfig config_;
};

}  // namespace madeline_cube
