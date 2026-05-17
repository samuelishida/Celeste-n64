#include "rom_telemetry.hpp"

#ifdef N64
#include <debug.h>
#else
#include <cstdio>
#endif

#include "physics_contracts.hpp"

namespace madeline_cube {

void RomTelemetry::PrintLine() const {
#ifdef N64
    debugf("[telemetry] f=%u sp=%u rp=%u g=%u a=%u w=%u ip=%u iv=%u st=%u "
           "L=%u Ds=%u De=%u W=%u C=%u ms=%u sl=%u sn=%u "
           "pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)\n",
           static_cast<unsigned int>(frame_index),
           static_cast<unsigned int>(spawn_count),
           static_cast<unsigned int>(respawn_count),
           static_cast<unsigned int>(grounded_frames),
           static_cast<unsigned int>(airborne_frames),
           static_cast<unsigned int>(wall_contact_frames),
           static_cast<unsigned int>(invalid_position_count),
           static_cast<unsigned int>(invalid_velocity_count),
           static_cast<unsigned int>(last_movement_state),
           static_cast<unsigned int>(landing_events),
           static_cast<unsigned int>(dash_start_events),
           static_cast<unsigned int>(dash_end_events),
           static_cast<unsigned int>(wall_jump_events),
           static_cast<unsigned int>(climb_start_events),
           static_cast<unsigned int>(moving_surface_count),
           static_cast<unsigned int>(slope_ground_count),
           static_cast<unsigned int>(snap_recoveries_count),
           static_cast<double>(last_position.x),
           static_cast<double>(last_position.y),
           static_cast<double>(last_position.z),
           static_cast<double>(last_velocity.x),
           static_cast<double>(last_velocity.y),
           static_cast<double>(last_velocity.z));
#else
    std::printf("[telemetry] f=%u sp=%u rp=%u g=%u a=%u w=%u ip=%u iv=%u st=%u "
                "L=%u Ds=%u De=%u W=%u C=%u ms=%u sl=%u sn=%u "
                "pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned int>(frame_index),
                static_cast<unsigned int>(spawn_count),
                static_cast<unsigned int>(respawn_count),
                static_cast<unsigned int>(grounded_frames),
                static_cast<unsigned int>(airborne_frames),
                static_cast<unsigned int>(wall_contact_frames),
                static_cast<unsigned int>(invalid_position_count),
                static_cast<unsigned int>(invalid_velocity_count),
                static_cast<unsigned int>(last_movement_state),
                static_cast<unsigned int>(landing_events),
                static_cast<unsigned int>(dash_start_events),
                static_cast<unsigned int>(dash_end_events),
                static_cast<unsigned int>(wall_jump_events),
                static_cast<unsigned int>(climb_start_events),
                static_cast<unsigned int>(moving_surface_count),
                static_cast<unsigned int>(slope_ground_count),
                static_cast<unsigned int>(snap_recoveries_count),
                static_cast<double>(last_position.x),
                static_cast<double>(last_position.y),
                static_cast<double>(last_position.z),
                static_cast<double>(last_velocity.x),
                static_cast<double>(last_velocity.y),
                static_cast<double>(last_velocity.z));
#endif
}

void RomTelemetry::RecordSurfaceSample(uint32_t live_moving_surfaces, bool grounded,
                                       float ground_normal_y, bool snap_recovered) {
    moving_surface_count = live_moving_surfaces;
    // Treat anything below 0.999 as a slope (axis-aligned floor normal is 1.0).
    if (grounded && IsNumericValid(ground_normal_y) && ground_normal_y < 0.999f) {
        ++slope_ground_count;
    }
    if (snap_recovered) {
        ++snap_recoveries_count;
    }
}

void RomTelemetry::RecordPlayerState(const PlayerState& state) {
    // Update contact frame counters
    if (state.grounded) {
        ++grounded_frames;
    } else {
        ++airborne_frames;
    }

    if (state.wall_left || state.wall_right) {
        ++wall_contact_frames;
    }

    // Check numeric validity
    if (!IsNumericValid(state.position)) {
        ++invalid_position_count;
    }
    if (!IsNumericValid(state.velocity)) {
        ++invalid_velocity_count;
    }

    switch (state.locomotion_state) {
        case LocomotionState::Idle:
            last_movement_state = 0;
            break;
        case LocomotionState::Run:
            last_movement_state = 1;
            break;
        case LocomotionState::Jump:
            last_movement_state = 2;
            break;
        case LocomotionState::Dash:
            last_movement_state = 3;
            break;
        case LocomotionState::Climb:
            last_movement_state = 4;
            break;
        case LocomotionState::Fall:
            last_movement_state = 5;
            break;
    }

    // Detect landing event: was airborne, now grounded
    if (state.grounded && !last_grounded) {
        RecordLanding();
    }

    // Store snapshot for next frame
    last_position = state.position;
    last_velocity = state.velocity;
    last_grounded = state.grounded;
}

}  // namespace madeline_cube
