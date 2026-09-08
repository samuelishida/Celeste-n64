#include "gameplay/render/textured_room_renderer.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef __mips__
#include <asset.h>
#endif
#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_sprite.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/debug_flags.hpp"  // kEnableCompressedCells (memory-opt Inc 2)

// streaming-experience Inc 1: async DMA read + DFS→PI address resolution.
// `dragonfs.h` (NOT `dfs.h` — the SDK header is dragonfs.h) exposes
// `dfs_rom_addr`; `dma.h` exposes `dma_read_async`/`dma_finished`.
#include <dma.h>
#include <dragonfs.h>

#include "gameplay/render/open_world_renderer.hpp"  // RenderCounters (Inc 1 / D7)
#include "gameplay/debug_flags.hpp"  // kEnableCellReveal / kRevealEpsilon (Inc 4)

namespace madeline_cube {

namespace {

struct LvlVertex { float x, y, z, u, v; };
struct LvlFace   { uint32_t vs, vc; uint16_t mid, flags; float nx, ny, nz; };

// RSPQ block precompilation gate (RSPQ-block-render plan / D1). When true,
// Load() captures the cell's active-path command sequence into a block and
// Draw() plays it back; when false, Draw() re-emits commands per frame.
constexpr bool kEnableRspqBlocks = true;

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

// The LVL2 UVs are UNWRAPPED world-scale texel coordinates (1 unit = 1 texel)
// that can far exceed the 32-texel sprite — a single cell spans ~6000 texels.
// The RDP must therefore WRAP the tile (sample ST modulo the tile size), not
// clamp. libdragon's default tileparms (sprite has no embedded texparms and we
// pass NULL) leave the tile descriptor all-zero, which rdpq_set_tile maps to
// CLAMP on both axes (`clamp | (mask==0)` = 1) — that would pin out-of-range
// UVs to edge texels instead of tiling. Force infinite repetition (wrap).
//
// The int16 overflow of extreme UVs is harmless once the tile wraps: the tile
// is 32 texels = 1024 in s10.5, and 65536 (the int16 wrap modulus) is a
// multiple of 1024, so (true_ST mod 65536) mod 1024 == true_ST mod 1024.
constexpr rdpq_texparms_t kWrapTexparms = {
    0, 0,
    {0.0f, 0, REPEAT_INFINITE, false},  // s
    {0.0f, 0, REPEAT_INFINITE, false},  // t
};

}  // namespace

// streaming-experience Inc 1: a bounds-checked big-endian reader over an
// in-memory .lvl image. The LVL2 format is big-endian (the baker writes it
// that way; the N64 is big-endian). `ParseFromBytes` uses this instead of the
// old FILE* cursor so the same parse body serves the sync `Load` (whole-file
// read) and the async `ParseStaged` (DMA-staged bytes).
struct LvlMemReader {
    const uint8_t* p;
    uint32_t size;
    uint32_t off = 0;
    bool ok = true;

