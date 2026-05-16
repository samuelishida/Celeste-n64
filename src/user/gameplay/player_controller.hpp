#pragma once

#include "movement_config.hpp"
#include "player_state.hpp"

namespace madeline_cube {

class PlayerController {
public:
    explicit PlayerController(MovementConfig config = {});

    void Step(
        PlayerState& state,
        const PlayerInput& input,
        const Vec3& camera_forward,
        float delta_seconds
    ) const;

private:
    MovementConfig config_;
};

}  // namespace madeline_cube
