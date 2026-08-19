#include "gameplay/render/lvl_room_renderer.hpp"

#include <cstdio>
#include <cstring>
#include <libdragon.h>
#include <rdpq.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)
#include "gameplay/world/entity_ids.hpp"

namespace madeline_cube {

namespace {

struct LvlVertex { float x, y, z, u, v; };
struct LvlFace   { uint32_t vs, vc; uint16_t mid, flags; float nx, ny, nz; };

// RSPQ block precompilation gate (RSPQ-block-render plan / D1). When true,
// Load() captures the cell's active-path command sequence into a block and
// Draw() plays it back (matrix push + rspq_block_run + pop); when false,
// Draw() re-emits commands per frame as before. Flip to false for an A/B
// comparison on device.
constexpr bool kEnableRspqBlocks = true;

// Compressed distant coordinate scale (Inc 3 / compressed-LOD). Must match
// `DistantWorldRenderer::kLodScale` (distant_world_renderer.hpp) — the DLOD
// no-repack shortcut in `LoadFromDlod` is only valid at this scale.
constexpr float kLodScale = 0.25f;

uint32_t ReadU32(FILE* f) {
    uint8_t b[4]; fread(b, 1, 4, f);
    return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];
}
uint16_t ReadU16(FILE* f) {
    uint8_t b[2]; fread(b, 1, 2, f);
    return uint16_t((uint16_t(b[0])<<8)|b[1]);
}

}  // namespace

