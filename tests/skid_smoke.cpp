// og-mover-overhaul Inc 4: Skidding state + dash steering + dash input buffer.
//
// Drives the full controller tick on a flat floor with the OG feel constants:
//   1. running fast + reversed input enters Skidding (dot <= SkidDotThreshold),
//   2. skid decel toward the input direction, exits to Normal at EndSkidSpeed,
//   3. SkidJump after the 0.10 s unlock launches with SkidJumpSpeed,
//   4. dash rotate-and-re-set steering during a dash,
//   5. a dash press just before an opportunity opens (buffered) still fires.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {
constexpr float kDt = 1.0f / 60.0f;
}

int main() {
    MovementConfig config;
    PlayerController controller(config);
    const Vec3 cam = {0.0f, 0.0f, 1.0f};
    // Floor plane at y=0 so MovePhase's ray GroundCheck keeps `grounded` true
    // across ticks (feet rest on it via half_height).
    Room no_room;
    no_room.colliders[no_room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-80.0f, 0.0f, -80.0f}, .max = {80.0f, 0.0f, 80.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
    };

    // ── 1+2: fast run into reversed input → skids, then exits at EndSkidSpeed ──
    {
        PlayerState p;
        p.grounded = true;
        p.position = {0.0f, config.player_half_height, 0.0f};
        p.velocity = {config.run_speed, 0.0f, 0.0f};   // 64 u/s right
        p.target_facing = {1.0f, 0.0f, 0.0f};
        p.last_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.move = {-1.0f, 0.0f};  // push left (opposite)

        bool skid_entered = false;
        int skid_ticks = 0;
        for (int i = 0; i < 120; ++i) {
            controller.Step(p, in, cam, kDt, &no_room);
            if (p.movement_state == PlayerMovementState::Skidding) skid_entered = true;
            if (p.movement_state == PlayerMovementState::Skidding) ++skid_ticks;
        }
        assert(skid_entered);
        printf("skid: entered=%d ticks=%d final_x=%.2f\n", (int)skid_entered, skid_ticks, p.velocity.x);
        // Once aligned and down at EndSkidSpeed it exits to Normal, then the
        // reversed input accelerates leftward. Assert the direction flipped.
        assert(p.velocity.x < 0.0f);
    }

    // ── 3: SkidJump after 0.10 s unlock ──
    {
        PlayerState p;
        p.grounded = true;
        p.position = {0.0f, config.player_half_height, 0.0f};
        p.velocity = {config.run_speed, 0.0f, 0.0f};
        p.target_facing = {1.0f, 0.0f, 0.0f};
        p.last_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.move = {-1.0f, 0.0f};
        // Enter skid.
        for (int i = 0; i < 3; ++i) controller.Step(p, in, cam, kDt, &no_room);
        assert(p.movement_state == PlayerMovementState::Skidding);

        // Wait through the 0.10 s unlock, then press jump.
        int unlock = (int)std::ceil(config.no_skid_jump_time / kDt);
        for (int i = 0; i < unlock + 1; ++i) {
            in.jump_pressed = false;
            controller.Step(p, in, cam, kDt, &no_room);
        }
        in.jump_pressed = true;
        in.jump_held = true;
        controller.Step(p, in, cam, kDt, &no_room);
        assert(p.movement_state == PlayerMovementState::Normal);
        assert(p.velocity.y > config.jump_speed);  // skid jump 120 > normal 90
        printf("skid-jump: vy=%.2f (expect ~%.0f)\n", p.velocity.y, config.skid_jump_speed);
    }

    // ── 4: dash rotation steering ──
    {
        PlayerState p;
        p.grounded = true;
        p.position = {0.0f, config.player_half_height, 0.0f};
        p.air_dash_available = true;
        p.dash_cooldown_remaining = 0.0f;
        p.target_facing = {1.0f, 0.0f, 0.0f};

        PlayerInput in;
        in.dash_pressed = true;
        in.move = {0.0f, 1.0f};  // toward +z
        controller.Step(p, in, cam, kDt, &no_room);
        assert(p.movement_state == PlayerMovementState::Dashing);
        // Keep steering toward +z over several ticks.
        in.dash_pressed = false;
        for (int i = 0; i < 20; ++i) controller.Step(p, in, cam, kDt, &no_room);
        // The dash direction must have rotated toward +z (z velocity grows).
        printf("dash-steer: vz=%.2f vx=%.2f\n", p.velocity.z, p.velocity.x);
        assert(p.velocity.z > 1.0f);
    }

    // ── 5: dash input buffer ──
    {
        PlayerState p;
        p.grounded = true;
        p.position = {0.0f, config.player_half_height, 0.0f};
        p.air_dash_available = true;
        // A dash cooldown blocks the press from firing *now*.
        p.dash_cooldown_remaining = 0.06f;  // ~4 ticks of block

        PlayerInput in;
        in.dash_pressed = true;         // press with the cooldown blocking
        controller.Step(p, in, cam, kDt, &no_room);
        assert(p.movement_state != PlayerMovementState::Dashing);

        // Cooldown expires within the 0.10 s buffer window; the buffered press
        // fires the dash once the opportunity opens (no re-press needed).
        in.dash_pressed = false;
        for (int i = 0; i < 5; ++i) controller.Step(p, in, cam, kDt, &no_room);
        assert(p.movement_state == PlayerMovementState::Dashing);
        printf("dash-buffer: fired from buffered press\n");
    }

    printf("skid_smoke PASS\n");
    return 0;
}
