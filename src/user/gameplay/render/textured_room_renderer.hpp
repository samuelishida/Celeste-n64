#pragma once

#include <t3d/t3dmodel.h>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/batch_coalesce.hpp"  // FaceSpec, BatchRun, RunFace (Inc 3 / D3)
#include "gameplay/render/material_catalog.hpp"

#if defined(__mips64) || defined(__mips)
#define MADCUBE_IS_N64 1
#endif

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
    // Synchronous (blocking fopen/fread) — kept as the fallback path
    // (streaming-experience Inc 1): boot center, async-pool exhaustion, or any
    // async DMA issue failure.
    bool Load(const char* lvl_path, const Vec3& render_origin,
              const MaterialCatalog* catalog,
              float pos_scale = kDefaultPosScale);

    // streaming-experience Inc 1: async read + staged parse.
    //
    // `LoadAsync` resolves the .lvl's PI address (`dfs_rom_addr`, dragonfs.h)
    // and issues a NON-BLOCKING `dma_read_async` of `file_size` bytes into
    // `staging` (an uncached buffer the streamer owns — the device DMA engine
    // writes RDRAM directly at the PI speed; the CPU must not read the buffer
    // until `dma_finished` confirms completion). Returns the DMA ticket
    // (always non-zero on success; libdragon `dma.h`), or 0 on any failure
    // (bad path, size larger than the staging buffer, DMA queue unavailable).
    // The caller (TileStreamer) then records the ticket and polls
    // `dma_finished` each frame via the streamer's pending-load pump.
    //
    // `ParseStaged` runs the existing parse (vertex transform + batches +
    // coalesced runs + material groups + RSPQ block precompilation) against
    // the `file_size` bytes already in `staging`. It is the former Load body
    // with the fio swapped for a memory read. Only call it once the DMA for
    // those bytes has completed, and while no other DMA is writing the buffer
    // (the streamer serializes: one in-flight DMA at a time).
    static bool LoadAsync(const char* lvl_path, uint32_t file_size,
                          uint8_t* staging, uint32_t staging_size,
                          uint64_t* out_ticket);
    bool ParseStaged(const uint8_t* staging, uint32_t file_size,
                     const Vec3& render_origin, const MaterialCatalog* catalog,
                     float pos_scale = kDefaultPosScale);

    // Free all allocated resources.
    void Free();

    // memory-opt Inc 4: attach the renderer's pool slot buffer + the pool's
    // shared parse scratch. Called by TileStreamer right after pool Acquire.
    // The persistent per-cell buffers (batches, runs, run_faces, material
    // groups) are carved from `slot`; the transient parse buffers (file buf,
    // faces, lvl_verts, specs, order, sorted_specs) from `scratch`. Both are
    // bump-allocated and reset here (the slot block is reused by the next
    // occupant; the scratch is shared across parses, which are serialized).
    void AttachSlotBuffer(uint8_t* slot, size_t slot_size,
                          uint8_t* scratch, size_t scratch_size);
    // Pool slot index (for TileStreamer's pool Release). -1 = not pooled.
    int PoolSlot() const { return pool_slot_; }
    void SetPoolSlot(int s) { pool_slot_ = s; }

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
    // combiner + primColor) for `material_id`. The global near-pass material
    // grouping calls this ONCE per material per frame (instead of once per
    // (material, cell)), then replays each cell's geometry via
    // `DrawMaterialRun`. A material with no sprite falls back to the flat
    // primColor (never a crash). Call within a t3d_frame_start/end pair.
    void UploadMaterial(uint16_t material_id, RenderCounters* counters) const;

    // Draw all room geometry with per-batch textured combiners. Call within a
    // t3d_frame_start/end pair. A batch whose material has no sprite falls
    // back to the flat primColor (never a crash).
    //
    // `reveal_scale` (streaming-experience Inc 4): the per-frame pivot-scale
    // the streamer animates onto cells that became visible while still
    // loading (a masked pop-in instead of a hard pop). `reveal_scale == 1.0f`
    // is the legacy behavior byte-for-byte (the baked `matrix_fp_` is pushed
    // as today). Below 1.0 the pushed matrix scales basis rows by `s` and
    // scales the translation by `(1-s)` so the pivot stays at the render
    // origin `o` (M'(v) = s·B·v + (1-s)·B·o; the vertex at `v == o` maps to
    // `o` for any `s`, and relative distances scale by `s`). The RSPQ block
    // is UNCHANGED — the matrix is pushed AFTER the block is compiled
    // (outside it), so the reveal adds zero RSP cost. Values below
    // `kRevealEpsilon` clamp to it (degenerate point), above 1.0 clamp to 1.0
    // (reveals only ramp up).
    void Draw(float reveal_scale = 1.0f) const;

    // streaming-memory-opt Inc 4: emit the runs of ONE material group for this
    // cell WITHOUT uploading the sprite (the caller uploads it once per
    // material before replaying the group). Pushes/pops the cell's
    // camera-relative matrix around the run geometry. Call within a
    // t3d_frame_start/end pair. A run whose material has no sprite falls back
    // to the flat primColor (never a crash). `reveal_scale` composes a
    // pivot-scale about the render origin (streaming-experience Inc 4 — see
    // `Draw`); 1.0 is the byte-identical today path.
    void DrawMaterialRun(int first_run, int run_count,
                         RenderCounters* counters, float reveal_scale = 1.0f) const;

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

    // streaming-experience Inc 1: the shared parse body. Reads the LVL2
    // header + faces + vertices from an in-memory image (`data`/`size` — the
    // whole .lvl file), then runs the vertex transform + batch build +
    // coalesced runs + material groups + RSPQ block precompilation. Both the
    // synchronous `Load` (after a blocking whole-file read) and the async
    // `ParseStaged` (after the DMA completes) delegate here, so the parse is
    // written once. Device-only (uses t3d/rdpq).
    bool ParseFromBytes(const uint8_t* data, uint32_t size,
                        const Vec3& render_origin,
                        const MaterialCatalog* catalog, float pos_scale);

    // streaming-experience Inc 4: build the camera-relative model matrix with
    // a reveal scale `s`. `s == 1.0` is the byte-identical today matrix
    // (scale = 1/ps, translation = origin - camera); below 1.0 the basis rows
    // scale by `s` and the translation is unchanged, so in camera space the
    // vertex map is `v' = s·B·v + (o - cam)` — the pivot is the camera (a
    // late-loaded cell grows/contracts toward the camera plane), and the
    // render origin maps to `o + (1 - s)·(o - cam)`. Writes into `out`.
    void BuildRevealMatrix(T3DMat4FP* out, float s) const;

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
    Vec3 camera_pos_ = {0.0f, 0.0f, 0.0f};  // streaming-experience Inc 4: last
                                            // SetCameraPosition (for the reveal
                                            // matrix rebuild)
    const MaterialCatalog* catalog_ = nullptr;
    RenderCounters* counters_ = nullptr;  // per-frame draw counters (Inc 1 / D7)

    // memory-opt Inc 4: pool slot buffer + shared scratch (bump allocators).
    // The persistent per-cell buffers live in `slot_buf_`; the transient
    // parse buffers in `scratch_`. verts_/matrix_fp_ stay malloc_uncached
    // (DMA-direct, uncached — pooling them into cached RAM would need a
    // per-frame cache writeback for the reveal matrix; documented deviation).
    uint8_t* slot_buf_ = nullptr;
    size_t slot_buf_size_ = 0;
    size_t slot_buf_used_ = 0;
    uint8_t* scratch_ = nullptr;
    size_t scratch_size_ = 0;
    size_t scratch_used_ = 0;
    int pool_slot_ = -1;

    // Bump-allocate from the slot buffer (persistent) / scratch (transient).
    // Every allocation is 16-aligned (the pool blocks are 16-aligned; the
    // cursor advances to the next 16-byte boundary) so the struct writes
    // (LvlFace/LvlVertex/Batch/...) never misalign on MIPS. Overflow asserts
    // AND returns nullptr — a cell exceeding its budget fails loudly
    // (callers null-check), never partial-loads.
    void* SlotAlloc(size_t need) {
        if (slot_buf_ == nullptr) {
            assert(false && "renderer slot buffer not attached");
            return nullptr;
        }
        const size_t aligned = (slot_buf_used_ + 15u) & ~size_t(15u);
        if (aligned + need > slot_buf_size_) {
            assert(false && "renderer slot buffer overflow");
            return nullptr;
        }
        void* p = slot_buf_ + aligned;
        slot_buf_used_ = aligned + need;
        return p;
    }
    void* ScratchAlloc(size_t need) {
        if (scratch_ == nullptr) {
            assert(false && "renderer parse scratch not attached");
            return nullptr;
        }
        const size_t aligned = (scratch_used_ + 15u) & ~size_t(15u);
        if (aligned + need > scratch_size_) {
            assert(false && "renderer parse scratch overflow");
            return nullptr;
        }
        void* p = scratch_ + aligned;
        scratch_used_ = aligned + need;
        return p;
    }
};

}  // namespace madeline_cube
