#include <cassert>
#include <cmath>
#include <limits>

#include "../src/user/gameplay/rom_telemetry.hpp"

using namespace madeline_cube;

int main() {
    // --- Frame counter ---
    {
        RomTelemetry t;
        assert(t.frame_index == 0);
        t.BeginFrame();
        assert(t.frame_index == 1);
        t.BeginFrame();
        assert(t.frame_index == 2);
    }

    // --- Spawn / respawn ---
    {
        RomTelemetry t;
        t.RecordSpawn();
        assert(t.spawn_count == 1);
        t.RecordRespawn();
        assert(t.respawn_count == 1);
    }

    // --- RecordPlayerState: grounded / airborne ---
    {
        RomTelemetry t;
        PlayerState state;
        state.grounded = true;
        t.RecordPlayerState(state);
        assert(t.grounded_frames == 1);
        assert(t.airborne_frames == 0);

        state.grounded = false;
        t.RecordPlayerState(state);
        assert(t.grounded_frames == 1);
        assert(t.airborne_frames == 1);
    }

    // --- RecordPlayerState: wall contact ---
    {
        RomTelemetry t;
        PlayerState state;
        state.wall_left = true;
        t.RecordPlayerState(state);
        assert(t.wall_contact_frames == 1);

        state.wall_left = false;
        state.wall_right = true;
        t.RecordPlayerState(state);
        assert(t.wall_contact_frames == 2);
    }

    // --- RecordPlayerState: numeric validity ---
    {
        RomTelemetry t;
        PlayerState state;
        state.position = {1.0f, 2.0f, 3.0f};
        state.velocity = {0.0f, 0.0f, 0.0f};
        t.RecordPlayerState(state);
        assert(t.invalid_position_count == 0);
        assert(t.invalid_velocity_count == 0);

        state.position.x = std::numeric_limits<float>::quiet_NaN();
        t.RecordPlayerState(state);
        assert(t.invalid_position_count == 1);

        state.position.x = 0.0f;
        state.velocity.y = std::numeric_limits<float>::infinity();
        t.RecordPlayerState(state);
        assert(t.invalid_velocity_count == 1);
    }

    // --- RecordPlayerState: movement state encoding ---
    {
        RomTelemetry t;
        PlayerState state;
        state.locomotion_state = LocomotionState::Idle;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 0);

        state.locomotion_state = LocomotionState::Run;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 1);

        state.locomotion_state = LocomotionState::Jump;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 2);

        state.locomotion_state = LocomotionState::Dash;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 3);

        state.locomotion_state = LocomotionState::Climb;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 4);

        state.locomotion_state = LocomotionState::Fall;
        t.RecordPlayerState(state);
        assert(t.last_movement_state == 5);
    }

    // --- Landing event detection ---
    {
        RomTelemetry t;
        PlayerState state;
        state.grounded = false;
        t.RecordPlayerState(state);
        assert(t.landing_events == 0);

        state.grounded = true;
        t.RecordPlayerState(state);
        assert(t.landing_events == 1);

        // Still grounded: no new landing
        t.RecordPlayerState(state);
        assert(t.landing_events == 1);
    }

    // --- Named events ---
    {
        RomTelemetry t;
        t.RecordLanding();
        assert(t.landing_events == 1);
        t.RecordDashStart();
        assert(t.dash_start_events == 1);
        t.RecordDashEnd();
        assert(t.dash_end_events == 1);
        t.RecordWallJump();
        assert(t.wall_jump_events == 1);
        t.RecordClimbStart();
        assert(t.climb_start_events == 1);
    }

    // --- Snapshot tracking ---
    {
        RomTelemetry t;
        PlayerState state;
        state.position = {1.0f, 2.0f, 3.0f};
        state.velocity = {4.0f, 5.0f, 6.0f};
        state.grounded = true;
        t.RecordPlayerState(state);
        assert(t.last_position.x == 1.0f);
        assert(t.last_velocity.z == 6.0f);
        assert(t.last_grounded == true);
    }

    return 0;
}
