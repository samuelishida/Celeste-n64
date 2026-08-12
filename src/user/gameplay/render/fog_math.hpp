#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Fog parameter math (arch.md §26-27). Host-safe — no N64 types.
struct FogParams {
    bool enabled = false;
    Vec3 color = {120.0f, 150.0f, 180.0f};
    float min = 300.0f;
    float max = 1200.0f;
};

// The maximum allowed fog min-distance (arch.md §27 clamps excessively high
// values because they can cause RDP problems). Raised from 1000 to 4000 so the
// fog onset can track the distant projection's far plane (full map diagonal,
// ~2700 for Forsaken City) — the z-split fix widened the distant far, and the
// old 1000 clamp would silently cap the fog start.
inline constexpr float kFogMaxMinDistance = 4000.0f;

// Build fog params, clamping `min_dist` to a sane maximum (arch.md §27).
inline FogParams MakeFog(float min_dist, float max_dist, const Vec3& color) {
    FogParams f;
    f.enabled = true;
    f.color = color;
    f.min = min_dist > kFogMaxMinDistance ? kFogMaxMinDistance : min_dist;
    f.max = max_dist;
    return f;
}

// Returns true if the fog range is valid (min < max, both >= 0).
inline bool ValidateFogRange(const FogParams& f) {
    return f.min >= 0.0f && f.max > f.min;
}

}  // namespace madeline_cube
