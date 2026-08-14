#include "gameplay/render/textured_room_renderer.hpp"

#include <cstdio>
#include <cstring>
#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_sprite.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)

namespace madeline_cube {

namespace {

struct LvlVertex { float x, y, z, u, v; };
struct LvlFace   { uint32_t vs, vc; uint16_t mid, flags; float nx, ny, nz; };

// RSPQ block precompilation gate (RSPQ-block-render plan / D1). When true,
// Load() captures the cell's active-path command sequence into a block and
// Draw() plays it back; when false, Draw() re-emits commands per frame.
constexpr bool kEnableRspqBlocks = true;

uint32_t ReadU32(FILE* f) {
    uint8_t b[4]; fread(b, 1, 4, f);
    return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];
}
uint16_t ReadU16(FILE* f) {
    uint8_t b[2]; fread(b, 1, 2, f);
    return uint16_t((uint16_t(b[0])<<8)|b[1]);
}

// Per-material flat fallback color (same table as lvl_room_renderer.cpp).
uint32_t material_color(uint16_t mat_id) {
    switch (mat_id) {
        case 0: return 0x968773FF;  // rock_1
        case 1: return 0xEBF0FAFF;  // snow_1
        case 2: return 0x6E6455FF;  // rock_2
        case 3: return 0x788291FF;  // metal_floor_1
        case 4: return 0x5F5A55FF;  // floor_dirty_concrete
        default: return 0xFFFFFFFF;
    }
}

}  // namespace

