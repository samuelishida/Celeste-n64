#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/batch_coalesce.hpp"  // FaceSpec, BatchRun, RunFace (Inc 3 / D3)
#include "gameplay/render/material_catalog.hpp"

namespace madeline_cube {

struct RenderCounters;  // defined in open_world_renderer.hpp (Inc 1 / D7)

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

    // streaming-memory-opt Inc 4: per-material run accessors for the global
    // near-pass material grouping. The active path's coalesced runs are
    // already grouped by material (the load-time stable sort), so a cell's
    // runs of one material are contiguous. `MaterialGroupCount` returns the
    // number of distinct materials in the active path; `MaterialGroupAt`
    // fills `out` with the first run index and run count for the i-th
    // material group. The orchestrator uses these to build the per-frame
    // (material, cell, first_run, run_count) list and to upload each sprite
    // once per material. Host-safe (no N64 types).
    int MaterialGroupCount() const;
    void MaterialGroupAt(int i, uint16_t* out_material, int* out_first_run,
                         int* out_run_count) const;

    // streaming-memory-opt Inc 4: emit the per-material STATE (sprite upload +
    // combiner + primColor) for `material_id`. The global near-pass material
    // grouping calls this ONCE per material per frame (instead of once per
    // (material, cell)), then replays each cell's geometry via
    // `DrawMaterialRun`. A material with no sprite falls back to the flat
    // primColor (never a crash). Call within a t3d_frame_start/end pair.
    void UploadMaterial(uint16_t material_id, RenderCounters* counters) const;

    // streaming-memory-opt Inc 4: emit the runs of ONE material group for this
    // cell WITHOUT uploading the sprite (the caller uploads it once per
    // material before replaying the group). Pushes/pops the cell's
    // camera-relative matrix around the run geometry. Call within a
    // t3d_frame_start/end pair. A run whose material has no sprite falls back
    // to the flat primColor (never a crash).
    void DrawMaterialRun(int first_run, int run_count,
                         RenderCounters* counters) const;

    bool IsLoaded() const { return verts_ != nullptr; }

    // Recompute the model-matrix translation so the drawn world is expressed
    // camera-relative (same coupling as `LvlRoomRenderer::SetCameraPosition`).
    void SetCameraPosition(const Vec3& camera_pos);

    // Number of faces discarded because they exceeded the batch cap.
    int DiscardedFaces() const { return discarded_faces_; }

    // Attach the per-frame draw counters (Inc 1 / D7). The renderer increments
    // `near_batches` / `texture_uploads` / `vert_loads` / `syncs` in Draw; the
    // orchestrator owns + resets them. May be null (counters disabled).
    void SetCounters(RenderCounters* counters) { counters_ = counters; }

private:
    static constexpr int kMaxBatches = 1024;
    static constexpr float kDefaultPosScale = 32.0f;
    static constexpr uint32_t kMaxRunSpan = 70;  // RSP vertex-load cap (D3)

    float kPosScale = kDefaultPosScale;
    float kInvScale = 1.0f / kDefaultPosScale;

    struct Batch {
        uint32_t first_vertex;
        uint32_t vertex_count;
        uint32_t tri_count;
        uint16_t material_id;
    };

    // Release the coalesced-run scratch (runs_ / run_faces_). Safe on an
    // unloaded renderer. Nulls both pointers.
    void FreeRuns();

    // Release the heap-allocated batch array (Inc 5 / D6). Safe on an
    // unloaded renderer. Nulls `batches_` so the destructor can't double-free.
    void FreeBatches();

    // Release the precompiled RSPQ block (RSPQ-block-render plan / D5). Safe
    // on an unloaded renderer. Nulls `block_` so a double-free is impossible.
    void FreeBlock();