bool LvlRoomRenderer::Load(const char* lvl_path, const Vec3& render_origin,
                           float pos_scale) {
    // Free the previous block + arrays before rebuilding (streaming re-loads
    // / SetCenter call Load repeatedly and would leak otherwise).
    FreeBlock();
    render_origin_ = render_origin;
    // Sanity-clamp the fixed-point scale so a bad distant LOD scale cannot
    // produce a degenerate (inverted / overflowing) transform.
    kPosScale = (pos_scale > 0.0f && pos_scale <= 256.0f) ? pos_scale
                                                          : kDefaultPosScale;
    kInvScale = 1.0f / kPosScale;
    FILE* f = fopen(lvl_path, "rb");
    if (!f) { debugf("[lvlroom] open FAILED: %s\n", lvl_path); return false; }

    // Header
    char magic[4]; fread(magic, 1, 4, f);
    if (magic[0]!='L'||magic[1]!='V'||magic[2]!='L'||magic[3]!='2') {
        debugf("[lvlroom] bad magic\n"); fclose(f); return false;
    }
    uint32_t ver = ReadU32(f);
    if (ver != 2) { debugf("[lvlroom] bad version %lu\n", (unsigned long)ver); fclose(f); return false; }

    ReadU32(f);  // collider_count (unused)
    uint32_t face_count   = ReadU32(f);
    uint32_t vertex_count = ReadU32(f);
    uint32_t entity_count = ReadU32(f);
    uint32_t string_count = ReadU32(f);

    // Skip atmosphere fields (16 bytes)
    fseek(f, 16, SEEK_CUR);

    uint32_t off_strings  = ReadU32(f);
    /*off_colliders*/ ReadU32(f);
    uint32_t off_faces    = ReadU32(f);
    uint32_t off_vertices = ReadU32(f);
    /*off_entities*/ ReadU32(f);
    /*off_props*/    ReadU32(f);

    (void)entity_count;
    (void)string_count;
    (void)off_strings;

    if (face_count == 0 || vertex_count == 0) {
        debugf("[lvlroom] empty level\n"); fclose(f); return false;
    }

    // Read faces
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

    // Read vertices
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

    // Build T3DVertPacked pairs, subtracting the render origin so the full
    // map's absolute world coordinates do not overflow the int16 packing.
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
        p.stA[0] = static_cast<int16_t>(va.u * 1024.0f);
        p.stA[1] = static_cast<int16_t>(va.v * 1024.0f);
        p.stB[0] = static_cast<int16_t>(vb.u * 1024.0f);
        p.stB[1] = static_cast<int16_t>(vb.v * 1024.0f);
    }

    // Apply face normals
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        T3DVec3 n = {{lf.nx, lf.ny, lf.nz}};
        uint16_t packed = t3d_vert_pack_normal(&n);
        for (uint32_t vi = lf.vs; vi < lf.vs + lf.vc && vi < vertex_count; ++vi) {
            if (vi & 1) verts_[vi / 2].normB = packed;
            else        verts_[vi / 2].normA = packed;
        }
    }

    // Build batches: one batch per face. The .lvl stores each face as an
    // independent fan of vertices (vs .. vs+vc-1). Merging consecutive
    // same-material faces and fanning across the whole range creates
    // garbage triangles between unrelated faces, so we draw each face
    // as its own fan. Material state changes are cheap enough for now.
    // Faces that exceed the batch cap are counted as discarded (must be 0
    // for a validated artifact) rather than silently truncated.
    //
    // Inc 5 / D6: heap-allocate the batch array sized to the face count
    // (clamped to kMaxBatches) instead of an embedded 16 KB array (~720 KB
    // across 45 distant cells). Free any previous array first (streaming
    // re-loads / SetCenter call Load repeatedly and would leak otherwise).
    // Allocation failure → Load returns false (caller keeps old state).
    FreeBatches();
    {
        const int batch_cap = static_cast<int>(face_count) < kMaxBatches
            ? static_cast<int>(face_count) : kMaxBatches;
        if (batch_cap > 0) {
            batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * batch_cap));
            if (!batches_) {
                free(faces);
                free(lvl_verts);
                debugf("[lvlroom] batch alloc FAILED\n");
                return false;
            }
        }
    }
    batch_count_ = 0;
    discarded_faces_ = 0;
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        if (lf.vc < 3) continue;
        if (batch_count_ >= kMaxBatches) {
            ++discarded_faces_;
            continue;
        }
        batches_[batch_count_++] = {lf.vs, lf.vc, lf.vc - 2, lf.mid};
    }

    // Build the coalesced runs + RSPQ block from the packed verts + batches.
    // (Inc 3 / compressed-LOD: factored into a shared helper so the LVL and
    // DLOD load paths can't drift.)
    {
        FaceSpec* specs = static_cast<FaceSpec*>(malloc(sizeof(FaceSpec) * batch_count_));
        if (specs) {
            for (int s = 0; s < batch_count_; ++s) {
                specs[s] = {batches_[s].first_vertex, batches_[s].vertex_count,
                            batches_[s].tri_count, batches_[s].material_id};
            }
            BuildRunsAndBlock(specs, batch_count_);
            free(specs);
        }
    }

    free(faces);
    free(lvl_verts);

    // Model matrix: compensate for kPosScale AND translate back to world
    // space. The vertices were rebased by `render_origin` before packing, so
    // the matrix must apply scale kInvScale then translation `render_origin`
    // to map the packed int16 (already (world - origin) * kPosScale) back to
    // world coordinates. Without the translation, the chunk is drawn at
    // `world - render_origin` while the player/camera are world-space, which
    // manifests as "only a small piece" + apparent fall-through.
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float r[3] = {0, 0, 0};
    const float p[3] = {render_origin_.x, render_origin_.y, render_origin_.z};
    t3d_mat4_from_srt_euler(&m, s, r, p);
    t3d_mat4_to_fixed(matrix_fp_, &m);

    debugf("[lvlroom] loaded: %lu verts, %d batches, %lu faces, %d discarded"
           " block=%s\n",
           (unsigned long)vertex_count, batch_count_, (unsigned long)face_count,
           discarded_faces_, block_ ? "yes" : "no");
    return true;
}

