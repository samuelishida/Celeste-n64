#include "gameplay/actor/hazard.hpp"

namespace madeline_cube {

bool CheckHazardKill(const Room& room,
                     const Vec3& player_pos,
                     float player_radius,
                     uint8_t* kind_out) {
    const float radius_sq = player_radius * player_radius;
    for (int i = 0; i < room.hazard_count; ++i) {
        const HazardVolume& hazard = room.hazards[i];
        const Vec3 closest = {
            Clamp(player_pos.x, hazard.center.x - hazard.half_extents.x, hazard.center.x + hazard.half_extents.x),
            Clamp(player_pos.y, hazard.center.y - hazard.half_extents.y, hazard.center.y + hazard.half_extents.y),
            Clamp(player_pos.z, hazard.center.z - hazard.half_extents.z, hazard.center.z + hazard.half_extents.z),
        };
        const float dx = player_pos.x - closest.x;
        const float dy = player_pos.y - closest.y;
        const float dz = player_pos.z - closest.z;
        if ((dx * dx) + (dy * dy) + (dz * dz) <= radius_sq) {
            if (kind_out) *kind_out = hazard.kind;
            return true;
        }
    }

    return false;
}

}  // namespace madeline_cube
