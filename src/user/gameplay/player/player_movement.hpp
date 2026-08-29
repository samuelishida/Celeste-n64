#pragma once

#include "gameplay/player/player_state.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

// OG Celeste64 single-mover port (og-mover-overhaul Inc 1).
//
// OG Celeste64 has exactly ONE mover (SweepTestMove + Popout) called once per
// tick from the state machine (Player.cs:341-373, 642-718). The demake instead
// had four parallel repair paths that all had to agree every tick, which is
// why four climb/physics repair cycles failed to converge and Madeline began
// clipping through walls and floors. MoveService RELOCATES that one mover here
// with OG's exact structure; it is dead code (tests only) until Inc 2 wires
// PlayerController through it.
//
// +Y up (N64/port); OG is +Z up. Every OG Z offset converts feet-anchored:
//   waist = center.y - half_height + 3, head = center.y - half_height + 10,
//   ground ray origin = center.y - half_height + 5.
struct MoveServiceConfig {
    float substep = 2.0f;                 // OG stepSize (Player.cs:646)
    float wall_radius = 3.0f;             // OG WallPushoutDist (Player.cs:50)
    float ground_ray_up = 5.0f;           // OG GroundCheck ray origin up from feet
    float ground_ray_len = 5.01f;         // OG GroundCheck ray length
    float ceiling_probe_up = 1.0f;        // OG CeilingCheck origin up from feet
    float ceiling_height = 12.0f;         // OG CeilingCheck Height
    float climb_wall_radius = 3.0f;       // climb popout radius (= WallPushoutDist)
    float ground_snap_distance = 5.0f;    // OG ground snap ray length (was demake 50)
    float ground_snap_cooldown = 0.10f;   // OG tGroundSnapCooldown (Player.cs:713)
    float half_height = 10.0f;            // demake body half-height
};

class MoveService {
public:
    explicit MoveService(MoveServiceConfig config = {});

    // OG GroundCheck (Player.cs:2280-2295, +Y port): ray down ground_ray_len
    // from (feet + ground_ray_up). Returns true on a hit whose normal passes
    // FloorNormalCheck ({normal.y > 0 && |n.y| >= 0.35}). `normal` receives the
    // hit normal, `pushout` the vector that carries the feet to the hit point.
    bool GroundCheck(const PlayerState& state, const Room& room,
                     Vec3* pushout, Vec3* normal) const;

    // OG CeilingCheck (Player.cs:2297-2308, +Y port): ray up ceiling_height-1
    // from (feet + ceiling_probe_up). `hit_out` receives the ceiling point.
    bool CeilingCheck(const PlayerState& state, const Room& room, Vec3* hit_out) const;

    // OG SweepTestMove (Player.cs:642-660): move by `amount` in substep
    // increments; after each substep run Popout(resolve_impact && first_wall).
    // After the loop one unconditional Popout(false) (Player.cs:368-373).
    void SweepMove(PlayerState& state, const Room& room, const Vec3& amount,
                   bool resolve_impact) const;

    // OG Popout (Player.cs:669-718). Order: GroundCheck (pushout + on
    // resolve_impact vy=max(vy,0)) -> else CeilingCheck (vy=min(vy,0)) -> else
    // wall at waist then head (position += pushout; on resolve_impact
    // velocity -= normal*|v|*min(0, dot(vhat, normal))). Returns true if any
    // repair happened. Writes position/velocity into `state`.
    bool Popout(PlayerState& state, const Room& room, bool resolve_impact) const;

    // OG ground snap (Player.cs:399-406, 711-712, +Y port): ray down 5 from
    // the feet when recent-ground and the existing
    // ground_snap_cooldown_remaining <= 0; snaps position down to the hit.
    // Reuses ContactState::ground_snap_cooldown_remaining (no new field, M4).
    void GroundSnap(PlayerState& state, const Room& room, float dt) const;

    const MoveServiceConfig& Config() const { return config_; }

private:
    // Feet-anchored test positions (OG SolidWaistTestPos/SolidHeadTestPos).
    Vec3 SolidWaistTestPos(const PlayerState& state) const;
    Vec3 SolidHeadTestPos(const PlayerState& state) const;
    Vec3 SolidFeetPos(const PlayerState& state) const;

    MoveServiceConfig config_;
};

}  // namespace madeline_cube
