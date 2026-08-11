#pragma once

#include <cstddef>

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "gameplay/render/tile_visibility.hpp"  // Mat4

namespace madeline_cube {

class LvlRoomRenderer;

// Render-only near-pass tile streamer (Inc 2: stub; Inc 3: full camera-driven
// top-view visibility + LRU resident pool).
//
// The public API is stable across increments so the orchestrator can call it:
//   UpdateCamera(camera_pos, inv_view_proj, ground_y)  -> compute visible set
//   DrawLowPriority(cam)                               -> Z-off subpass
//   DrawHighPriority(cam)                              -> Z-on main tiles
//
// Inc 2 stub: the class exists so `OpenWorldRenderer` can hold it by pointer
// and exercise the frame order, but it draws nothing and keeps no resident
// tiles yet. Inc 3 replaces the internal pool with the real streamer.
class TileStreamer {
public:
    TileStreamer() = default;
    ~TileStreamer();
    TileStreamer(const TileStreamer&) = delete;
    TileStreamer& operator=(const TileStreamer&) = delete;

    // Inc 3 fills this in: derive the visibility polygon, scanline-enumerate
    // visible tiles, request missing blocks, and evict LRU residents.
    // Inc 2: no-op (no resident pool yet).
    void UpdateCamera(const Vec3& camera_pos, const Mat4& inv_view_proj,
                      float ground_y);

    // Inc 5 renders water/background with Z off; Inc 2 stub is a no-op.
    void DrawLowPriority(const CameraDesc& cam);

    // Draw all resident detailed tiles with Z on. Inc 2 stub: no-op.
    void DrawHighPriority(const CameraDesc& cam);

    // Number of resident detailed tiles (Inc 2: always 0).
    int ResidentCount() const { return resident_count_; }

private:
    LvlRoomRenderer* residents_ = nullptr;
    int resident_count_ = 0;
    int resident_capacity_ = 0;
};

}  // namespace madeline_cube