    // Emit the per-material STATE for `material_id`: sprite upload + textured
    // combiner + white primColor (or flat PRIM*SHADE primColor when the
    // material has no sprite). Counter increments apply only when `counters`
    // is non-null. streaming-memory-opt Inc 4: split out of `EmitRunCommands`
    // so the global near-pass material grouping can upload each sprite ONCE
    // per material (via `UploadMaterial`) instead of once per (material, cell).
    void EmitRunState(uint16_t material_id, RenderCounters* counters) const;

    // Emit the GEOMETRY for one coalesced material run: one `t3d_vert_load` +
    // each face's OWN fan + one `t3d_tri_sync`. Counter increments apply only
    // when `counters` is non-null. streaming-memory-opt Inc 4: split out of
    // `EmitRunCommands` so the global near-pass material grouping can replay
    // a cell's runs under a single per-material state (via `DrawMaterialRun`).
    void EmitRunGeometry(int r, RenderCounters* counters) const;

    // Emit the command sequence for one coalesced material run: sprite upload
    // + textured combiner + white primColor (or flat PRIM*SHADE primColor
    // when the material has no sprite) + one `t3d_vert_load` + each face's
    // OWN fan + one `t3d_tri_sync`. Counter increments apply only when
    // `counters` is non-null. Used both to build the precompiled RSPQ block
    // (counters = nullptr) and by the legacy fallback Draw loop.
    void EmitRunCommands(int r, RenderCounters* counters) const;

    // Emit the command sequence for one per-face batch (fallback path).
    void EmitBatchCommands(int b, RenderCounters* counters) const;

    T3DVertPacked* verts_ = nullptr;
    uint32_t vert_count_ = 0;
    uint32_t pair_count_ = 0;

    // Heap-allocated batch array (Inc 5 / D6): sized to the face count
    // (clamped to kMaxBatches) instead of an embedded 16 KB array. Load()
    // frees any existing array before reallocating; Free() frees + nulls it.
    Batch* batches_ = nullptr;
    int batch_count_ = 0;
    int discarded_faces_ = 0;

    // Coalesced material runs (Inc 3 / D3). Same shape + semantics as
    // `LvlRoomRenderer::runs_`; the textured Draw uploads the sprite once per
    // run and fans each face from its own origin.
    BatchRun* runs_ = nullptr;
    RunFace* run_faces_ = nullptr;
    int run_count_ = 0;

    // streaming-memory-opt Inc 4: per-material run groups for the active path.
    // Derived at load from the coalesced runs (which are already grouped by
    // material). Each group is a contiguous run range of one material.
    struct MaterialGroup {
        uint16_t material_id;
        int first_run;
        int run_count;
    };
    MaterialGroup* material_groups_ = nullptr;
    int material_group_count_ = 0;

    // Precompiled RSPQ block (RSPQ-block-render plan / D1). Captured at the
    // end of Load(): the active path's full command sequence (sprite uploads
    // resolved via `catalog_` at Load — same staleness semantics as the old
    // draw-time resolution). Draw() plays it back with one `rspq_block_run`
    // after pushing the per-frame camera-relative matrix (the matrix stays
    // outside the block). Null when kEnableRspqBlocks is off or no geometry
    // (Draw falls back to per-frame emission).
    rspq_block_t* block_ = nullptr;

    // Precomputed per-frame counter sums (RSPQ-block-render plan / D2).
    // Computed at Load with the exact predicates the emitters use; the
    // block-path Draw adds them to counters_ in O(1).
    uint32_t counted_batches_ = 0;
    uint32_t counted_texture_uploads_ = 0;
    uint32_t counted_vert_loads_ = 0;
    uint32_t counted_syncs_ = 0;

    T3DMat4FP* matrix_fp_ = nullptr;
    Vec3 render_origin_ = {0.0f, 0.0f, 0.0f};
    const MaterialCatalog* catalog_ = nullptr;
    RenderCounters* counters_ = nullptr;  // per-frame draw counters (Inc 1 / D7)
};

}  // namespace madeline_cube