    uint32_t u32() {
        if (off + 4 > size) { ok = false; return 0; }
        const uint32_t v = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                           (uint32_t(p[off + 2]) << 8) | uint32_t(p[off + 3]);
        off += 4;
        return v;
    }
    uint16_t u16() {
        if (off + 2 > size) { ok = false; return 0; }
        const uint16_t v = uint16_t((uint16_t(p[off]) << 8) | p[off + 1]);
        off += 2;
        return v;
    }
    void f32(float& out) {
        if (off + 4 > size) { ok = false; out = 0.0f; return; }
        std::memcpy(&out, p + off, 4);
        off += 4;
    }
    void skip(uint32_t n) { off += n; if (off > size) ok = false; }
    void seek(uint32_t n) { off = n; if (off > size) ok = false; }
};

void TexturedRoomRenderer::AttachSlotBuffer(uint8_t* slot, size_t slot_size,
                                            uint8_t* scratch,
                                            size_t scratch_size) {
    // memory-opt Inc 4: (re)attach the pool slot buffer + shared scratch.
    // Resets both bump cursors — the previous occupant's buffers are gone
    // (the pool reuses the block; the scratch is shared across parses).
    slot_buf_ = slot;
    slot_buf_size_ = slot_size;
    slot_buf_used_ = 0;
    scratch_ = scratch;
    scratch_size_ = scratch_size;
    scratch_used_ = 0;
}

bool TexturedRoomRenderer::Load(const char* lvl_path, const Vec3& render_origin,
                                const MaterialCatalog* catalog,
                                float pos_scale) {
    // memory-opt Inc 4: reset the shared parse scratch cursor — the previous
    // parse's transient buffers are gone (parses are serialized).
    scratch_used_ = 0;
    // Synchronous fallback (streaming-experience Inc 1): read the whole file
    // into a temp buffer, then run the shared parse. Used at boot (center
    // cell) and whenever the async path is unavailable (pool exhausted, DMA
    // issue failure, queue full).
#ifdef __mips__
    if (kEnableCompressedCells) {
        // memory-opt Inc 2: asset_load (whole-buffer, transparent decompress).
        // asset_fopen is NOT usable here — it asserts on fseek, and this load
        // needs the file size.
        int sz = 0;
        uint8_t* data = static_cast<uint8_t*>(asset_load(lvl_path, &sz));
        if (data) {
            const bool ok = ParseFromBytes(data, static_cast<uint32_t>(sz),
                                           render_origin, catalog, pos_scale);
            free(data);
            return ok;
        }
        debugf("[texroom] asset_load FAILED (falling back to fopen): %s\n", lvl_path);
    }
#endif
    FILE* f = fopen(lvl_path, "rb");
    if (!f) { debugf("[texroom] open FAILED: %s\n", lvl_path); return false; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    const long file_size = ftell(f);
    if (file_size <= 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    // memory-opt Inc 4: the file buffer comes from the shared parse scratch
    // (bump-allocated; the scratch is reset at the next parse). No heap alloc.
    uint8_t* buf = static_cast<uint8_t*>(ScratchAlloc((size_t)file_size));
    const size_t got = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if (got != (size_t)file_size) return false;
    const bool ok = ParseFromBytes(buf, (uint32_t)file_size, render_origin,
                                   catalog, pos_scale);
    return ok;
}

bool TexturedRoomRenderer::ParseFromBytes(const uint8_t* data, uint32_t size,
                                          const Vec3& render_origin,
                                          const MaterialCatalog* catalog,
                                          float pos_scale) {
    // Free the previous block + arrays before rebuilding (streaming re-loads
    // / SetCenter call Load repeatedly and would leak otherwise).
    FreeBlock();
    // memory-opt Inc 4: reset the slot-buffer bump cursor — the previous
    // occupant's persistent buffers are rebuilt from the start. The scratch
    // cursor is NOT reset here: Load may have bump-allocated the file buffer
    // into it before calling us (it must stay live while we parse).
    slot_buf_used_ = 0;
    render_origin_ = render_origin;
    catalog_ = catalog;
    kPosScale = (pos_scale > 0.0f && pos_scale <= 256.0f) ? pos_scale
                                                          : kDefaultPosScale;
    kInvScale = 1.0f / kPosScale;

    LvlMemReader rd{data, size};
    char magic[4];
    if (size < 4) return false;
    std::memcpy(magic, data, 4);
    if (magic[0]!='L'||magic[1]!='V'||magic[2]!='L'||magic[3]!='2') {
        debugf("[texroom] bad magic\n"); return false;
    }
    rd.skip(4);  // the magic was read directly; advance past it (the old FILE
                 // cursor advanced via fread, so the reader must too)
    const uint32_t ver = rd.u32();
    if (ver != 2) { debugf("[texroom] bad version\n"); return false; }

    rd.u32();  // collider_count
    const uint32_t face_count   = rd.u32();
    const uint32_t vertex_count = rd.u32();
    rd.u32();  // entity_count
    rd.u32();  // string_count

    rd.skip(16);  // atmosphere
    rd.u32();  // off_strings
    rd.u32();  // off_colliders
    const uint32_t off_faces    = rd.u32();
    const uint32_t off_vertices = rd.u32();
    rd.u32();  // off_entities
    rd.u32();  // off_props

    if (!rd.ok || face_count == 0 || vertex_count == 0) {
        debugf("[texroom] empty level\n"); return false;
    }

    LvlFace* faces = static_cast<LvlFace*>(ScratchAlloc(sizeof(LvlFace) * face_count));
    if (!faces) return false;
    rd.seek(off_faces);
    for (uint32_t i = 0; i < face_count; ++i) {
        LvlFace& lf = faces[i];
        lf.vs    = rd.u32();
        lf.vc    = rd.u32();
        lf.mid   = rd.u16();
        lf.flags = rd.u16();
        rd.f32(lf.nx);
        rd.f32(lf.ny);
        rd.f32(lf.nz);
    }

    LvlVertex* lvl_verts = static_cast<LvlVertex*>(ScratchAlloc(sizeof(LvlVertex) * vertex_count));
    if (!lvl_verts) return false;
    rd.seek(off_vertices);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        LvlVertex& lv = lvl_verts[i];
        rd.f32(lv.x);
        rd.f32(lv.y);
        rd.f32(lv.z);
        rd.f32(lv.u);
        rd.f32(lv.v);
    }
    if (!rd.ok) return false;

    pair_count_ = (vertex_count + 1) / 2;
    vert_count_ = vertex_count;
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count_));
    if (!verts_) return false;
    // memory-opt Inc 3: DMA-direct (RSP vertex DMA) — `malloc_uncached` is
    // 16-byte aligned by contract; assert so a future allocator change
    // cannot regress it.
    assert((reinterpret_cast<uintptr_t>(verts_) & 0xF) == 0);

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
        // UVs are in UNWRAPPED TEXEL units (1 unit = 1 texel, world-scale from
        // the bake pipeline's compute_uv). Pack as s10.5 fixed point where
        // 1 texel = 32 units (t3d.h: "UV fixed point 10.5 (pixel coords)").
        // The tile wraps (see kWrapTexparms), so values beyond the 32-texel
        // sprite are sampled modulo the tile size; the int16 overflow of the
        // extreme unwrapped values is harmless (65536 is a multiple of the
        // 1024-unit tile size in s10.5).
        p.stA[0] = static_cast<int16_t>(va.u * 32.0f);
        p.stA[1] = static_cast<int16_t>(va.v * 32.0f);
        p.stB[0] = static_cast<int16_t>(vb.u * 32.0f);
        p.stB[1] = static_cast<int16_t>(vb.v * 32.0f);
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
            batches_ = static_cast<Batch*>(SlotAlloc(sizeof(Batch) * batch_cap));
            if (!batches_) {
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
        FaceSpec* specs = static_cast<FaceSpec*>(ScratchAlloc(sizeof(FaceSpec) * batch_count_));
        if (specs) {
            for (int s = 0; s < batch_count_; ++s) {
                specs[s] = {batches_[s].first_vertex, batches_[s].vertex_count,
                            batches_[s].tri_count, batches_[s].material_id};
            }
            const int alloc = batch_count_ > 0 ? batch_count_ : 1;
            runs_ = static_cast<BatchRun*>(SlotAlloc(sizeof(BatchRun) * alloc));
            run_faces_ = static_cast<RunFace*>(SlotAlloc(sizeof(RunFace) * alloc));
            if (runs_ && run_faces_ && batch_count_ > 0) {
                // Inc 3 / D2: stable-sort by material, then physically reorder
                // a scratch FaceSpec array using the permutation (CoalesceBatches
                // takes a contiguous array and iterates linearly — it cannot
                // consume indices directly).
                uint16_t* order = static_cast<uint16_t*>(
                    ScratchAlloc(sizeof(uint16_t) * batch_count_));
                FaceSpec* sorted_specs = static_cast<FaceSpec*>(
                    ScratchAlloc(sizeof(FaceSpec) * batch_count_));
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
                if (run_count_ <= 0) {  // coalescing failed — fall back
                    FreeRuns();
                }
            } else {
                FreeRuns();
            }
        }
    }

    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    // memory-opt Inc 3: DMA-direct (RSP matrix DMA) — 16-byte aligned by
    // contract; assert.
    assert((reinterpret_cast<uintptr_t>(matrix_fp_) & 0xF) == 0);
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float rot[3] = {0, 0, 0};
    const float p[3] = {render_origin_.x, render_origin_.y, render_origin_.z};
    t3d_mat4_from_srt_euler(&m, s, rot, p);
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

