#pragma once

#include "movement_config.hpp"
#include "player_state.hpp"

namespace madeline_cube {

class RespawnSystem {
public:
    explicit RespawnSystem(MovementConfig config = {});

    bool Step(PlayerState& player, const Vec3& checkpoint) const;

private:
    MovementConfig config_;
};

}  // namespace madeline_cube

