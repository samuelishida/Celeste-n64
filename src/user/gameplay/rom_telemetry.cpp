#include "rom_telemetry.hpp"

#ifdef N64
#include <debug.h>
#else
#include <cstdio>
#endif

#include "physics_contracts.hpp"
#include "debug_flags.hpp"

namespace madeline_cube {

namespace {
constexpr uint8_t kClimbTracePreTicks = 60;
constexpr uint8_t kClimbTracePostTicks = 30;
constexpr size_t kClimbTraceCapacity = kClimbTracePreTicks;

struct ClimbTraceRing {
    ClimbTraceFrame frames[kClimbTraceCapacity]{};
    ClimbTraceFrame capture[kClimbTracePreTicks + kClimbTracePostTicks]{};
    size_t next = 0;
    size_t count = 0;
    size_t captured = 0;
    size_t pre_count = 0;
    size_t drain_index = 0;
    uint8_t post_ticks_remaining = 0;
    bool draining = false;
};

ClimbTraceRing& GetClimbTraceRing() {
    static ClimbTraceRing ring;
    return ring;
}

#ifdef N64
void PrintClimbTraceFrame(const ClimbTraceFrame& f, const char* phase) {
    debugf("[climb-window] phase=%s tick=%u raw=(%d,%d,%d) intent=%u move=(%.2f,%.2f) "
           "state=%u>%u query=%u face=%d normal=(%.3f,%.3f,%.3f) "
           "anchor=(%.2f,%.2f,%.2f) p0=(%.2f,%.2f,%.2f) "
           "collision_correction=(%.3f,%.3f,%.3f) "
           "p1=(%.2f,%.2f,%.2f) v0=(%.2f,%.2f,%.2f) "
           "v1=(%.2f,%.2f,%.2f) event=%u release=%d\n",
           phase, static_cast<unsigned int>(f.tick), static_cast<int>(f.raw_z),
           static_cast<int>(f.raw_l), static_cast<int>(f.raw_r),
           static_cast<unsigned int>(f.input_climb),
           static_cast<double>(f.move_input.x), static_cast<double>(f.move_input.y),
           static_cast<unsigned int>(f.state_before),
           static_cast<unsigned int>(f.state_after),
           static_cast<unsigned int>(f.query_hit), static_cast<int>(f.query_face),
           static_cast<double>(f.query_normal.x),
           static_cast<double>(f.query_normal.y),
           static_cast<double>(f.query_normal.z),
           static_cast<double>(f.anchor.x), static_cast<double>(f.anchor.y),
           static_cast<double>(f.anchor.z),
           static_cast<double>(f.position_before.x),
           static_cast<double>(f.position_before.y),
           static_cast<double>(f.position_before.z),
           static_cast<double>(f.collision_correction.x),
           static_cast<double>(f.collision_correction.y),
           static_cast<double>(f.collision_correction.z),
           static_cast<double>(f.position_after.x),
           static_cast<double>(f.position_after.y),
           static_cast<double>(f.position_after.z),
           static_cast<double>(f.velocity_before.x),
           static_cast<double>(f.velocity_before.y),
           static_cast<double>(f.velocity_before.z),
           static_cast<double>(f.velocity_after.x),
           static_cast<double>(f.velocity_after.y),
           static_cast<double>(f.velocity_after.z),
           static_cast<unsigned int>(f.event), static_cast<int>(f.release_reason));
}
#endif
}  // namespace

void RecordClimbTraceFrame(const ClimbTraceFrame& frame) {
    // USB debug output is synchronous. Normal gameplay must not dump a
    // 60-line floating-point trace when the player grabs a wall.
    if (!kDebugInputRecording) return;
    ClimbTraceRing& ring = GetClimbTraceRing();
    ring.frames[ring.next] = frame;
    ring.next = (ring.next + 1) % kClimbTraceCapacity;
    if (ring.count < kClimbTraceCapacity) ++ring.count;

    if (ring.draining) {
#ifdef N64
        PrintClimbTraceFrame(ring.capture[ring.drain_index],
                            ring.drain_index < ring.pre_count ? "pre" : "post");
#endif
        if (++ring.drain_index == ring.captured) {
            ring.draining = false;
#ifdef N64
            debugf("[climb-window] end tick=%u\n", static_cast<unsigned int>(frame.tick));
#endif
        }
    } else if (frame.event != 0 && ring.post_ticks_remaining == 0) {
        const size_t first = (ring.next + kClimbTraceCapacity - ring.count) %
                             kClimbTraceCapacity;
        for (size_t i = 0; i < ring.count; ++i) {
            ring.capture[i] = ring.frames[(first + i) % kClimbTraceCapacity];
        }
        ring.captured = ring.count;
        ring.pre_count = ring.count;
        ring.post_ticks_remaining = kClimbTracePostTicks;
    } else if (ring.post_ticks_remaining > 0) {
        ring.capture[ring.captured++] = frame;
        --ring.post_ticks_remaining;
        if (ring.post_ticks_remaining == 0) {
            ring.draining = true;
            ring.drain_index = 0;
#ifdef N64
            debugf("[climb-window] begin event=%u samples=%u post=%u\n",
                   static_cast<unsigned int>(ring.capture[ring.captured - kClimbTracePostTicks - 1].event),
                   static_cast<unsigned int>(ring.captured),
                   static_cast<unsigned int>(kClimbTracePostTicks));
#endif
        }
    }
}

void RomTelemetry::PrintLine() const {
#ifdef N64
    debugf("[telemetry] f=%u sp=%u rp=%u g=%u a=%u w=%u ip=%u iv=%u st=%u "
           "L=%u Ds=%u De=%u W=%u C=%u M=%u ms=%u sl=%u sn=%u "
           "qr=%u qs=%u qo=%u qb=%u "
           "room=%s fnorm=%.3f orig=(%.1f,%.1f,%.1f) "
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
          static_cast<unsigned int>(climb_mantle_events),
          static_cast<unsigned int>(moving_surface_count),
          static_cast<unsigned int>(slope_ground_count),
           static_cast<unsigned int>(snap_recoveries_count),
           static_cast<unsigned int>(query_raycasts),
           static_cast<unsigned int>(query_sphere_sweeps),
           static_cast<unsigned int>(query_overlap_queries),
           static_cast<unsigned int>(query_bvh_nodes_touched),
           active_room[0] ? active_room : "-",
           static_cast<double>(floor_normal_y),
           static_cast<double>(render_origin[0]),
           static_cast<double>(render_origin[1]),
           static_cast<double>(render_origin[2]),
           static_cast<double>(last_position.x),
           static_cast<double>(last_position.y),
           static_cast<double>(last_position.z),
           static_cast<double>(last_velocity.x),
           static_cast<double>(last_velocity.y),
           static_cast<double>(last_velocity.z));
#else
    std::printf("[telemetry] f=%u sp=%u rp=%u g=%u a=%u w=%u ip=%u iv=%u st=%u "
                "L=%u Ds=%u De=%u W=%u C=%u M=%u ms=%u sl=%u sn=%u "
                "qr=%u qs=%u qo=%u qb=%u "
                "room=%s fnorm=%.3f orig=(%.1f,%.1f,%.1f) "
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
                static_cast<unsigned int>(climb_mantle_events),
                static_cast<unsigned int>(moving_surface_count),
                static_cast<unsigned int>(slope_ground_count),
                static_cast<unsigned int>(snap_recoveries_count),
                static_cast<unsigned int>(query_raycasts),
                static_cast<unsigned int>(query_sphere_sweeps),
                static_cast<unsigned int>(query_overlap_queries),
                static_cast<unsigned int>(query_bvh_nodes_touched),
                active_room[0] ? active_room : "-",
                static_cast<double>(floor_normal_y),
                static_cast<double>(render_origin[0]),
                static_cast<double>(render_origin[1]),
                static_cast<double>(render_origin[2]),
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
    // Inc 3: Store floor_normal_y for diagnostic printing.
    floor_normal_y = grounded ? ground_normal_y : 0.0f;
}

void RomTelemetry::RecordActiveRoom(const char* room_id, float fnorm_y,
                                    const Vec3& origin) {
    if (room_id != nullptr) {
        // Safe copy: max 15 chars + null terminator.
        std::strncpy(active_room, room_id, 15);
        active_room[15] = '\0';
    } else {
        active_room[0] = '\0';
    }
    floor_normal_y = fnorm_y;
    render_origin[0] = origin.x;
    render_origin[1] = origin.y;
    render_origin[2] = origin.z;
}

void RomTelemetry::RecordCollisionQueryCounters(const CollisionQueryCounters& counters) {
    query_raycasts = counters.raycasts;
    query_sphere_sweeps = counters.sphere_sweeps;
    query_overlap_queries = counters.overlap_queries;
    query_bvh_nodes_touched = counters.bvh_nodes_touched;
}

void RomTelemetry::RecordPlayerState(const PlayerState& state) {
    // Update contact frame counters
    if (state.grounded) {
        ++grounded_frames;
    } else {
        ++airborne_frames;
    }

    if (state.wall_contact) {
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