bool TexturedRoomRenderer::Load(const char* lvl_path, const Vec3& render_origin,
                                const MaterialCatalog* catalog,
                                float pos_scale) {
    // Free the previous block + arrays before rebuilding (streaming re-loads
    // / SetCenter call Load repeatedly and would leak otherwise).
    FreeBlock();
    render_origin_ = render_origin;
    catalog_ = catalog;
    kPosScale = (pos_scale > 0.0f && pos_scale <= 256.0f) ? pos_scale
                                                          : kDefaultPosScale;
    kInvScale = 1.0f / kPosScale;

    FILE* f = fopen(lvl_path, "rb");
    if (!f) { debugf("[texroom] open FAILED: %s\n", lvl_path); return false; }

    char magic[4]; fread(magic, 1, 4, f);
    if (magic[0]!='L'||magic[1]!='V'||magic[2]!='L'||magic[3]!='2') {
        debugf("[texroom] bad magic\n"); fclose(f); return false;
    }
    uint32_t ver = ReadU32(f);
    if (ver != 2) { debugf("[texroom] bad version\n"); fclose(f); return false; }

    ReadU32(f);  // collider_count
    uint32_t face_count   = ReadU32(f);
    uint32_t vertex_count = ReadU32(f);
    ReadU32(f);  // entity_count
    ReadU32(f);  // string_count

    fseek(f, 16, SEEK_CUR);  // atmosphere
    ReadU32(f);  // off_strings
    ReadU32(f);  // off_colliders
    uint32_t off_faces    = ReadU32(f);
    uint32_t off_vertices = ReadU32(f);
    ReadU32(f);  // off_entities
    ReadU32(f);  // off_props

    if (face_count == 0 || vertex_count == 0) {
        debugf("[texroom] empty level\n"); fclose(f); return false;
    }

    LvlFace* faces = static_cast<LvlFace*>(malloc(sizeof(LvlFace) * face_count));
    fseek(f, (long)off_faces, SEEK_SET);
    for (uint32_t i = 0; i < face_count; ++i) {
        LvlFace& lf = faces[i];
        lf.vs    = ReadU32(f);
        lf.vc    = ReadU32(f);
        lf.mid   = ReadU16(f);
        lf.flags = ReadU16(f);
        fread(&lf.nx, 4, 1, f);
        fread(&lf.ny, 4, 1, f);
        fread(&lf.nz, 4, 1, f);
    }

    LvlVertex* lvl_verts = static_cast<LvlVertex*>(malloc(sizeof(LvlVertex) * vertex_count));
    fseek(f, (long)off_vertices, SEEK_SET);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        LvlVertex& lv = lvl_verts[i];
        fread(&lv.x, 4, 1, f);
        fread(&lv.y, 4, 1, f);
        fread(&lv.z, 4, 1, f);
        fread(&lv.u, 4, 1, f);
        fread(&lv.v, 4, 1, f);
    }
    fclose(f);

    pair_count_ = (vertex_count + 1) / 2;
    vert_count_ = vertex_count;
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count_));
    if (!verts_) { free(faces); free(lvl_verts); return false; }

    for (uint32_t pi = 0; pi < pair_count_; ++pi) {
        T3DVertPacked& p = verts_[pi];
        uint32_t ia = pi * 2;
        uint32_t ib = pi * 2 + 1;
        const LvlVertex& va = lvl_verts[ia];
        const LvlVertex& vb = (ib < vertex_count) ? lvl_verts[ib] : lvl_verts[ia];

        auto toFp = [this](float v) -> int16_t { return static_cast<int16_t>(v * kPosScale); };

        p.posA[0] = toFp(va.x - render_origin.x);
        p.posA[1] = toFp(va.y - render_origin.y);
        p.posA[2] = toFp(va.z - render_origin.z);
        p.posB[0] = toFp(vb.x - render_origin.x);
        p.posB[1] = toFp(vb.y - render_origin.y);
        p.posB[2] = toFp(vb.z - render_origin.z);
        p.normA = 0; p.normB = 0;
        p.rgbaA = 0xFFFFFFFF; p.rgbaB = 0xFFFFFFFF;
        // UVs are in texture-repeat units; convert to pixel coords in 10.5
        // fixed point (32px sprite * 32 = 1024). Mirrors lvl_room_renderer.
        p.stA[0] = static_cast<int16_t>(va.u * 1024.0f);
        p.stA[1] = static_cast<int16_t>(va.v * 1024.0f);
        p.stB[0] = static_cast<int16_t>(vb.u * 1024.0f);
        p.stB[1] = static_cast<int16_t>(vb.v * 1024.0f);
    }

    // Apply face normals.
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        T3DVec3 n = {{lf.nx, lf.ny, lf.nz}};
        uint16_t packed = t3d_vert_pack_normal(&n);
        for (uint32_t vi = lf.vs; vi < lf.vs + lf.vc && vi < vertex_count; ++vi) {
            if (vi & 1) verts_[vi / 2].normB = packed;
            else        verts_[vi / 2].normA = packed;
        }
    }

    // Build batches: one batch per face (same as lvl_room_renderer).
    // Inc 5 / D6: heap-allocate the batch array sized to the face count
    // (clamped to kMaxBatches). Free any previous array first (streaming
    // re-loads leak otherwise). Allocation failure → Load returns false.
    FreeBatches();
    {
        const int batch_cap = static_cast<int>(face_count) < kMaxBatches
            ? static_cast<int>(face_count) : kMaxBatches;
        if (batch_cap > 0) {
            batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * batch_cap));
            if (!batches_) {
                free(faces);
                free(lvl_verts);
                debugf("[texroom] batch alloc FAILED\n");
                return false;
            }
        }
    }
    batch_count_ = 0;
    discarded_faces_ = 0;
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        if (lf.vc < 3) continue;
        if (batch_count_ >= kMaxBatches) { ++discarded_faces_; continue; }
        batches_[batch_count_++] = {lf.vs, lf.vc, lf.vc - 2, lf.mid};
    }

    // Build the coalesced material runs (Inc 3 / D3) — same as the flat
    // renderer: adjacent same-material faces merge into runs capped at
    // `kMaxRunSpan` loaded vertices, each face keeping its own fan origin. On
    // failure we fall back to the per-face batch path (run_count_ stays 0).
    // Inc 3 / D2: the near pass is Z-on (order-safe), so we STABLE-SORT the
    // faces by material BEFORE coalescing. This groups all faces of one
    // material into contiguous runs → one sprite upload + one vert_load + one
    // tri_sync per material per cell instead of per run (e.g. cell_n01_n02:
    // 199 runs → 23 material groups). The sort is load-time only (runs in
    // TileStreamer::SetCenter, not per frame). The `batches_` fallback path
    // below stays in original face order (unsorted) — it gets no sort benefit,
    // but it is only used if coalescing fails.
    FreeRuns();
    {
        FaceSpec* specs = static_cast<FaceSpec*>(malloc(sizeof(FaceSpec) * batch_count_));
        if (specs) {
            for (int s = 0; s < batch_count_; ++s) {
                specs[s] = {batches_[s].first_vertex, batches_[s].vertex_count,
                            batches_[s].tri_count, batches_[s].material_id};
            }
            const int alloc = batch_count_ > 0 ? batch_count_ : 1;
            runs_ = static_cast<BatchRun*>(malloc(sizeof(BatchRun) * alloc));
            run_faces_ = static_cast<RunFace*>(malloc(sizeof(RunFace) * alloc));
            if (runs_ && run_faces_ && batch_count_ > 0) {
                // Inc 3 / D2: stable-sort by material, then physically reorder
                // a scratch FaceSpec array using the permutation (CoalesceBatches
                // takes a contiguous array and iterates linearly — it cannot
                // consume indices directly).
                uint16_t* order = static_cast<uint16_t*>(
                    malloc(sizeof(uint16_t) * batch_count_));
                FaceSpec* sorted_specs = static_cast<FaceSpec*>(
                    malloc(sizeof(FaceSpec) * batch_count_));
                if (order && sorted_specs) {
                    const int groups = SortFacesByMaterial(
                        specs, batch_count_, order, batch_count_);
                    if (groups > 0) {
                        for (int s = 0; s < batch_count_; ++s) {
                            sorted_specs[s] = specs[order[s]];
                        }
                        run_count_ = CoalesceBatches(
                            sorted_specs, batch_count_, runs_, batch_count_,
                            run_faces_, batch_count_, kMaxRunSpan);
                    } else {
                        run_count_ = -1;  // sort failed — fall back
                    }
                } else {
                    run_count_ = -1;  // scratch alloc failed — fall back
                }
                free(order);
                free(sorted_specs);
                if (run_count_ <= 0) {  // coalescing failed — fall back
                    FreeRuns();
                }
            } else {
                FreeRuns();
            }
            free(specs);
        }
    }

    free(faces);
    free(lvl_verts);

    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float r[3] = {0, 0, 0};
    const float p[3] = {render_origin_.x, render_origin_.y, render_origin_.z};
    t3d_mat4_from_srt_euler(&m, s, r, p);
    t3d_mat4_to_fixed(matrix_fp_, &m);

    // Precompute the per-frame counter sums (RSPQ-block-render plan / D2)
    // using the exact predicates the emitters use (sprite presence resolves
    // through the catalog exactly like the emit path).
    counted_batches_ = 0;
    counted_texture_uploads_ = 0;
    counted_vert_loads_ = 0;
    counted_syncs_ = 0;
    if (run_count_ > 0 && runs_ && run_faces_) {
        for (int r = 0; r < run_count_; ++r) {
            const BatchRun& run = runs_[r];
            if (run.face_count == 0 || run.vertex_count == 0) continue;
            ++counted_batches_;
            if (catalog_ && catalog_->MaterialFor(run.material_id)) {
                ++counted_texture_uploads_;
            }
            ++counted_vert_loads_;
            ++counted_syncs_;
        }
    } else {
        for (int b = 0; b < batch_count_; ++b) {
            const Batch& batch = batches_[b];
            if (batch.tri_count == 0) continue;
            ++counted_batches_;
            if (catalog_ && catalog_->MaterialFor(batch.material_id)) {
                ++counted_texture_uploads_;
            }
            ++counted_vert_loads_;
            ++counted_syncs_;
        }
    }

    // Precompile the active path's full command sequence into one RSPQ block
    // (RSPQ-block-render plan / D1). All run/batch contents are static after
    // Load (sprites resolve via `catalog_` now; the camera-relative matrix is
    // pushed outside the block at Draw). `rspq_block_begin` asserts on OOM, so
    // the legacy per-frame Draw loops remain as the defensive fallback.
    block_ = nullptr;
    if (kEnableRspqBlocks && (run_count_ > 0 || batch_count_ > 0)) {
        rspq_block_begin();
        if (run_count_ > 0 && runs_ && run_faces_) {
            for (int r = 0; r < run_count_; ++r) EmitRunCommands(r, nullptr);
        } else {
            for (int b = 0; b < batch_count_; ++b) EmitBatchCommands(b, nullptr);
        }
        block_ = rspq_block_end();
    }

    debugf("[texroom] loaded: %lu verts, %d batches, %lu faces, %d discarded"
           " block=%s\n",
           (unsigned long)vertex_count, batch_count_, (unsigned long)face_count,
           discarded_faces_, block_ ? "yes" : "no");
    return true;
}

