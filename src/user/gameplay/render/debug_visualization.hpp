#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Debug visualization helpers (arch.md §43). Host-safe color + boundary math;
// the device-only draw functions live in the renderer. Exposes tile
// boundaries, LOD level, pass isolation, and streaming state.
//
// Host-safe — no N64 types.

// Map a LOD level to a distinct color (for the LOD-level debug overlay).
inline uint32_t DebugColorForLod(int lod_level) {
    switch (lod_level) {
        case 0: return 0x00FF00FF;  // green — highest detail
        case 1: return 0xFFFF00FF;  // yellow
        case 2: return 0xFF8800FF;  // orange
        case 3: return 0xFF0000FF;  // red — coarsest
        default: return 0xFFFFFFFF;
    }
}

// Map a pass name to a distinct color (for the pass-isolation overlay).
inline uint32_t DebugColorForPass(const char* pass_name) {
    if (!pass_name) return 0xFFFFFFFF;
    // Compare the leading characters to avoid strcmp in a hot path.
    switch (pass_name[0]) {
        case 'd': return 0x0000FFFF;  // distant — blue
        case 'l': return 0x00FFFFFF;  // low_priority — cyan
        case 'h': return 0xFF00FFFF;  // high_priority — magenta
        case 's': return 0x888888FF;  // skybox — grey
        default: return 0xFFFFFFFF;
    }
}

// Compute the 4 corner vertices of a tile/cell boundary (a closed square in
// world XZ at `y`). `out` receives 4 points in order (bottom-left, bottom-right,
// top-right, top-left) so a line loop forms a closed square. Host-safe.
inline void TileBoundaryCorners(const Vec3& origin, float size, float y,
                                Vec3 out[4]) {
    const float half = size * 0.5f;
    out[0] = {origin.x - half, y, origin.z - half};
    out[1] = {origin.x + half, y, origin.z - half};
    out[2] = {origin.x + half, y, origin.z + half};
    out[3] = {origin.x - half, y, origin.z + half};
}

}  // namespace madeline_cube
