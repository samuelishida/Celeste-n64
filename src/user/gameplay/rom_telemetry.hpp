#pragma once

#include <cstdint>

#include "math_types.hpp"
#include "player_state.hpp"

namespace madeline_cube {

// Low-cost counters and flags reported by the ROM for hardware diagnostics.
// All fields are numeric scalars so they can be printed with debugf and
// compared against host runs without parsing complex strings.
struct RomTelemetry {
    // Frame counters
    uint32_t frame_index = 0;

    // Spawn / lifecycle
    uint32_t spawn_count = 0;
    uint32_t respawn_count = 0;

    // Contact state transitions (incremented each frame they are true)
    uint32_t grounded_frames = 0;
    uint32_t airborne_frames = 0;
    uint32_t wall_contact_frames = 0;

    // Numeric validity failures (should stay zero)
    uint32_t invalid_position_count = 0;
    uint32_t invalid_velocity_count = 0;

    // Motor phase state (encoded as small integers for compact logging)
    // 0=Normal, 1=Dashing, 2=Skidding, 3=Climbing
    uint8_t last_movement_state = 0;

    // Named events (incremented once per occurrence)
    uint32_t landing_events = 0;
    uint32_t dash_start_events = 0;
    uint32_t dash_end_events = 0;
    uint32_t wall_jump_events = 0;
    uint32_t climb_start_events = 0;

    // Surface / carry sample counters (Inc 7).  These are increment-scoped
    // probes -- the hardware budget check runs in Inc 9.
    // moving_surface_count: live MovingSurface count seen this frame.
    // slope_ground_count: frames the player was grounded on a non-flat normal.
    // snap_recoveries_count: frames the motor recovered ground via ground-snap
    //                        (was airborne post-sweep, was_grounded last frame).
    uint32_t moving_surface_count = 0;
    uint32_t slope_ground_count = 0;
    uint32_t snap_recoveries_count = 0;

    // Snapshot of current frame values (for delta comparison)
    Vec3 last_position;
    Vec3 last_velocity;
    bool last_grounded = false;

    void BeginFrame() { ++frame_index; }

    void RecordPlayerState(const PlayerState& state);

    // Inc 7: surface/carry sample counters.
    //   live_moving_surfaces -- room.moving_surface_count.
    //   ground_normal_y -- state.contact.ground_normal.y (only used when grounded).
    //   snap_recovered  -- motor recovered ground via ground-snap this frame.
    void RecordSurfaceSample(uint32_t live_moving_surfaces, bool grounded,
                             float ground_normal_y, bool snap_recovered);

    void RecordLanding() { ++landing_events; }
    void RecordDashStart() { ++dash_start_events; }
    void RecordDashEnd() { ++dash_end_events; }
    void RecordWallJump() { ++wall_jump_events; }
    void RecordClimbStart() { ++climb_start_events; }
    void RecordSpawn() { ++spawn_count; }
    void RecordRespawn() { ++respawn_count; }

    // Print compact telemetry line for debugf / serial comparison.
    void PrintLine() const;
};

}  // namespace madeline_cube