void TexturedRoomRenderer::Free() {
    if (verts_) { free_uncached(verts_); verts_ = nullptr; }
    if (matrix_fp_) { free_uncached(matrix_fp_); matrix_fp_ = nullptr; }
    FreeBatches();
    FreeRuns();
    FreeBlock();
    vert_count_ = 0;
    pair_count_ = 0;
    counted_batches_ = 0;
    counted_texture_uploads_ = 0;
    counted_vert_loads_ = 0;
    counted_syncs_ = 0;
}

void TexturedRoomRenderer::FreeBlock() {
    if (block_) {
        // Free only when the RSP is done with it: SetCenter/destructor run in
        // the Update phase, matching the existing vertex-buffer free timing
        // (same caveat as model.cpp) — never mid-frame.
        rspq_block_free(block_);
        block_ = nullptr;
    }
}

void TexturedRoomRenderer::FreeBatches() {
    if (batches_) { free(batches_); batches_ = nullptr; }
    batch_count_ = 0;
}

void TexturedRoomRenderer::FreeRuns() {
    if (runs_) { free(runs_); runs_ = nullptr; }
    if (run_faces_) { free(run_faces_); run_faces_ = nullptr; }
    run_count_ = 0;
}

void TexturedRoomRenderer::SetCameraPosition(const Vec3& camera_pos) {
    if (!matrix_fp_) return;
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float r[3] = {0, 0, 0};
    const float p[3] = {
        render_origin_.x - camera_pos.x,
        render_origin_.y - camera_pos.y,
        render_origin_.z - camera_pos.z
    };
    t3d_mat4_from_srt_euler(&m, s, r, p);
    t3d_mat4_to_fixed(matrix_fp_, &m);
}

