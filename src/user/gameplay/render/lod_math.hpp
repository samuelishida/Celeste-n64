#pragma once

#include <cmath>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// LOD threshold constant (arch.md §10). The squared-distance threshold below
// which a higher-detail child representation is retained.
inline constexpr float kLevel2MinDistance = 500.0f;

// Per-direction mesh index (arch.md §11-12). A distant LOD entry can hold up
// to 4 directional meshes (N/S/E/W). This returns the index for a given
// relative direction vector.
//
// The four directions are indexed:
//   0 = +Z (south, toward camera when looking -Z)
//   1 = -Z (north)
//   2 = +X (east)
//   3 = -X (west)
inline int DirectionalIndexFromDelta(const Vec3& delta) {
    const float adx = std::fabs(delta.x);
    const float adz = std::fabs(delta.z);
    if (adx >= adz) {
        return delta.x >= 0.0f ? 2 : 3;  // east / west
    }
    return delta.z >= 0.0f ? 0 : 1;      // south / north
}

// Select the directional mesh index for a tile given the camera position and
// the camera's facing direction (arch.md §12). When the camera is very close
// to the tile center (|delta.x| and |delta.z| both < `close_threshold`), use
// the camera's own direction to avoid unstable directional selection around
// the center of the world; otherwise use the tile->camera delta direction.
inline int DirectionalMeshIndex(const Vec3& camera_pos, const Vec3& tile_origin,
                                const Vec3& camera_dir, float close_threshold) {
    const Vec3 delta = {camera_pos.x - tile_origin.x,
                        camera_pos.y - tile_origin.y,
                        camera_pos.z - tile_origin.z};
    if (std::fabs(delta.x) < close_threshold &&
        std::fabs(delta.z) < close_threshold) {
        return DirectionalIndexFromDelta(camera_dir);
    }
    return DirectionalIndexFromDelta(delta);
}

// Select the LOD level for a distant tile (arch.md §10). Returns the LOD
// level (0 = highest detail, closer) whose squared-distance threshold
// `dist² < kLevel2MinDistance² * lod_scale²` is satisfied. `max_level` bounds
// the returned level (the furthest/baked representation).
//
// The rule: for each level, if the camera-to-tile distance² is LESS than the
// level's threshold, the child (higher-detail) representation is still
// required — so we return that (lower) level. When the distance exceeds all
// thresholds, we return `max_level` (the coarsest).
inline int SelectLodLevel(const Vec3& camera_pos, const Vec3& tile_origin,
                          float lod_scale, int max_level) {
    const float dx = camera_pos.x - tile_origin.x;
    const float dz = camera_pos.z - tile_origin.z;
    const float dist2 = dx * dx + dz * dz;
    const float base = kLevel2MinDistance * kLevel2MinDistance * lod_scale * lod_scale;
    for (int level = 0; level <= max_level; ++level) {
        // Higher levels have larger thresholds (parent replaces children when
        // the distance is large). Scale the base threshold by the level so
        // level 0 is the closest-switch.
        const float threshold = base * (1.0f + static_cast<float>(level));
        if (dist2 < threshold) return level;
    }
    return max_level;
}

}  // namespace madeline_cube