bool LvlRoomRenderer::BuildRunsAndBlock(const FaceSpec* faces, int face_count) {
    // Coalesce the per-face specs into material runs (Inc 3 / D3). Adjacent
    // same-material faces merge into runs capped at `kMaxRunSpan` loaded
    // vertices; each face keeps its own fan origin (RunFace.offset) so
    // triangulation never crosses a face boundary. Runs are heap-allocated
    // sized to the face count. On any failure (allocation or capacity) we
    // fall back to the per-face batch path (run_count_ stays 0) — never a
    // silent truncation.
    //
    // Inc 1 / D1 (distant-pass perf): the distant pass is Z-off, but the bake
    // (`tools/ogworld/distant_lod.py`) emits faces in arbitrary original
    // polygon order — not material-grouped, not back-to-front — so there is no
    // meaningful intra-cell order to preserve. We STABLE-SORT the faces by
    // material BEFORE coalescing (mirroring `TexturedRoomRenderer::Load`),
    // grouping all faces of one material into contiguous runs → one
    // `t3d_vert_load` + one `t3d_tri_sync` per material per cell instead of per
    // run. Measured from the baked LVLs: 1015 → 303 runs across all 45 cells
    // (~22.6 → ~6.7 runs/cell), a ~3.3× reduction in RSP syncs. Cells are still
    // sorted back-to-front by distance² in `DistantWorldRenderer::Render`. The
    // sort is load-time only (runs in `DistantWorldRenderer::Load` /
    // `TileStreamer::SetCenter`, not per frame). The `batches_` fallback path
    // below stays in original face order (unsorted) — it gets no sort benefit,
    // but it is only used if coalescing fails.
    //
    // The DLOD path (Inc 3 / compressed-LOD) is pre-grouped by material at
    // bake time, so the stable sort is a no-op there (already sorted) — the
    // shared helper keeps the two paths identical.
    FreeRuns();
    if (face_count <= 0) return false;
    {
        const int alloc = face_count;
        runs_ = static_cast<BatchRun*>(malloc(sizeof(BatchRun) * alloc));
        run_faces_ = static_cast<RunFace*>(malloc(sizeof(RunFace) * alloc));
        if (runs_ && run_faces_) {
            // Inc 1 / D1: stable-sort by material, then physically reorder
            // a scratch FaceSpec array using the permutation (CoalesceBatches
            // takes a contiguous array and iterates linearly — it cannot
            // consume indices directly).
            uint16_t* order = static_cast<uint16_t*>(
                malloc(sizeof(uint16_t) * face_count));
            FaceSpec* sorted_specs = static_cast<FaceSpec*>(
                malloc(sizeof(FaceSpec) * face_count));
            if (order && sorted_specs) {
                const int groups = SortFacesByMaterial(
                    faces, face_count, order, face_count);
                if (groups > 0) {
                    for (int s = 0; s < face_count; ++s) {
                        sorted_specs[s] = faces[order[s]];
                    }
                    run_count_ = CoalesceBatches(
                        sorted_specs, face_count, runs_, face_count,
                        run_faces_, face_count, kMaxRunSpan);
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
    }

    // Precompute the per-frame counter sums (RSPQ-block-render plan / D2)
    // using the exact predicates the emitters use, so the block-path Draw
    // adds them in O(1) with totals identical to the legacy per-run counting.
    counted_batches_ = 0;
    counted_vert_loads_ = 0;
    counted_syncs_ = 0;
    if (IsActiveRunPath()) {
        for (int r = 0; r < run_count_; ++r) {
            const BatchRun& run = runs_[r];
            if (run.face_count == 0 || run.vertex_count == 0) continue;
            ++counted_batches_;
            ++counted_vert_loads_;
            ++counted_syncs_;
        }
    } else {
        for (int b = 0; b < batch_count_; ++b) {
            const Batch& batch = batches_[b];
            if (batch.tri_count == 0) continue;
            ++counted_batches_;
            ++counted_vert_loads_;
            ++counted_syncs_;
        }
    }

    // Precompile the active path's full command sequence into one RSPQ block
    // (RSPQ-block-render plan / D1). All run/batch contents are static after
    // Load (the per-frame camera-relative matrix is pushed outside the block
    // at Draw). `rspq_block_begin` asserts on OOM, so the legacy per-frame
    // Draw loops remain as the defensive fallback when block_ is null.
    block_ = nullptr;
    // streaming-memory-opt Inc 3: no-block mode (distant pass) skips the RSPQ
    // block capture entirely — the cell allocates ZERO blocks and draws its
    // runs directly via DrawRunsDirect. The near pass (no_block_ false) keeps
    // the block path for A/B (R3).
    if (!no_block_ && kEnableRspqBlocks && (run_count_ > 0 || batch_count_ > 0)) {
        rspq_block_begin();
        if (IsActiveRunPath()) {
            for (int r = 0; r < run_count_; ++r) EmitRunCommands(r, nullptr);
        } else {
            for (int b = 0; b < batch_count_; ++b) EmitBatchCommands(b, nullptr);
        }
        block_ = rspq_block_end();
    }

    // Inc 5 / compressed-LOD: once the coalesced-run path is active, the
    // per-face batch array is never read again (Draw uses the runs + block).
    // Free it to reclaim ~60 KB across 45 distant cells. Keep it only when
    // coalescing failed (the fallback path needs it). `Draw()`'s run-path gate
    // (`IsActiveRunPath`) already guards the null `batches_`.
    if (IsActiveRunPath()) {
        FreeBatches();
    }
    return true;
}

bool LvlRoomRenderer::LoadFromDlod(const DlodMesh& mesh, int direction,
                                   const Vec3& render_origin, float pos_scale) {
    // The no-repack shortcut is only valid at the baked scale.
    if (pos_scale != kLodScale) return false;
    if (direction < 0 || direction >= mesh.direction_count) return false;
    const DlodDirection& dir = mesh.dirs[direction];
    if (!dir.verts || dir.face_count <= 0 || dir.vert_count <= 0) return false;

    // Free the previous block + arrays before rebuilding (streaming re-loads
    // / SetCenter call Load repeatedly and would leak otherwise).
    FreeBlock();
    render_origin_ = render_origin;
    kPosScale = pos_scale;
    kInvScale = 1.0f / kPosScale;

    // Copy the direction's consecutive per-face triples into T3DVertPacked
    // pairs (with the odd-pair padding the existing path already handles).
    // Each face is 3 verts; the DLOD stores them as contiguous triples, so
    // face i uses verts[3i..3i+2].
    const int face_count = dir.face_count;
    const int vertex_count = dir.vert_count;  // = 3 × face_count
    pair_count_ = (vertex_count + 1) / 2;
    vert_count_ = static_cast<uint32_t>(vertex_count);
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count_));
    if (!verts_) return false;

    for (int pi = 0; pi < static_cast<int>(pair_count_); ++pi) {
        T3DVertPacked& p = verts_[pi];
        const int ia = pi * 2;
        const int ib = pi * 2 + 1;
        const DlodVertex& va = dir.verts[ia];
        const DlodVertex& vb = (ib < vertex_count) ? dir.verts[ib] : dir.verts[ia];
        p.posA[0] = va.x(); p.posA[1] = va.y(); p.posA[2] = va.z();
        p.posB[0] = vb.x(); p.posB[1] = vb.y(); p.posB[2] = vb.z();
        p.normA = 0; p.normB = 0;
        p.rgbaA = 0xFFFFFFFF; p.rgbaB = 0xFFFFFFFF;
        p.stA[0] = 0; p.stA[1] = 0;
        p.stB[0] = 0; p.stB[1] = 0;
    }

    // Build the per-face batch array (one batch per face, each a 3-vert fan).
    FreeBatches();
    {
        const int batch_cap = face_count < kMaxBatches ? face_count : kMaxBatches;
        if (batch_cap > 0) {
            batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * batch_cap));
            if (!batches_) return false;
        }
    }
    batch_count_ = 0;
    discarded_faces_ = 0;
    for (int f = 0; f < face_count; ++f) {
        if (batch_count_ >= kMaxBatches) {
            ++discarded_faces_;
            continue;
        }
        // Face f uses verts[3f..3f+2] (contiguous triples).
        const uint32_t first_vertex = static_cast<uint32_t>(3 * f);
        batches_[batch_count_++] = {first_vertex, 3u, 1u, dir.materials[f]};
    }

    // Build the coalesced runs + RSPQ block (shared with the LVL path).
    {
        FaceSpec* specs = static_cast<FaceSpec*>(malloc(sizeof(FaceSpec) * batch_count_));
        if (specs) {
            for (int s = 0; s < batch_count_; ++s) {
                specs[s] = {batches_[s].first_vertex, batches_[s].vertex_count,
                            batches_[s].tri_count, batches_[s].material_id};
            }
            BuildRunsAndBlock(specs, batch_count_);
            free(specs);
        }
    }

    // Model matrix: compensate for kPosScale AND translate back to world
    // space (same as Load()).
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float r[3] = {0, 0, 0};
    const float p[3] = {render_origin_.x, render_origin_.y, render_origin_.z};
    t3d_mat4_from_srt_euler(&m, s, r, p);
    t3d_mat4_to_fixed(matrix_fp_, &m);

    debugf("[lvlroom] dlod loaded: %d verts, %d faces, block=%s\n",
           vertex_count, face_count, block_ ? "yes" : "no");
    return true;
}

