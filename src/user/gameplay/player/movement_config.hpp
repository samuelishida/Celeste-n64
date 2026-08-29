#pragma once

namespace madeline_cube {

struct MovementConfig {
    // OG Celeste64 values (Y-up, no rescaling — these ARE the original values).
    float run_speed = 64.0f;
    float acceleration = 500.0f;
    float past_max_deceleration = 60.0f;
    float air_accel_mult_min = 0.5f;
    float air_accel_mult_max = 1.0f;
    float friction = 800.0f;
    float air_friction_mult = 0.1f;
    float gravity = 600.0f;
    float max_fall_speed = -120.0f;
    float half_gravity_threshold = 100.0f;
    float jump_speed = 90.0f;
    float jump_hold_time = 0.10f;
    float jump_xy_boost = 10.0f;
    float dash_speed = 140.0f;
    float dash_end_speed_multiplier = 0.75f;
    float dash_duration = 0.20f;
    float dash_reset_cooldown = 0.20f;
    float dash_cooldown = 0.10f;
    float dash_jump_speed = 40.0f;
    float dash_jump_hold_speed = 20.0f;
    float dash_jump_hold_time = 0.30f;
    float dash_jump_xy_boost = 16.0f;
    float respawn_fall_height = -200.0f;

    float wall_jump_speed_x = 83.2f;    // WallJumpXYSpeed = MaxSpeed * 1.3
    float wall_jump_speed_y = 90.0f;    // same as JumpSpeed
    float climb_speed = 40.0f;
    float climb_hop_up_speed = 80.0f;
    float climb_hop_forward_speed = 40.0f;

    // OG Celeste64 climb: unlimited grip — no stamina, no material gate.
    float climb_hop_no_move_time = 0.25f;   // no-move time after ledge hop
    float climb_cooldown = 0.30f;           // cooldown after leaving climb before re-enter

    // ── OG climb probe anchors (climb-parity-3 Inc 1; OG Z-up → demake +Y-up).
    // Feet-anchored: feet = center.y - player_half_height. OG
    // SolidWaistTestPos = feet + 3, head = feet + 10 (Player.cs:843-851).
    float player_half_height = 10.0f;       // == MoveServiceConfig::half_height
    float climb_waist_offset = 3.0f;        // grip probe height above feet (OG SolidWaistTestPos)
    float climb_head_offset = 10.0f;        // OG head probe band top (feet + 10)
    float climb_ledge_probe_extra = 1.0f;   // mantle probe = waist + 1 (OG ClimbCheckAt +1 UnitZ)
    float climb_check_dist = 4.0f;          // grip/ledge/corner query radius (OG ClimbCheckDist)
    float climb_pushout_dist = 3.0f;        // entry reposition target (OG WallPushoutDist)
    float climb_corner_ease_time = 0.20f;   // corner position/facing ease (OG 0.2 s)

    // Entry visibility radius (climb-parity-3 Inc 1/2): the entry probe uses
    // this (5.0 = motor tangent rest distance) because at exact tangency her
    // waist rests one unit beyond climb_check_dist (4) reach, yet OG still
    // grabs there (Popout first pulls her in; dist == radius counts as
    // touching). Entry then re-anchors to the OG TryClimb spot, after which
    // the r4 sustain probe governs. NOT a deprecated alias — the entry probe
    // is its only consumer and it stays.
    float wall_probe_radius = 5.0f;                 // == MoveServiceConfig::wall_radius (3)/motor radius (5) legacy
};

struct MovementProfile {
    float run_max_speed = 64.0f;
    float ground_acceleration = 500.0f;
    float ground_deceleration = 800.0f;
    float air_acceleration = 500.0f;
    float air_turn_acceleration = 500.0f;
    float past_max_decel = 60.0f;
    float rotate_threshold = 12.8f;
    float rotate_speed = 9.424778f;  // Tau * 1.5
    float rotate_speed_above_max = 3.769911f;  // Tau * 0.6
    float dash_rotate_speed = 1.884956f;     // Tau * 0.3
    float coyote_time = 0.12f;
    float jump_buffer_time = 0.08f;

    float dash_speed = 140.0f;
    float dash_hitstop_time = 0.02f;
    float dash_active_time = 0.2f;
    float dash_cooldown = 0.10f;
    float dash_long_jump_speed = 100.0f;

    float wall_jump_horizontal_speed = 83.2f;
    float wall_jump_vertical_speed = 90.0f;
    float neutral_wall_jump_horizontal_speed = 40.0f;
    float neutral_wall_jump_vertical_speed = 105.0f;

    float climb_speed = 40.0f;
};

}  // namespace madeline_cube
