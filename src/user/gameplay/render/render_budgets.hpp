#pragma once

#include <cstdint>

namespace madeline_cube {

// Per-frame render budgets (arch.md §31). Hard caps on per-frame work so
// visibility can never create unbounded work. Host-safe — no N64 types.
//
// These are initial estimates; recalibrate at the first Ares measurement
// (see the plan's "Budget recalibration process").
inline constexpr int kMaxVisibleCells = 9;            // near ring: center + 8
inline constexpr int kMaxTrianglesPerFrame = 6000;    // near pass combined
inline constexpr int kMaxMaterialChangesPerFrame = 128;
inline constexpr int kMaxTextureUploadsPerFrame = 64;
inline constexpr int kMaxStreamOpsPerFrame = 4;       // tile loads/evictions
inline constexpr int kMaxParticlesPerFrame = 128;     // reserved (deferred)

// Per-frame render counts, incremented by the render passes and asserted
// against the budgets after the frame.
struct RenderCounts {
    int visible_cells = 0;
    int triangles = 0;
    int material_changes = 0;
    int texture_uploads = 0;
    int stream_ops = 0;
    int particles = 0;
};

// Returns true if ANY count exceeds its cap.
inline bool BudgetsExceeded(const RenderCounts& c) {
    return c.visible_cells > kMaxVisibleCells ||
           c.triangles > kMaxTrianglesPerFrame ||
           c.material_changes > kMaxMaterialChangesPerFrame ||
           c.texture_uploads > kMaxTextureUploadsPerFrame ||
           c.stream_ops > kMaxStreamOpsPerFrame ||
           c.particles > kMaxParticlesPerFrame;
}

}  // namespace madeline_cube
