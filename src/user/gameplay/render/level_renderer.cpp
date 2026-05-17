#include "gameplay/render/level_renderer.hpp"

#include <algorithm>
#include <cstring>
#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_sprite.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

namespace madeline_cube {

namespace {

// Pack a position (game-units) into int16 by scaling to fixed-point.
// T3D uses a coordinate space where vertices are in roughly [-32768, 32767].
// Game units are typically in [-20, 20] range; scale by 512 for good precision.
static constexpr float kPosFp = 512.0f;

// UV scale: baker outputs UVs already in texture-repeat units (0-1 = one tile).
// No additional scaling needed.
static constexpr float kUvScale = 1.0f;

static int16_t ToFp(float v) {
    return static_cast<int16_t>(v * kPosFp);
}

}  // namespace

bool LevelRenderer::Init(const LevelGeometry& geometry) {
    if (geometry.vertex_count == 0 || geometry.face_count == 0) return false;

    // Allocate uncached vertex buffer for RSP
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * (geometry.vertex_count + 1) / 2 * 2));
    if (!verts_) return false;

    // Build T3DVertPacked from LevelVertex (pair-packed format)
    const int pair_count = (geometry.vertex_count + 1) / 2;
    for (int pi = 0; pi < pair_count; ++pi) {
        T3DVertPacked& pair = verts_[pi];
        const int ia = pi * 2;
        const int ib = pi * 2 + 1;
        const LevelVertex& va = geometry.vertices[ia];
        const LevelVertex& vb = (ib < geometry.vertex_count)
                                ? geometry.vertices[ib]
                                : geometry.vertices[ia];

        pair.posA[0] = ToFp(va.pos.x);
        pair.posA[1] = ToFp(va.pos.y);
        pair.posA[2] = ToFp(va.pos.z);
        pair.normA   = 0;
        pair.posB[0] = ToFp(vb.pos.x);
        pair.posB[1] = ToFp(vb.pos.y);
        pair.posB[2] = ToFp(vb.pos.z);
        pair.normB   = 0;
        // UV: int16 ST in T3DVertPacked
        // Baker outputs UVs in texture-repeat units; scale by 1024 for T3D's int16 UV space
        pair.stA[0] = static_cast<int16_t>(va.u * 1024.0f);
        pair.stA[1] = static_cast<int16_t>(va.v * 1024.0f);
        pair.stB[0] = static_cast<int16_t>(vb.u * 1024.0f);
        pair.stB[1] = static_cast<int16_t>(vb.v * 1024.0f);
        pair.rgbaA  = 0xFFFFFFFF;
        pair.rgbaB  = 0xFFFFFFFF;
    }

    // Apply flat face normals after the shared vertex packing pass.
    for (int fi = 0; fi < geometry.face_count; ++fi) {
        const LevelFace& lf = geometry.faces[fi];
        T3DVec3 normal = {{lf.normal.x, lf.normal.y, lf.normal.z}};
        const uint16_t packed = t3d_vert_pack_normal(&normal);
        for (uint32_t vi = lf.vertex_start;
             vi < lf.vertex_start + lf.vertex_count &&
             vi < static_cast<uint32_t>(geometry.vertex_count);
             ++vi) {
            if (vi & 1) {
                verts_[vi / 2].normB = packed;
            } else {
                verts_[vi / 2].normA = packed;
            }
        }
    }

    // Build draw batches from faces (one batch per face; faces are already
    // grouped by material in the baked output)
    batch_count_ = 0;
    for (int i = 0; i < geometry.face_count && batch_count_ < kMaxBatches; ++i) {
        const LevelFace& lf = geometry.faces[i];
        DrawBatch& batch = batches_[batch_count_++];
        batch.material_id  = lf.material_id;
        batch.vertex_start = lf.vertex_start;
        batch.vertex_count = lf.vertex_count;
        // Each face is a convex polygon; draw as a fan of triangles
        batch.tri_count    = (lf.vertex_count >= 3) ? (lf.vertex_count - 2) : 0;
    }

    // Identity matrix for the level geometry (already in world space)
    identity_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 identity;
    const float s[3] = {1.0f / kPosFp, 1.0f / kPosFp, 1.0f / kPosFp};
    const float r[3] = {0.0f, 0.0f, 0.0f};
    const float p[3] = {0.0f, 0.0f, 0.0f};
    t3d_mat4_from_srt_euler(&identity, s, r, p);
    t3d_mat4_to_fixed(identity_fp_, &identity);

    debugf("[lvlrender] init: %d verts %d batches\n",
           geometry.vertex_count, batch_count_);
    return true;
}

void LevelRenderer::Free() {
    if (verts_) {
        free_uncached(verts_);
        verts_ = nullptr;
    }
    if (identity_fp_) {
        free_uncached(identity_fp_);
        identity_fp_ = nullptr;
    }
    batch_count_ = 0;
}

void LevelRenderer::Draw(const MaterialCatalog& catalog) const {
    if (!verts_ || !identity_fp_) return;

    t3d_matrix_push(identity_fp_);

    int current_material_id = -1;
    for (int b = 0; b < batch_count_; ++b) {
        const DrawBatch& batch = batches_[b];
        if (batch.tri_count == 0) continue;

        sprite_t* material = catalog.MaterialFor(batch.material_id);
        if (batch.material_id != current_material_id && material != nullptr) {
            rdpq_texparms_t parms = {};
            parms.s.repeats = REPEAT_INFINITE;
            parms.t.repeats = REPEAT_INFINITE;
            rdpq_sprite_upload(TILE0, material, &parms);
            current_material_id = batch.material_id;
        }

        // Packed vertices are stored in pairs. Odd face starts land in the
        // second slot of the first loaded pair, so the local fan must keep
        // that one-vertex offset instead of borrowing the previous face.
        const uint32_t base_vertex = batch.vertex_start & 1u;
        const uint32_t loaded_vertex_count =
            ((base_vertex + batch.vertex_count + 1u) / 2u) * 2u;
        const uint32_t vertex_count = std::min<uint32_t>(70u, loaded_vertex_count);
        t3d_vert_load(verts_ + batch.vertex_start / 2, 0, vertex_count);

        for (uint32_t t = 0; t < batch.tri_count; ++t) {
            t3d_tri_draw(base_vertex, base_vertex + t + 1, base_vertex + t + 2);
        }
        t3d_tri_sync();
    }

    t3d_matrix_pop(1);
}

}  // namespace madeline_cube
