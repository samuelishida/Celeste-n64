#pragma once
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct FallingBlockRuntime {
    enum Phase : uint8_t { Wait, Shake, Fall, Respawn };
    Phase phase = Wait;
    float timer = 0.0f;
    float velocity_y = 0.0f;
    Vec3 current_position;
};

void InitFallingBlocks(const Room& room, FallingBlockRuntime out[], int* out_count);
void StepFallingBlocks(FallingBlockRuntime rt[], int count,
                       Room& room, const Vec3& player_pos, float dt);

}  // namespace madeline_cube
