#pragma once

#include <cstdint>

namespace madeline_cube {

// ── Material-run batch coalescing (Inc 3 / D3) ────────────────────────────
// Both room renderers (`LvlRoomRenderer`, `TexturedRoomRenderer`) merge
// ADJACENT same-material faces at load time into runs. A run shares one
// RDP-state block (sprite upload, combiner, prim color) plus one
// `t3d_vert_load` + one `t3d_tri_sync`, but EMITS EACH FACE AS ITS OWN FAN
// (a run stores the sub-range of its faces) so triangulation never crosses a
// face boundary. Runs are capped so a single `t3d_vert_load` never exceeds the
// RSP vertex-cache limit (70 vertices, `T3D_VERTEX_CACHE_SIZE`).
//
// Host-safe — plain integers/floats, no N64 types.

// One face as read from the .lvl (an independent fan of `vertex_count`
// vertices starting at `first_vertex`).
struct FaceSpec {
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t tri_count;       // vertex_count - 2 (fan)
    uint16_t material_id;
};

// Stable-sort `src` faces by material_id (ascending), writing the reordered
// indices to `out_order` (a permutation of 0..n-1). Returns the number of
// distinct material groups, or -1 if `out_capacity < n` (caller falls back to
// unsorted coalescing — never a silent truncation). The sort is STABLE so
// faces within a material keep their original order (preserves per-face fan
// origins — each face still fans from its own first_vertex, so reordering is
// safe for the Z-on near pass). Host-safe — no N64 types.
inline int SortFacesByMaterial(const FaceSpec* src, int n,
                               uint16_t* out_order, int out_capacity) {
    if (!src || n <= 0 || !out_order) return 0;
    if (out_capacity < n) return -1;
    // Initialize the identity permutation.
    for (int i = 0; i < n; ++i) out_order[i] = static_cast<uint16_t>(i);
    // Stable insertion sort by material_id (n is small — a cell's face count
    // is bounded by kMaxBatches=1024; insertion sort is O(n²) but n is small
    // and this runs once at load time, not per frame).
    for (int i = 1; i < n; ++i) {
        const uint16_t key = out_order[i];
        const uint16_t key_mat = src[key].material_id;
        int j = i - 1;
        while (j >= 0 && src[out_order[j]].material_id > key_mat) {
            out_order[j + 1] = out_order[j];
            --j;
        }
        out_order[j + 1] = key;
    }
    // Count distinct material groups.
    int groups = 0;
    for (int i = 0; i < n; ++i) {
        if (i == 0 || src[out_order[i]].material_id != src[out_order[i - 1]].material_id) {
            ++groups;
        }
    }
    return groups;
}

// One coalesced run: a contiguous span of same-material faces.
// `vertex_count` is the run span measured from the even-aligned load start so
// that `(first_vertex & 1) + vertex_count <= max_span` (see CoalesceBatches) —
// this guarantees the draw-time `t3d_vert_load` never exceeds the RSP cap.
struct BatchRun {
    uint32_t first_vertex;   // run span start (absolute vertex index)
    uint32_t vertex_count;   // run span length (loaded-span cap, ≤ max_span)
    uint16_t material_id;
    uint16_t first_face;     // index into out_faces (first face of this run)
    uint16_t face_count;     // faces in this run (adjacent, same material)
};

// One face within a run. `offset` is the face's origin relative to
// `run.first_vertex`, so `Draw` fans it from `offset + (run.first_vertex & 1)`.
struct RunFace {
    uint32_t offset;         // face origin relative to run.first_vertex
    uint32_t tri_count;
};

// Merge adjacent same-material faces into runs, splitting when the loaded
// span would exceed `max_span` (the RSP vertex-load cap, 70) or the material
// changes. Degenerate faces (vertex_count < 3) are skipped (they emit no
// geometry) and do not break adjacency. `out_faces` is a flattened per-face
// list grouped by run and indexed by `BatchRun.first_face`.
//
// Returns the number of runs, or -1 if either capacity is exceeded (the
// caller falls back to per-face batches — never a silent truncation).
inline int CoalesceBatches(const FaceSpec* src, int n, BatchRun* out,
                           int out_cap, RunFace* out_faces, int face_cap,
                           uint32_t max_span) {
    if (!src || n <= 0 || !out || out_cap <= 0 || !out_faces || face_cap <= 0) {
        return -1;
    }
    if (max_span < 3) max_span = 3;

    int run_count = 0;
    int face_out = 0;
    int i = 0;
    while (i < n) {
        if (src[i].vertex_count < 3 || src[i].tri_count == 0) {
            ++i;  // degenerate — emit nothing, keep scanning
            continue;
        }
        const uint16_t mat = src[i].material_id;
        const uint32_t run_first = src[i].first_vertex;
        const uint32_t align = run_first & 1u;  // pair-grid offset at draw
        uint32_t run_end = run_first + src[i].vertex_count;  // exclusive
        const int first_face = face_out;
        if (run_count >= out_cap || face_out >= face_cap) return -1;
        out_faces[face_out++] = {0u, src[i].tri_count};  // first face, offset 0
        int face_count = 1;

        int j = i + 1;
        while (j < n) {
            const FaceSpec& nf = src[j];
            if (nf.vertex_count < 3 || nf.tri_count == 0) {
                ++j;  // degenerate — does not break adjacency
                continue;
            }
            if (nf.material_id != mat) break;  // material change ends the run
            const uint32_t nf_end = nf.first_vertex + nf.vertex_count;
            const uint32_t new_end = nf_end > run_end ? nf_end : run_end;
            // Span measured from the even-aligned load start; one t3d_vert_load
            // must cover the whole run without exceeding the RSP cap.
            if (align + (new_end - run_first) > max_span) break;
            if (face_out >= face_cap) return -1;
            out_faces[face_out++] = {nf.first_vertex - run_first, nf.tri_count};
            ++face_count;
            run_end = new_end;
            ++j;
        }

        out[run_count++] = {
            run_first, run_end - run_first, mat,
            static_cast<uint16_t>(first_face), static_cast<uint16_t>(face_count)};
        i = j;
    }
    return run_count;
}

}  // namespace madeline_cube
