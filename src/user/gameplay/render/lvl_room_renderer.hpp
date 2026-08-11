#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Renders baked room geometry directly from .lvl face/vertex data.
// Bypasses the .t3dm / gltf_to_t3d pipeline entirely — builds T3DVertPacked
// pairs in code and draws with manual t3d_vert_load + t3d_tri_draw.
// This is the same proven approach as the diagnostic test triangle.
class LvlRoomRenderer {
public:
    LvlRoomRenderer() = default;
    ~LvlRoomRenderer() { Free(); }

    // Load faces + vertices from a .lvl file. Returns true on success.
    // `render_origin` is subtracted from each vertex before fixed-point
    // packing so the full map's absolute world coordinates do not overflow
    // the int16 packing (chunk-local rendering). The scene renders the
    // player/camera/actors in the same local frame.
    //
    // `pos_scale` is the fixed-point precision (kPosScale). The near pass
    // uses the default 32; the distant pass (Inc 4) passes a compressed
    // `kLodScale` so coarse distant meshes pack far inside int16 range.
    bool Load(const char* lvl_path, const Vec3& render_origin = {0.0f, 0.0f, 0.0f},
              float pos_scale = kDefaultPosScale);

    // Free all allocated resources.
    void Free();

    // Draw all room geometry. Call within a t3d_frame_start/end pair.
    void Draw() const;

    bool IsLoaded() const { return verts_ != nullptr; }

    // Recompute the model-matrix translation so the drawn world is expressed
    // camera-relative: the matrix translates by `render_origin_ - camera_pos`
    // instead of `render_origin_`. Vertices stay packed against their fixed
    // per-cell render origin (no per-frame re-packing). The near-pass view
    // must ALSO be camera-at-origin (see the CRITICAL coupling note in
    // gameplay_scene.cpp) or geometry is double-offset by `-camera`.
    void SetCameraPosition(const Vec3& camera_pos);

    // Number of faces discarded because they exceeded the batch cap. Must
    // remain zero for a validated artifact.
    int DiscardedFaces() const { return discarded_faces_; }

    // The render origin this renderer was loaded with (world units). Stored
    // as a plain Vec3 so host tests can assert it without any N64 dependency.
    const Vec3& RenderOrigin() const { return render_origin_; }

private:
    static constexpr int kMaxBatches = 1024;  // covers the bake's 1024-face cap
    static constexpr float kDefaultPosScale = 32.0f;  // fixed-point precision

    float kPosScale = kDefaultPosScale;  // fixed-point precision (near or LOD)
    float kInvScale = 1.0f / kDefaultPosScale;

    struct Batch {
        uint32_t first_vertex;   // index of first vertex in the full array
        uint32_t vertex_count;
        uint32_t tri_count;      // (vertex_count - 2) for fan triangulation
        uint16_t material_id;
    };

    T3DVertPacked* verts_ = nullptr;
    uint32_t vert_count_ = 0;     // total logical vertices (half of pairs*2)
    uint32_t pair_count_ = 0;     // number of T3DVertPacked structs

    Batch batches_[kMaxBatches];
    int batch_count_ = 0;
    int discarded_faces_ = 0;

    T3DMat4FP* matrix_fp_ = nullptr;

    Vec3 render_origin_ = {0.0f, 0.0f, 0.0f};
};

}  // namespace madeline_cube
