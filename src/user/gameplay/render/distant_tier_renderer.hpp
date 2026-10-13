#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/fog_math.hpp"
#include "gameplay/render/pass_camera_math.hpp"

namespace madeline_cube {

class LvlRoomRenderer;  // N64-only
class MapSpecV2;        // host-safe fwd
class TileStreamer;

// Whole-map horizon tier. Loads one DLOD direction per cell, frustum-culls
// cells, then draws visible blocks under one shared camera-relative matrix.
// Cells submitted by the near pass are omitted; all others retain DLOD coverage.
class DistantTierRenderer {
public:
    DistantTierRenderer();
    ~DistantTierRenderer();

    // Load every distant cell's `.dlod` once. `build_dir` is null on device.
    // Idempotent (first call loads; later calls no-op).
    void LoadAll(const MapSpecV2& spec, const char* build_dir);

    // Draw visible horizon blocks using distant projection. Caller owns the
    // viewport attach and pass boundary. A resident owns its cell only when
    // the near pass will draw it this frame; pending or culled cells keep DLOD.
    void Draw(const CameraDesc& cam, struct RenderCounters* counters,
              const TileStreamer& near_streamer);

    // Free all distant renderers.
    void FreeAll();

    // Horizon fog (Inc 6): fade the distant tier into atmosphere. Applied
    // around the DLOD draw; torn down before the near pass.
    void SetFog(const FogParams& fog);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace madeline_cube