void TexturedRoomRenderer::Draw() const {
    if (!verts_ || !matrix_fp_) return;

    t3d_matrix_push(matrix_fp_);

    // RSPQ block path (RSPQ-block-render plan / D1): the cell's full command
    // sequence (sprite uploads + combiners + fans) was captured at Load; per
    // frame we only push the camera-relative matrix and play it back. The
    // precomputed counted sums replace the per-run counter increments (D2).
    if (kEnableRspqBlocks && block_) {
        if (counters_) {
            counters_->near_batches += counted_batches_;
            counters_->texture_uploads += counted_texture_uploads_;
            counters_->vert_loads += counted_vert_loads_;
            counters_->syncs += counted_syncs_;
        }
        rspq_block_run(block_);
    } else if (run_count_ > 0 && runs_ && run_faces_) {
        // Legacy fallback: coalesced material runs (Inc 3 / D3) — sprite
        // uploaded + combiner set ONCE per run (the TMEM-upload collapse),
        // EACH FACE FANS FROM ITS OWN ORIGIN (MUST-FIX #1).
        for (int r = 0; r < run_count_; ++r) {
            EmitRunCommands(r, counters_);
        }
    } else if (batches_) {
        // Legacy fallback: per-face batches (coalescing failed or unbuilt).
        for (int b = 0; b < batch_count_; ++b) {
            EmitBatchCommands(b, counters_);
        }
    }

    t3d_matrix_pop(1);
}

