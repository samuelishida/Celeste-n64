#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/batch_coalesce.hpp"  // FaceSpec, BatchRun, RunFace (Inc 3 / D3)
#include "gameplay/render/dlod_format.hpp"     // DlodMesh (Inc 3 / compressed-LOD)

namespace madeline_cube {

struct RenderCounters;  // defined in open_world_renderer.hpp (Inc 1 / D7)

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

    // Load from a parsed DLOD direction (Inc 3 / compressed-LOD). Positions
    // are already packed at `pos_scale` relative to `render_origin`; faces are
    // contiguous vertex triples pre-grouped by material. Reuses the
    // run-coalescing + RSPQ block capture path. REQUIRES pos_scale ==
    // kLodScale (the no-repack shortcut is only valid at the baked scale) —
    // asserted. Returns false on a null direction / 0 faces (cell skipped,
    // non-fatal, matching today).
    bool LoadFromDlod(const DlodMesh& mesh, int direction,
                      const Vec3& render_origin, float pos_scale);

    // Free all allocated resources.
    void Free();

    // Draw all room geometry. Call within a t3d_frame_start/end pair.
    void Draw() const;

    // Draw the precompiled RSPQ block WITHOUT touching the matrix stack
    // (Inc 3 / D2). Used by the distant pass, which pushes ONE shared
    // camera-relative matrix for the whole pass and draws every cell's block
    // under it. Guards like Draw(): block_ null → legacy per-run/per-batch
    // emission WITHOUT a matrix push (the caller's shared matrix is already
    // on the stack — must not add one). Only valid when
    // `uses_external_matrix_` is set (distant-loaded meshes).
    void DrawBlockOnly() const;

    bool IsLoaded() const { return verts_ != nullptr; }

    // Recompute the model-matrix translation so the drawn world is expressed
    // camera-relative: the matrix translates by `render_origin_ - camera_pos`
    // instead of `render_origin_`. Vertices stay packed against their fixed
    // per-cell render origin (no per-frame re-packing). The near-pass view
    // must ALSO be camera-at-origin (see the CRITICAL coupling note in
    // gameplay_scene.cpp) or geometry is double-offset by `-camera`.
    // No-op when `uses_external_matrix_` is set (the caller owns the matrix).
    void SetCameraPosition(const Vec3& camera_pos);

    // Mark this renderer as drawing under an EXTERNAL (pass-shared) matrix
    // (Inc 3 / D2). Set ONLY on distant-loaded meshes; the near pass's
    // `TileStreamer` renderers keep their per-frame matrix rebuilds. When
    // set, `SetCameraPosition` is a no-op and `DrawBlockOnly` is the draw path.
    void SetExternalMatrixOwner() { uses_external_matrix_ = true; }

    // Number of faces discarded because they exceeded the batch cap. Must
    // remain zero for a validated artifact.
    int DiscardedFaces() const { return discarded_faces_; }

    // Attach the per-frame draw counters (Inc 1 / D7). The renderer increments
    // `near_batches` / `vert_loads` / `syncs` in Draw; the orchestrator owns
    // + resets them. May be null (counters disabled).
    void SetCounters(RenderCounters* counters) { counters_ = counters; }

    // Whether Draw() uses the coalesced-run path (Inc 3 / D3). Mirrors the
    // `run_count_ > 0 && runs_ && run_faces_` gate inside Draw(), so the
    // distant-pass counter split (Inc 2 / instrumentation) picks the exact
    // same path the renderer actually takes. Host-safe.
    bool IsActiveRunPath() const { return run_count_ > 0 && runs_ && run_faces_; }

    // Number of coalesced material runs (Inc 3 / D3). One vert_load + one
    // tri_sync per run when IsActiveRunPath(). May be -1 if coalescing failed.
    int RunCount() const { return run_count_; }

    // Number of per-face batches (fallback path). Used when !IsActiveRunPath().
    int BatchCount() const { return batch_count_; }

    // Total logical vertices loaded from the .lvl (cell-size proxy). Baked
    // vertex count, NOT the per-frame run span loaded into RSP DMEM. Cast is
    // safe: the bake caps faces at kMaxBatches, so vert_count_ fits in int.
    int VertexCount() const { return static_cast<int>(vert_count_); }

    // The render origin this renderer was loaded with (world units). Stored
    // as a plain Vec3 so host tests can assert it without any N64 dependency.
    const Vec3& RenderOrigin() const { return render_origin_; }

private:
    static constexpr int kMaxBatches = 1024;  // covers the bake's 1024-face cap
    static constexpr float kDefaultPosScale = 32.0f;  // fixed-point precision
    static constexpr uint32_t kMaxRunSpan = 70;  // RSP vertex-load cap (D3)

    float kPosScale = kDefaultPosScale;  // fixed-point precision (near or LOD)
    float kInvScale = 1.0f / kDefaultPosScale;