    // streaming-memory-opt Inc 4: derive per-material run groups for the
    // active path. The coalesced runs are already grouped by material (the
    // load-time stable sort), so each group is a contiguous run range of one
    // material. Used by the global near-pass material grouping to upload each
    // sprite once per material instead of once per (material, cell).
    if (material_groups_) { free(material_groups_); material_groups_ = nullptr; }
    material_group_count_ = 0;
    if (run_count_ > 0 && runs_ && run_faces_) {
        material_groups_ = (MaterialGroup*)SlotAlloc(sizeof(MaterialGroup) * (size_t)run_count_);
        if (material_groups_) {
            int g = 0;
            int r = 0;
            while (r < run_count_) {
                const uint16_t mat = runs_[r].material_id;
                const int first = r;
                while (r < run_count_ && runs_[r].material_id == mat) ++r;
                material_groups_[g].material_id = mat;
                material_groups_[g].first_run = first;
                material_groups_[g].run_count = r - first;
                ++g;
            }
            material_group_count_ = g;
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

bool TexturedRoomRenderer::LoadAsync(const char* lvl_path, uint32_t file_size,
                                     uint8_t* staging, uint32_t staging_size,
                                     uint64_t* out_ticket) {
    // Device-only (streaming-experience Inc 1). Resolve the .lvl's PI address
    // and issue a non-blocking DMA read into the caller's staging buffer.
    // The manifest carries the exact `rom:/lvl/<pack>/<chunk>.lvl` path, but
    // `dfs_rom_addr` (dragonfs.h) expects the RAW path (NO `rom:/` prefix —
    // libdragon's own callers strip it, e.g. model64.c passes `anim_name+5`).
    // Skipping the 5-char `rom:/` prefix here is load-bearing: passing the
    // prefixed path returns 0 (file not found) and every async load silently
    // fell back to sync, defeating the async-load goal (Inc 1). The returned
    // PI address is guaranteed 2-byte aligned (even), so the raw DMA path
    // (real-HW ROM is not io_accessible) is safe with an even read length.
    if (!out_ticket) return false;
    *out_ticket = 0;
    if (file_size == 0 || file_size > staging_size) {
        debugf("[texroom] async size %lu > staging %lu\n", (unsigned long)file_size, (unsigned long)staging_size);
        return false;
    }
    const char* rom_path = lvl_path;
    if (strncmp(rom_path, "rom:/", 5) == 0) rom_path += 5;
    const pi_addr_t pi = dfs_rom_addr(rom_path);
    if (pi == 0) {
        debugf("[texroom] async open FAILED: %s\n", lvl_path);
        return false;
    }
    // Round the read length up to even. `| 1` does NOT round up (it is a no-op
    // on the odd .lvl sizes in the current manifest), so the original odd
    // length actually relied on `dma_read_async`'s odd-length handling, which
    // is valid only under the 1-bit-alignment contract — RAM and PI addresses
    // must share their low bit (both even here: `malloc_uncached` staging is
    // 8-aligned, and `dfs_rom_addr` returns an even ROM offset). Even-rounding
    // is a harmless defensive preference: it keeps the transfer on the plain
    // raw-DMA path (multiple-of-2 length) with no CPU-fixup, and the extra
    // byte lands harmlessly in the (oversized) staging buffer and is never
    // parsed (ParseStaged bounds by the exact file_size).
    const uint32_t read_len = (file_size + 1u) & ~1u;
    const uint64_t ticket = dma_read_async(staging, pi, read_len);
    if (ticket == 0) {
        debugf("[texroom] async DMA issue FAILED: %s\n", lvl_path);
        return false;
    }
    *out_ticket = ticket;
    return true;
}

bool TexturedRoomRenderer::ParseStaged(const uint8_t* staging, uint32_t file_size,
                                       const Vec3& render_origin,
                                       const MaterialCatalog* catalog,
                                       float pos_scale) {
    // streaming-experience Inc 1: parse the DMA-staged bytes into the renderer
    // (the former Load body, reading from `staging` instead of a FILE). The
    // caller must have confirmed `dma_finished` for the ticket that filled
    // `staging`, and must not issue another DMA into the same buffer until
    // this returns (the streamer serializes one in-flight read at a time).
    // memory-opt Inc 4: reset the shared parse scratch cursor (no file buf
    // was bump-allocated on this path).
    scratch_used_ = 0;
    return ParseFromBytes(staging, file_size, render_origin, catalog, pos_scale);
}

void TexturedRoomRenderer::Free() {
    if (verts_) { free_uncached(verts_); verts_ = nullptr; }
    if (matrix_fp_) { free_uncached(matrix_fp_); matrix_fp_ = nullptr; }
    // memory-opt Inc 4: batches_/runs_/run_faces_/material_groups_ live in
    // the pool slot buffer — FreeBatches/FreeRuns null them WITHOUT free (the
    // pool reuses the block; the next occupant's AttachSlotBuffer resets the
    // cursor).
    FreeBatches();
    FreeRuns();
    material_groups_ = nullptr;
    FreeBlock();
    material_group_count_ = 0;
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
    // memory-opt Inc 4: pool-slot buffer — null without free.
    batches_ = nullptr;
    batch_count_ = 0;
}

void TexturedRoomRenderer::FreeRuns() {
    // memory-opt Inc 4: pool-slot buffer — null without free.
    runs_ = nullptr;
    run_faces_ = nullptr;
    run_count_ = 0;
}

void TexturedRoomRenderer::SetCameraPosition(const Vec3& camera_pos) {
    if (!matrix_fp_) return;
    camera_pos_ = camera_pos;
    BuildRevealMatrix(matrix_fp_, 1.0f);
}

void TexturedRoomRenderer::BuildRevealMatrix(T3DMat4FP* out, float s) const {
    // streaming-experience Inc 4: the camera-relative model matrix with a
    // reveal pivot-scale `s` about the render origin. `s == 1.0` is the
    // byte-identical today matrix (scale = 1/ps, translation = origin -
    // camera). Below 1.0 the basis rows scale by `s` while the translation is
    // unchanged, so in camera space the vertex map for world point `w` is
    //   c'(w) = s·(w - o) + (o - cam)
    // The render origin `o` (local L = 0) maps to `o - cam` for EVERY `s`
    // — the pivot is fixed at the cell center — and a cell's extent shrinks
    // by `s` about `o` (a late-loaded cell scales in toward its own center).
    // The RSPQ block is untouched (the matrix is pushed outside it).
    T3DMat4 m;
    const float sc[3] = {kInvScale * s, kInvScale * s, kInvScale * s};
    const float rot[3] = {0, 0, 0};
    const float p[3] = {
        render_origin_.x - camera_pos_.x,
        render_origin_.y - camera_pos_.y,
        render_origin_.z - camera_pos_.z
    };
    t3d_mat4_from_srt_euler(&m, sc, rot, p);
    t3d_mat4_to_fixed(out, &m);
}

void TexturedRoomRenderer::Draw(float reveal_scale) const {
    if (!verts_ || !matrix_fp_) return;

    // streaming-experience Inc 4: compose the reveal pivot-scale into the
    // pushed matrix. At `reveal_scale >= 1.0` push the baked `matrix_fp_`
    // (byte-identical today path); below 1.0 rebuild with the scale (the pivot
    // stays at the render origin). The RSPQ block is unchanged — the matrix is
    // pushed AFTER the block is compiled (outside it), so the reveal adds zero
    // RSP cost.
    T3DMat4FP reveal;
    const float s = reveal_scale < kRevealEpsilon ? kRevealEpsilon
                    : (reveal_scale > 1.0f ? 1.0f : reveal_scale);
    if (s >= 1.0f) {
        t3d_matrix_push(matrix_fp_);
    } else {
        BuildRevealMatrix(&reveal, s);
        t3d_matrix_push(&reveal);
    }

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

int TexturedRoomRenderer::MaterialGroupCount() const {
    return material_group_count_;
}

void TexturedRoomRenderer::MaterialGroupAt(
    int i, uint16_t* out_material, int* out_first_run, int* out_run_count) const {
    if (i < 0 || i >= material_group_count_ || !material_groups_) return;
    if (out_material) *out_material = material_groups_[i].material_id;
    if (out_first_run) *out_first_run = material_groups_[i].first_run;
    if (out_run_count) *out_run_count = material_groups_[i].run_count;
}

void TexturedRoomRenderer::DrawMaterialRun(int first_run, int run_count,
                                           RenderCounters* counters,
                                           float reveal_scale) const {
    if (first_run < 0 || run_count <= 0 || !runs_ || !run_faces_) return;
    if (first_run + run_count > run_count_) run_count = run_count_ - first_run;
    if (run_count <= 0) return;
    // streaming-experience Inc 4: compose the reveal pivot-scale (see `Draw`).
    T3DMat4FP reveal;
    const float s = reveal_scale < kRevealEpsilon ? kRevealEpsilon
                    : (reveal_scale > 1.0f ? 1.0f : reveal_scale);
    if (s >= 1.0f) {
        t3d_matrix_push(matrix_fp_);
    } else {
        BuildRevealMatrix(&reveal, s);
        t3d_matrix_push(&reveal);
    }
    for (int r = first_run; r < first_run + run_count; ++r) {
        EmitRunGeometry(r, counters);
    }
    t3d_matrix_pop(1);
}

void TexturedRoomRenderer::EmitRunState(uint16_t material_id,
                                        RenderCounters* counters) const {
    // Resolve the material's sprite once. If present, upload it as a tile;
    // else fall back to flat primColor for the whole run.
    sprite_t* sprite = catalog_ ? catalog_->MaterialFor(material_id) : nullptr;
    if (sprite) {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH | (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0)));
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        // RDPQ_COMBINER_TEX_FLAT = (TEX0,0,PRIM,0) = TEX0 * PRIM, so
        // primColor MUST be white here or the texture is tinted by whatever
        // primColor the distant pass (or a flat run) last left.
        rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        // memory-opt Inc 3: rdpq_sprite_upload DMA's from sprite->data — the
        // RSP DMA contract needs 8-byte alignment. sprite_load mallocs the
        // struct (8-aligned) and `data` is a flexible array at offset 8, so
        // it holds by construction; assert so a future sprite source cannot
        // regress it.
        assert((reinterpret_cast<uintptr_t>(sprite->data) & 0x7) == 0);
        rdpq_sprite_upload(TILE0, sprite, &kWrapTexparms);
        if (counters) ++counters->texture_uploads;  // Inc 1 / D7
    } else {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0)));
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        uint32_t color = material_color(material_id);
        rdpq_set_prim_color(RGBA32(
            (uint8_t)(color >> 24),
            (uint8_t)(color >> 16),
            (uint8_t)(color >> 8),
            (uint8_t)(color)
        ));
    }
}

void TexturedRoomRenderer::EmitRunGeometry(int r, RenderCounters* counters) const {
    const BatchRun& run = runs_[r];
    if (run.face_count == 0 || run.vertex_count == 0) return;
    // Inc 1 / D7: count near-pass batches (one per run).
    if (counters) ++counters->near_batches;

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

void TexturedRoomRenderer::EmitRunCommands(int r, RenderCounters* counters) const {
    const BatchRun& run = runs_[r];
    if (run.face_count == 0 || run.vertex_count == 0) return;
    EmitRunState(run.material_id, counters);
    EmitRunGeometry(r, counters);
}

void TexturedRoomRenderer::UploadMaterial(uint16_t material_id,
                                          RenderCounters* counters) const {
    EmitRunState(material_id, counters);
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
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH | (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0)));
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        // Same primColor-to-white requirement as the run path (see above).
        rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        // memory-opt Inc 3: sprite upload source — 8-byte RSP DMA contract
        // (see the run path assert above).
        assert((reinterpret_cast<uintptr_t>(sprite->data) & 0x7) == 0);
        rdpq_sprite_upload(TILE0, sprite, &kWrapTexparms);
        if (counters) ++counters->texture_uploads;  // Inc 1 / D7
    } else {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0)));
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
