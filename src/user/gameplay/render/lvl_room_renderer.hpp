#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

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
    bool Load(const char* lvl_path);

    // Free all allocated resources.
    void Free();

    // Draw all room geometry. Call within a t3d_frame_start/end pair.
    void Draw() const;

    bool IsLoaded() const { return verts_ != nullptr; }

private:
    static constexpr int kMaxBatches = 512;
    static constexpr float kPosScale = 32.0f;   // fixed-point precision
    static constexpr float kInvScale = 1.0f / kPosScale;

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

    T3DMat4FP* matrix_fp_ = nullptr;
};

}  // namespace madeline_cube
