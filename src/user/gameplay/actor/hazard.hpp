#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

// Returns true on the first hazard the player's collision sphere overlaps.
// `kind_out` receives the hazard kind when provided: 0=spike, 1=death.
bool CheckHazardKill(const Room& room,
                     const Vec3& player_pos,
                     float player_radius,
                     uint8_t* kind_out = nullptr);

}  // namespace madeline_cube
