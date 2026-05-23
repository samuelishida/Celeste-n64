#include "gameplay/actor/falling_block.hpp"

#include <cmath>
#include <cstdio>

namespace madeline_cube {

// OG FallingBlock constants (from Celeste64-og Source/Actors/FallingBlock.cs)
constexpr float kFallingBlockShakeSeconds = 0.4f;
constexpr float kFallingBlockGravity      = -600.0f;
constexpr float kFallingBlockMaxFallSpeed = -160.0f;
constexpr float kFallingBlockRespawnSeconds = 5.0f;

void InitFallingBlocks(const Room& room, FallingBlockRuntime out[], int* out_count) {
    *out_count = 0;
    for (int i = 0; i < room.falling_block_count; ++i) {
        auto& spawn = room.falling_blocks[i];
        out[*out_count] = {
            .phase = FallingBlockRuntime::Wait,
            .timer = 0.0f,
            .velocity_y = 0.0f,
            .current_position = spawn.origin,
        };
        (*out_count)++;
    }
}

void StepFallingBlocks(FallingBlockRuntime rt[], int count,
                       Room& room, const Vec3& player_pos, float dt) {
    for (int i = 0; i < count; ++i) {
        auto& runtime = rt[i];
        auto& spawn = room.falling_blocks[i];

        float dx = player_pos.x - spawn.origin.x;
        float dy = player_pos.y - spawn.origin.y;
        float dz = player_pos.z - spawn.origin.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < spawn.half_extents.x * 2.0f) {
            if (runtime.phase == FallingBlockRuntime::Wait) {
                runtime.phase = FallingBlockRuntime::Shake;
                runtime.timer = kFallingBlockShakeSeconds;
            }
        }

        switch (runtime.phase) {
            case FallingBlockRuntime::Wait:
                break;

            case FallingBlockRuntime::Shake:
                runtime.timer -= dt;
                if (runtime.timer <= 0.0f) {
                    runtime.phase = FallingBlockRuntime::Fall;
                    runtime.velocity_y = 0.0f;
                }
                break;

            case FallingBlockRuntime::Fall: {
                runtime.velocity_y += kFallingBlockGravity * dt;
                if (runtime.velocity_y < kFallingBlockMaxFallSpeed) {
                    runtime.velocity_y = kFallingBlockMaxFallSpeed;
                }
                runtime.current_position.y += runtime.velocity_y * dt;

                int ms_idx = spawn.moving_surface_index;
                if (ms_idx >= 0 && ms_idx < room.moving_surface_count) {
                    room.moving_surfaces[ms_idx].position = runtime.current_position;
                    // last_position is set by AdvanceMovingSurfaces before each tick —
                    // do not stomp it here or rider velocity will compute as zero.
                }

                // Transition to Respawn when block falls far enough below origin
                if (runtime.current_position.y < spawn.origin.y - 200.0f) {
                    runtime.phase = FallingBlockRuntime::Respawn;
                    runtime.timer = kFallingBlockRespawnSeconds;
                }
                break;
            }

            case FallingBlockRuntime::Respawn:
                runtime.timer -= dt;
                if (runtime.timer <= 0.0f) {
                    runtime.phase = FallingBlockRuntime::Wait;
                    runtime.current_position = spawn.origin;
                    runtime.velocity_y = 0.0f;
                    int ms_idx = spawn.moving_surface_index;
                    if (ms_idx >= 0 && ms_idx < room.moving_surface_count) {
                        room.moving_surfaces[ms_idx].position = spawn.origin;
                    }
                }
                break;
        }
    }
}

}  // namespace madeline_cube
