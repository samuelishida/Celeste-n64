#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only
class MapSpecV2;        // host-safe fwd

// render-distance Inc 4: the whole-map horizon tier. Loads every distant
// cell's baked `.dlod` (heavily decimated, flat-color) once at boot and draws
// them as the far background using the SAME near-pass camera/projection (NO
// mid-frame projection switch — that was the historical z-split corruption
// root cause, removed by design here). Cells inside the resident near ring
// are skipped (the near pass draws them in full detail). Cheap: 45 × ~1.4 KB
// decimated cells ≈ tens of faces each.
class DistantTierRenderer {
public:
    DistantTierRenderer();
    ~DistantTierRenderer();

    // Load every distant cell's `.dlod` once. `build_dir` is null on device.
    // Idempotent (first call loads; later calls no-op).
    void LoadAll(const MapSpecV2& spec, const char* build_dir);

    // Mark a cell as near-drawn so the distant tier skips it (avoids the
    // near/distant overlap on the same cell).
    void SetNearCell(int cell_ix, int cell_iz);

    // Draw the whole horizon (call within a t3d frame). Uses the same camera
    // as the near pass — no projection switch.
    void Draw(const CameraDesc& cam);

    // Free all distant renderers.
    void FreeAll();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace madeline_cube
