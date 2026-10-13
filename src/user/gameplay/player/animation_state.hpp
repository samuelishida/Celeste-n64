#pragma once
#include <cstdint>
namespace madeline_cube {
// Stable asset identities. State handlers issue Play/Clear/restart commands;
// final physics flags cannot reconstruct original state transition ordering.
enum class PlayerAnimState : uint8_t { Idle, Run, Jump, Dash, ClimbIdle, ClimbUp, Skid };
} // namespace madeline_cube
