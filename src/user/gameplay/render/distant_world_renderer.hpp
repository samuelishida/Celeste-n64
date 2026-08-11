#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"

namespace madeline_cube {

// Distant-pass world renderer (Inc 2: stub; Inc 4: compressed-coordinate
// Z-off LOD renderer). Renders the coarse distant representation of the world
// so the horizon is visible, using a separate camera with compressed
// coordinates and no Z-buffer (back-to-front sorted).
class DistantWorldRenderer {
public:
    DistantWorldRenderer() = default;
    ~DistantWorldRenderer() = default;
    DistantWorldRenderer(const DistantWorldRenderer&) = delete;
    DistantWorldRenderer& operator=(const DistantWorldRenderer&) = delete;

    void UpdateCamera(const Vec3& camera_pos, const CameraDesc& cam);

    // Inc 2 stub: flips Z off briefly so the frame order is exercised, then
    // restores it. Inc 4 renders actual distant LOD cells.
    void Render(const CameraDesc& cam);
};

}  // namespace madeline_cube