    struct Batch {
        uint32_t first_vertex;   // index of first vertex in the full array
        uint32_t vertex_count;
        uint32_t tri_count;      // (vertex_count - 2) for fan triangulation
        uint16_t material_id;
    };

    // Release the coalesced-run scratch (runs_ / run_faces_). Safe to call on
    // an unloaded renderer (both null). Nulls both pointers.
    void FreeRuns();

    // Shared tail of Load()/LoadFromDlod(): build the coalesced runs + RSPQ
    // block from an already-packed vertex array + a FaceSpec list. The LVL
    // and DLOD paths both funnel through here so they can't drift. `faces`
    // is the per-face spec list (already material-sorted for DLOD, or in
    // original order for LVL — the sort happens inside). Returns true on
    // success (runs or fallback batches built + block captured).
    bool BuildRunsAndBlock(const FaceSpec* faces, int face_count);

    // Release the heap-allocated batch array (Inc 5 / D6). Safe on an
    // unloaded renderer. Nulls `batches_` so the destructor path can't
    // double-free.
    void FreeBatches();

    // Release the precompiled RSPQ block (RSPQ-block-render plan / D5). Safe
    // on an unloaded renderer. Nulls `block_` so a double-free is impossible.
    void FreeBlock();

    // Emit the command sequence for one coalesced material run: prim color +
    // one `t3d_vert_load` (≤ kMaxRunSpan vertices) + each face's OWN fan from
    // its RunFace.offset (never a run-wide fan — crosses face boundaries) +
    // one `t3d_tri_sync`. Counter increments (near_batches / vert_loads /
    // syncs) apply only when `counters` is non-null. Used both to build the
    // precompiled RSPQ block (counters = nullptr) and by the legacy fallback
    // Draw loop (counters = counters_). Emits nothing for empty runs.
    void EmitRunCommands(int r, RenderCounters* counters) const;

    // Emit the command sequence for one per-face batch (fallback path when
    // coalescing failed): prim color + vert_load + its own fan + tri_sync.
    void EmitBatchCommands(int b, RenderCounters* counters) const;

    T3DVertPacked* verts_ = nullptr;
    uint32_t vert_count_ = 0;     // total logical vertices (half of pairs*2)
    uint32_t pair_count_ = 0;     // number of T3DVertPacked structs

    // Heap-allocated batch array (Inc 5 / D6): sized to the face count
    // (clamped to kMaxBatches) instead of an embedded `Batch[1024]` (16 KB
    // each — ~720 KB across 45 distant cells). Load() frees any existing
    // array before reallocating (streaming re-loads / SetCenter leak
    // otherwise); Free() frees + nulls it.
    Batch* batches_ = nullptr;
    int batch_count_ = 0;
    int discarded_faces_ = 0;

    // Coalesced material runs (Inc 3 / D3). Heap-allocated in Load() sized to
    // the face count; freed by FreeRuns()/Free(). When run_count_ > 0, Draw()
    // uses the run path (one RDP state + one vert_load + one t3d_tri_sync per
    // run, each face fanned from its own origin); otherwise it falls back to
    // the per-face batch path.
    BatchRun* runs_ = nullptr;
    RunFace* run_faces_ = nullptr;
    int run_count_ = 0;

    // Precompiled RSPQ block (RSPQ-block-render plan / D1). Captured at the
    // end of Load(): the active path's full command sequence (runs, or the
    // per-face batches when coalescing failed). Draw() plays it back with one
    // `rspq_block_run` after pushing the per-frame camera-relative matrix —
    // the matrix is the ONLY per-frame state, so it stays outside the block.
    // Null when kEnableRspqBlocks is off or Load failed before building
    // (Draw falls back to per-frame emission).
    rspq_block_t* block_ = nullptr;

    // Precomputed per-frame counter sums (RSPQ-block-render plan / D2).
    // Computed at Load with the exact predicates the emitters use, so the
    // block-path Draw adds them to counters_ in O(1) with totals identical to
    // the legacy per-run increments (each cell draws at most once per frame).
    uint32_t counted_batches_ = 0;
    uint32_t counted_vert_loads_ = 0;
    uint32_t counted_syncs_ = 0;

    T3DMat4FP* matrix_fp_ = nullptr;

    Vec3 render_origin_ = {0.0f, 0.0f, 0.0f};
    RenderCounters* counters_ = nullptr;  // per-frame draw counters (Inc 1 / D7)
    // Inc 3 / D2: when true, this renderer draws under an EXTERNAL
    // (pass-shared) matrix — `SetCameraPosition` is a no-op and `DrawBlockOnly`
    // is the draw path. Set only on distant-loaded meshes.
    bool uses_external_matrix_ = false;
};

}  // namespace madeline_cube
