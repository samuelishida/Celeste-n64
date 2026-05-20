#pragma once

#include <t3d/t3dmodel.h>

#include "gameplay/render/material_catalog.hpp"
#include "gameplay/world/level_loader.hpp"

namespace madeline_cube {

// Renders baked level geometry with per-face material textures.
// Call Init() once after level load; Draw() each frame; Free() on level exit.
class LevelRenderer {
public:
    bool Init(const LevelGeometry& geometry);
    void Free();
    void Draw(const MaterialCatalog& catalog) const;

private:
    struct DrawBatch {
        uint16_t material_id;
        uint32_t vertex_start;
        uint32_t vertex_count;
        uint32_t tri_count;
    };

    static constexpr int kMaxBatches = 1024;
    static constexpr int kMaxVerts = 4096;  // unused — see Init()

    T3DVertPacked* verts_ = nullptr;
    DrawBatch batches_[kMaxBatches];
    int batch_count_ = 0;
    T3DMat4FP* identity_fp_ = nullptr;
};

}  // namespace madeline_cube
