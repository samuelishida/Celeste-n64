#pragma once

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"
#include "n64/frame_arena.hpp"
#include "n64/profiler.hpp"

namespace madeline_cube {

// The two-pass camera set derived from a single world-space camera.
// `near_cam` uses the normal gameplay clip planes; `distant_cam` uses the
// compressed `arch.md` §5 clip planes. Both share the same orientation.
struct PassCameras {
    CameraDesc near_cam;
    CameraDesc distant_cam;
};

// Derive both pass cameras from one world-space camera. `tile_size` is the
// world tile/cell size; `lod_scale` is the coordinate packing scale for the
// distant pass (used in Inc 4), NOT a clip-plane multiplier.
// Host-testable — no N64 types.
inline PassCameras BuildPassCameras(const Vec3& camera_pos,
                                    const Vec3& camera_target,
                                    float fov_deg, float near_plane,
                                    float far_plane, float tile_size,
                                    float lod_scale) {
    const Vec3 up = {0.0f, 1.0f, 0.0f};
    PassCameras p;
    p.near_cam = MakeNearCamera(fov_deg, near_plane, far_plane,
                                camera_pos, camera_target, up);
    p.distant_cam = MakeDistantCamera(p.near_cam, tile_size, lod_scale);
    return p;
}

// The documented `arch.md` §21 frame-stage order.
enum class FrameStage {
    Distant,       // Z off, compressed coordinates, back-to-front sort
    LowPriority,   // near subpass, Z off (water/background)
    HighPriority,  // near main pass, Z on
    Present,
};

inline const char* FrameStageName(FrameStage s) {
    switch (s) {
        case FrameStage::Distant: return "distant";
        case FrameStage::LowPriority: return "low_priority";
        case FrameStage::HighPriority: return "high_priority";
        case FrameStage::Present: return "present";
    }
    return "unknown";
}

// The canonical stage sequence (host-testable). Returns 4 entries in the
// documented order so a host test can assert the frame order without calling
// the device-only `OpenWorldRenderer::Render`.
inline void OrderedFrameStages(FrameStage out[4]) {
    out[0] = FrameStage::Distant;
    out[1] = FrameStage::LowPriority;
    out[2] = FrameStage::HighPriority;
    out[3] = FrameStage::Present;
}

// N64-only renderer types, forward-declared so this header stays host-safe.
class TileStreamer;           // near pass (Inc 3 resident pool)
class DistantWorldRenderer;   // distant pass (Inc 4 fleshes out)
class Skybox;                 // skybox (Inc 6)

// Device-only render orchestrator. Owns the near tile streamer and the
// distant world renderer by pointer (created in the .cpp), so this header
// never pulls in libdragon/t3d. Drives the `arch.md` §21 frame order.
class OpenWorldRenderer {
public:
    OpenWorldRenderer();
    ~OpenWorldRenderer();
    OpenWorldRenderer(const OpenWorldRenderer&) = delete;
    OpenWorldRenderer& operator=(const OpenWorldRenderer&) = delete;

    // Full frame: skybox (Inc 6), distant (Inc 4), low-priority, high-priority.
    void Render(const PassCameras& cams);

    // Individual passes (called by Render; exposed for per-phase profiling in
    // Inc 7 and for the distant/near split).
    void RenderDistant(const CameraDesc& cam);
    void RenderLowPriority(const CameraDesc& cam);
    void RenderHighPriority(const CameraDesc& cam);

    // Inc 2/3 near-pass management (delegates to the active near renderer).
    void SetCenter(const class MapSpecV2& spec, const class V2RoomSpec& center,
                   const char* build_dir);
    void SetCameraPosition(const Vec3& camera_pos);

    // Set the material catalog for the textured near pass (Inc 5). Forwarded
    // to the tile streamer. The catalog is owned by the caller.
    void SetMaterialCatalog(const class MaterialCatalog* catalog);

    // Set the fog applied to the distant pass (Inc 6).
    void SetFog(const class FogParams& fog);

    // Reset the frame-scoped arena at the start of each frame (Inc 7).
    void BeginFrame();

    // The frame-scoped arena for transient per-frame allocations (Inc 7).
    n64::FrameArena& Arena() { return arena_; }

    // Per-phase profiler (Inc 7). Reports per-pass timing.
    n64::FrameProfiler& Profiler() { return profiler_; }

private:
    TileStreamer* tile_streamer_ = nullptr;  // Inc 3 near pass
    DistantWorldRenderer* distant_ = nullptr;
    Skybox* skybox_ = nullptr;
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};
    n64::FrameArena arena_;  // frame-scoped transient allocations
    n64::FrameProfiler profiler_;  // per-phase timing
};

}  // namespace madeline_cube