void LvlRoomRenderer::Free() {
    if (verts_) { free_uncached(verts_); verts_ = nullptr; }
    if (matrix_fp_) { free_uncached(matrix_fp_); matrix_fp_ = nullptr; }
    FreeBatches();
    FreeRuns();
    FreeBlock();
    vert_count_ = 0;
    pair_count_ = 0;
    counted_batches_ = 0;
    counted_vert_loads_ = 0;
    counted_syncs_ = 0;
}

void LvlRoomRenderer::FreeBlock() {
    if (block_) {
        // Free only when the RSP is done with it: SetCenter/destructor run in
        // the Update phase, matching the existing vertex-buffer free timing
        // (same caveat as model.cpp) — never mid-frame.
        rspq_block_free(block_);
        block_ = nullptr;
    }
}

void LvlRoomRenderer::FreeBatches() {
    if (batches_) { free(batches_); batches_ = nullptr; }
    batch_count_ = 0;
}

void LvlRoomRenderer::FreeRuns() {
    if (runs_) { free(runs_); runs_ = nullptr; }
    if (run_faces_) { free(run_faces_); run_faces_ = nullptr; }
    run_count_ = 0;
}

void LvlRoomRenderer::SetCameraPosition(const Vec3& camera_pos) {
    if (!matrix_fp_) return;  // not loaded; no-op
    // Inc 3 / D2: when drawing under an external (pass-shared) matrix, the
    // caller owns the matrix — this is a no-op.
    if (uses_external_matrix_) return;
    // The vertices are packed as (world - render_origin) * kPosScale. To draw
    // the world camera-relative, translate the matrix by (render_origin -
    // camera_pos) so that:
    //   drawn = kInvScale * (world - origin) * kPosScale + (origin - camera)
    //         = (world - origin) + (origin - camera)
    //         = world - camera
    // The near-pass view must be camera-at-origin for this to match.
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

// Per-material vertex colors (RGBA8888, same as bake_glb.py MATERIAL_COLORS)
static uint32_t material_color(uint16_t mat_id) {
    // mat_id 0=rock_1, 1=snow_1, 2=rock_2, 3=metal_floor_1, 4=floor_dirty_concrete
    switch (mat_id) {
        case 0: return 0x968773FF;  // rock_1: brown
        case 1: return 0xEBF0FAFF;  // snow_1: white-blue
        case 2: return 0x6E6455FF;  // rock_2: dark brown
        case 3: return 0x788291FF;  // metal_floor_1: grey-blue
        case 4: return 0x5F5A55FF;  // floor_dirty_concrete: dark grey
        default: return 0xFFFFFFFF;
    }
}

void LvlRoomRenderer::Draw() const {
    if (!verts_ || !matrix_fp_) return;

    t3d_matrix_push(matrix_fp_);

    // RSPQ block path (RSPQ-block-render plan / D1): the cell's full command
    // sequence was captured at Load; per frame we only push the
    // camera-relative matrix (outside the block) and play it back. The
    // precomputed counted sums replace the per-run counter increments (D2) —
    // totals are identical because each cell draws at most once per frame.
    if (kEnableRspqBlocks && block_) {
        if (counters_) {
            counters_->near_batches += counted_batches_;
            counters_->vert_loads += counted_vert_loads_;
            counters_->syncs += counted_syncs_;
        }
        rspq_block_run(block_);
    } else if (run_count_ > 0 && runs_ && run_faces_) {
        // Legacy fallback: coalesced material runs (Inc 3 / D3) — one
        // primColor + one vert_load + one tri_sync per run, but EACH FACE
        // FANS FROM ITS OWN ORIGIN (RunFace.offset + base_offset) — a run-wide
        // fan crosses face boundaries and renders garbage (MUST-FIX #1).
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

void LvlRoomRenderer::DrawBlockOnly() const {
    if (!verts_) return;
    // Inc 3 / D2: draw the precompiled block WITHOUT touching the matrix
    // stack — the caller's shared pass matrix is already on the stack. Guards
    // like Draw() (block_ null → legacy per-run/per-batch emission WITHOUT a
    // matrix push; must not add one).
    if (kEnableRspqBlocks && block_) {
        if (counters_) {
            counters_->near_batches += counted_batches_;
            counters_->vert_loads += counted_vert_loads_;
            counters_->syncs += counted_syncs_;
        }
        rspq_block_run(block_);
    } else if (run_count_ > 0 && runs_ && run_faces_) {
        for (int r = 0; r < run_count_; ++r) {
            EmitRunCommands(r, counters_);
        }
    } else if (batches_) {
        for (int b = 0; b < batch_count_; ++b) {
            EmitBatchCommands(b, counters_);
        }
    }
}

void LvlRoomRenderer::DrawRunsDirect() const {
    if (!verts_) return;
    // streaming-memory-opt Inc 3: emit the active path's runs DIRECTLY (no
    // RSPQ block) WITHOUT touching the matrix stack — the caller's shared
    // pass matrix is already on the stack (same contract as DrawBlockOnly).
    // The (run, face) sequence is identical to the block path's: both replay
    // the same coalesced runs (or the same fallback batches), so the distant
    // silhouettes are unchanged. Flat color (rdpq_set_prim_color per run) —
    // no sprite upload. Distant meshes carry no counters_ (the pass counts
    // distant_batches/vert_loads/syncs separately in Render), so passing
    // counters_ here is a no-op that cannot pollute the near counters.
    if (run_count_ > 0 && runs_ && run_faces_) {
        for (int r = 0; r < run_count_; ++r) {
            EmitRunCommands(r, counters_);
        }
    } else if (batches_) {
        for (int b = 0; b < batch_count_; ++b) {
            EmitBatchCommands(b, counters_);
        }
    }
}

void LvlRoomRenderer::EmitRunCommands(int r, RenderCounters* counters) const {
    const BatchRun& run = runs_[r];
    if (run.face_count == 0 || run.vertex_count == 0) return;
    // Inc 1 / D7: count near-pass batches (one per run).
    if (counters) ++counters->near_batches;

    // Set per-material primColor once per run.
    uint32_t color = material_color(run.material_id);
    rdpq_set_prim_color(RGBA32(
        (uint8_t)(color >> 24),
        (uint8_t)(color >> 16),
        (uint8_t)(color >> 8),
        (uint8_t)(color)
    ));

    // Load the run span once (≤ kMaxRunSpan vertices, pair-aligned).
    // CoalesceBatches guarantees (first_vertex & 1) + vertex_count
    // ≤ kMaxRunSpan, so load_count never exceeds the RSP cap.
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

void LvlRoomRenderer::EmitBatchCommands(int b, RenderCounters* counters) const {
    const Batch& batch = batches_[b];
    if (batch.tri_count == 0) return;
    // Inc 1 / D7: count near-pass batches.
    if (counters) ++counters->near_batches;

    // Set per-material primColor
    uint32_t color = material_color(batch.material_id);
    rdpq_set_prim_color(RGBA32(
        (uint8_t)(color >> 24),
        (uint8_t)(color >> 16),
        (uint8_t)(color >> 8),
        (uint8_t)(color)
    ));

    // Load vertex pairs for this batch.
    // The base_vertex accounts for odd/even alignment in the pair-packed format.
    uint32_t base_vertex = batch.first_vertex & 1u;
    uint32_t load_count = ((base_vertex + batch.vertex_count + 1u) / 2u) * 2u;
    if (load_count > 70) load_count = 70;  // RSP DMEM limit per load
    t3d_vert_load(verts_ + batch.first_vertex / 2, 0, load_count);
    if (counters) ++counters->vert_loads;

    // Fan triangulation
    for (uint32_t t = 0; t < batch.tri_count; ++t) {
        t3d_tri_draw(base_vertex, base_vertex + t + 1, base_vertex + t + 2);
    }
    t3d_tri_sync();
    if (counters) ++counters->syncs;
}

}  // namespace madeline_cube
