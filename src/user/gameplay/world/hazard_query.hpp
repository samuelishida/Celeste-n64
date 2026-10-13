#pragma once

#include <cmath>

#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

inline bool HazardContainsPoint(const HazardVolumeSpec& hazard,
                                const Vec3& point) {
    const AABB& bounds = hazard.bounds;
    return !(point.x < bounds.min.x || point.y < bounds.min.y ||
             point.z < bounds.min.z || point.x > bounds.max.x ||
             point.y > bounds.max.y || point.z > bounds.max.z);
}

// OG World.Overlaps<SpikeBlock>(point) tests actor WorldBounds only; it does
// not apply SpikeBlock's lethal-velocity direction predicate.
inline bool AnySpikeBlockOverlapsAtPoint(const MapSpecV2& map,
                                         const Vec3& point) {
    for (int i = 0; i < map.hazard_count; ++i) {
        const HazardVolumeSpec& hazard = map.hazards[i];
        if (hazard.kind == kHazardSpikeBlock &&
            HazardContainsPoint(hazard, point)) return true;
    }
    return false;
}

// Match Player.Update's World.Overlaps<DeathBlock/SpikeBlock>(waistPoint).
// SpikeBlock additionally applies Vec3.Dot(velocity.Normalized(), Direction)
// < 0.5. System.Numerics normalization of zero velocity yields NaNs, making
// that predicate false; preserve that edge case instead of safe-normalizing.
inline bool HazardKillsAtPoint(const HazardVolumeSpec& hazard,
                               const Vec3& point,
                               const Vec3& velocity) {
    if (!HazardContainsPoint(hazard, point)) return false;
    if (hazard.kind == kHazardDeathBlock) return true;
    if (hazard.kind != kHazardSpikeBlock) return false;

    const float speed2 = velocity.x * velocity.x +
                         velocity.y * velocity.y +
                         velocity.z * velocity.z;
    if (speed2 == 0.0f) return false;
    const float inv_speed = 1.0f / std::sqrt(speed2);
    const float dot = (velocity.x * inv_speed) * hazard.spike_direction.x +
                      (velocity.y * inv_speed) * hazard.spike_direction.y +
                      (velocity.z * inv_speed) * hazard.spike_direction.z;
    return dot < 0.5f;
}

inline bool AnyHazardKillsAtPoint(const MapSpecV2& map,
                                  const Vec3& point,
                                  const Vec3& velocity) {
    for (int i = 0; i < map.hazard_count; ++i)
        if (HazardKillsAtPoint(map.hazards[i], point, velocity)) return true;
    return false;
}

}  // namespace madeline_cube
