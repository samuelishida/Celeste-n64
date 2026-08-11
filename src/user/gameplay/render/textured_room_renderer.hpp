#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/material_catalog.hpp"

namespace madeline_cube {

// Textured near-pass room renderer (Inc 5). Wraps `LvlRoomRenderer`-style
// LVL2 packing but draws each material batch with the resolved sprite from a
// `MaterialCatalog`, instead of the flat per-material primColor. This is a
// NEW file (not an extension of `lvl_room_renderer.cpp`) so the flat-color
// `LvlRoomRenderer` stays intact as the validated fallback when
// `kEnableTextures` is off.
//
// Device-only (includes libdragon/t3d + sprite.h). The near pass
// (`TileStreamer::DrawHighPriority`) instantiates this when texturing is on.
class TexturedRoomRenderer {
public:
    TexturedRoomRenderer() = default;
    ~TexturedRoomRenderer() { Free(); }
    TexturedRoomRenderer(const TexturedRoomRenderer&) = delete;
    TexturedRoomRenderer& operator=(const TexturedRoomRenderer&) = delete;

    // Load faces + vertices from a .lvl file, packing against `render_origin`
    // at `pos_scale` (near default 32). `catalog` is the material catalog used
    // to resolve each batch's sprite at draw time. Returns true on success.
    bool Load(const char* lvl_path, const Vec3& render_origin,
              const MaterialCatalog* catalog,
              float pos_scale = kDefaultPosScale);

    // Free all allocated resources.
    void Free();

    // Draw all room geometry with per-batch textured combiners. Call within a
    // t3d_frame_start/end pair. A batch whose material has no sprite falls
    // back to the flat primColor (never a crash).
    void Draw() const;

    bool IsLoaded() const { return verts_ != nullptr; }

    // Recompute the model-matrix translation so the drawn world is expressed
    // camera-relative (same coupling as `LvlRoomRenderer::SetCameraPosition`).
    void SetCameraPosition(const Vec3& camera_pos);

    // Number of faces discarded because they exceeded the batch cap.
    int DiscardedFaces() const { return discarded_faces_; }

private:
    static constexpr int kMaxBatches = 1024;
    static constexpr float kDefaultPosScale = 32.0f;

    float kPosScale = kDefaultPosScale;
    float kInvScale = 1.0f / kDefaultPosScale;

    struct Batch {
        uint32_t first_vertex;
        uint32_t vertex_count;
        uint32_t tri_count;
        uint16_t material_id;
    };

    T3DVertPacked* verts_ = nullptr;
    uint32_t vert_count_ = 0;
    uint32_t pair_count_ = 0;

    Batch batches_[kMaxBatches];
    int batch_count_ = 0;
    int discarded_faces_ = 0;

    T3DMat4FP* matrix_fp_ = nullptr;
    Vec3 render_origin_ = {0.0f, 0.0f, 0.0f};
    const MaterialCatalog* catalog_ = nullptr;
};

}  // namespace madeline_cube