void TexturedRoomRenderer::EmitRunCommands(int r, RenderCounters* counters) const {
    const BatchRun& run = runs_[r];
    if (run.face_count == 0 || run.vertex_count == 0) return;
    // Inc 1 / D7: count near-pass batches (one per run).
    if (counters) ++counters->near_batches;

    // Resolve the run's material sprite once. If present, upload it as a
    // tile; else fall back to flat primColor for the whole run.
    sprite_t* sprite = catalog_ ? catalog_->MaterialFor(run.material_id)
                                : nullptr;
    if (sprite) {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        // RDPQ_COMBINER_TEX_FLAT = (TEX0,0,PRIM,0) = TEX0 * PRIM, so
        // primColor MUST be white here or the texture is tinted by whatever
        // primColor the distant pass (or a flat run) last left.
        rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        rdpq_sprite_upload(TILE0, sprite, NULL);
        if (counters) ++counters->texture_uploads;  // Inc 1 / D7
    } else {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        uint32_t color = material_color(run.material_id);
        rdpq_set_prim_color(RGBA32(
            (uint8_t)(color >> 24),
            (uint8_t)(color >> 16),
            (uint8_t)(color >> 8),
            (uint8_t)(color)
        ));
    }

    // Load the run span once (≤ kMaxRunSpan vertices, pair-aligned).
    const uint32_t base_offset = run.first_vertex & 1u;
    uint32_t load_count = ((base_offset + run.vertex_count + 1u) / 2u) * 2u;
    if (load_count > 70) load_count = 70;  // safety net
    t3d_vert_load(verts_ + run.first_vertex / 2, 0, load_count);
    if (counters) ++counters->vert_loads;  // Inc 1 / D7

    // Each face fans from its own origin (offset + base_offset).
    for (uint32_t f = 0; f < run.face_count; ++f) {
        const RunFace& rf = run_faces_[run.first_face + f];
        const uint32_t base = rf.offset + base_offset;
        for (uint32_t t = 0; t < rf.tri_count; ++t) {
            t3d_tri_draw(base, base + t + 1, base + t + 2);
        }
    }
    t3d_tri_sync();
    if (counters) ++counters->syncs;  // Inc 1 / D7
}

void TexturedRoomRenderer::EmitBatchCommands(int b, RenderCounters* counters) const {
    const Batch& batch = batches_[b];
    if (batch.tri_count == 0) return;
    // Inc 1 / D7: count near-pass batches.
    if (counters) ++counters->near_batches;

    // Resolve the material's sprite. If present, use a textured combiner
    // and upload the sprite as a tile; else fall back to flat primColor.
    sprite_t* sprite = catalog_ ? catalog_->MaterialFor(batch.material_id)
                                : nullptr;
    if (sprite) {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        // Same primColor-to-white requirement as the run path (see above).
        rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        rdpq_sprite_upload(TILE0, sprite, NULL);
        if (counters) ++counters->texture_uploads;  // Inc 1 / D7
    } else {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        uint32_t color = material_color(batch.material_id);
        rdpq_set_prim_color(RGBA32(
            (uint8_t)(color >> 24),
            (uint8_t)(color >> 16),
            (uint8_t)(color >> 8),
            (uint8_t)(color)
        ));
    }

    uint32_t base_vertex = batch.first_vertex & 1u;
    uint32_t load_count = ((base_vertex + batch.vertex_count + 1u) / 2u) * 2u;
    if (load_count > 70) load_count = 70;
    t3d_vert_load(verts_ + batch.first_vertex / 2, 0, load_count);
    if (counters) ++counters->vert_loads;  // Inc 1 / D7

    for (uint32_t t = 0; t < batch.tri_count; ++t) {
        t3d_tri_draw(base_vertex, base_vertex + t + 1, base_vertex + t + 2);
    }
    t3d_tri_sync();
    if (counters) ++counters->syncs;  // Inc 1 / D7
}

}  // namespace madeline_cube
