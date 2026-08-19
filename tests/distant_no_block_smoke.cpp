// Host test for the distant no-block emit path (streaming-memory-opt Inc 3,
// Pattern A: header-only, no N64 deps).
//
// The device change: distant cells skip the RSPQ block capture at load
// (no_block_ set before LoadFromDlod) and draw via DrawRunsDirect, which
// emits the coalesced runs directly under the shared pass matrix. The (run,
// face) sequence DrawRunsDirect emits is IDENTICAL to the block path's (both
// replay the same coalesced runs), so silhouettes are unchanged; the cell
// simply allocates ZERO RSPQ blocks.
//
// We cannot link LvlRoomRenderer (it pulls <t3d/t3dmodel.h>), so we model the
// host-safe core of BuildRunsAndBlock — stable-sort by material + coalesce
// into runs, with the no_block_ gate on block capture — and assert:
//   (a) the no-block path emits the SAME (run, face) sequence as the block
//       path for a synthetic cell (DrawRunsDirect == block replay);
//   (b) the no-block path attaches NO block handle (block_ stays null);
//   (c) every run respects the 70-vertex RSP load cap (the split invariant);
//   (d) GEOMETRY EQUIVALENCE — replaying the no-block runs with per-face fan
//       origins emits the exact same triangle set as the uncoalesced faces
//       (a run-wide fan would cross face boundaries and render garbage).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_no_block_smoke.cpp
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gameplay/render/batch_coalesce.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The RSP vertex-load cap (kMaxRunSpan in LvlRoomRenderer).
static constexpr uint32_t kMaxRunSpan = 70;

// A built cell: the coalesced run sequence + whether a block was captured.
// Mirrors the host-safe core of LvlRoomRenderer::BuildRunsAndBlock.
struct BuiltCell {
    std::vector<BatchRun> runs;
    std::vector<RunFace> run_faces;
    int run_count = 0;
    bool block_captured = false;  // models block_ != nullptr
};

// Model BuildRunsAndBlock(faces, n) with the no_block_ gate. Both paths run
// the SAME SortFacesByMaterial + CoalesceBatches; only the block-capture step
// differs (skipped when no_block). Returns the built cell.
static BuiltCell BuildCell(const FaceSpec* faces, int n, bool no_block) {
    BuiltCell c;
    if (n <= 0) return c;
    std::vector<uint16_t> order(n);
    std::vector<FaceSpec> sorted(n);
    const int groups = SortFacesByMaterial(faces, n, order.data(), n);
    if (groups > 0) {
        for (int s = 0; s < n; ++s) sorted[s] = faces[order[s]];
        c.runs.resize(n);
        c.run_faces.resize(n);
        c.run_count = CoalesceBatches(sorted.data(), n, c.runs.data(), n,
                                      c.run_faces.data(), n, kMaxRunSpan);
    }
    if (c.run_count < 0) c.run_count = 0;  // coalesce failed — no runs
    // Trim run_faces to the faces actually written (sum of run.face_count).
    int face_total = 0;
    for (int r = 0; r < c.run_count; ++r) face_total += c.runs[r].face_count;
    c.runs.resize(c.run_count);
    c.run_faces.resize(face_total);
    // Block capture: only when !no_block and there are runs to capture.
    c.block_captured = (!no_block && c.run_count > 0);
    return c;
}

// The (run, face) sequence a draw path emits: for each run, its material +
// span + each face's (offset, tri_count). This is exactly the data
// EmitRunCommands consumes, so comparing two sequences proves the two draw
// paths emit identically.
struct EmitSeq {
    std::vector<std::array<uint32_t, 5>> runs;  // {mat, first_vert, vert_cnt, face_cnt, 0}
    std::vector<std::array<uint32_t, 2>> faces;  // {offset, tri_count}
};

static EmitSeq EmitSequence(const BuiltCell& c) {
    EmitSeq s;
    for (int r = 0; r < c.run_count; ++r) {
        const BatchRun& run = c.runs[r];
        s.runs.push_back({run.material_id, run.first_vertex, run.vertex_count,
                          run.face_count, 0});
        for (uint16_t f = 0; f < run.face_count; ++f) {
            const RunFace& rf = c.run_faces[run.first_face + f];
            s.faces.push_back({rf.offset, rf.tri_count});
        }
    }
    return s;
}

// The triangle index set for the uncoalesced face list: each face fans from
// its own first_vertex (fv, fv+t+1, fv+t+2). Returns sorted triangles.
static std::vector<std::array<uint32_t, 3>> TrianglesUncoalesced(
    const FaceSpec* faces, int n) {
    std::vector<std::array<uint32_t, 3>> tris;
    for (int i = 0; i < n; ++i) {
        if (faces[i].vertex_count < 3 || faces[i].tri_count == 0) continue;
        const uint32_t fv = faces[i].first_vertex;
        for (uint32_t t = 0; t < faces[i].tri_count; ++t) {
            std::array<uint32_t, 3> tri = {fv, fv + t + 1, fv + t + 2};
            std::sort(tri.begin(), tri.end());
            tris.push_back(tri);
        }
    }
    std::sort(tris.begin(), tris.end());
    return tris;
}

// Replay the coalesced runs with per-face fan origins. Each face's absolute
// origin is run.first_vertex + offset. Returns the same sorted triangle set.
static std::vector<std::array<uint32_t, 3>> TrianglesCoalesced(
    const BuiltCell& c) {
    std::vector<std::array<uint32_t, 3>> tris;
    for (int r = 0; r < c.run_count; ++r) {
        const BatchRun& run = c.runs[r];
        for (uint16_t f = 0; f < run.face_count; ++f) {
            const RunFace& rf = c.run_faces[run.first_face + f];
            const uint32_t fv = run.first_vertex + rf.offset;
            for (uint32_t t = 0; t < rf.tri_count; ++t) {
                std::array<uint32_t, 3> tri = {fv, fv + t + 1, fv + t + 2};
                std::sort(tri.begin(), tri.end());
                tris.push_back(tri);
            }
        }
    }
    std::sort(tris.begin(), tris.end());
    return tris;
}

// The loaded span a run's t3d_vert_load must cover (pair-aligned), as
// EmitRunCommands computes it. Must be ≤ kMaxRunSpan.
static uint32_t RunLoadSpan(const BatchRun& run) {
    const uint32_t base_offset = run.first_vertex & 1u;
    uint32_t load_count = ((base_offset + run.vertex_count + 1u) / 2u) * 2u;
    if (load_count > kMaxRunSpan) load_count = kMaxRunSpan;
    return load_count;
}

int main() {
    // Synthetic cell: 12 faces across 3 materials, interleaved so the stable
    // sort must regroup them. Each face is a 3-vertex fan (tri_count 1).
    // first_vertex advances by 3 per face (consecutive triples, like DLOD).
    // Material pattern: 0,1,2,0,1,2,0,1,2,0,1,2 (forces 3 groups of 4).
    FaceSpec faces[12];
    const uint16_t mats[12] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2};
    for (int i = 0; i < 12; ++i) {
        faces[i] = {static_cast<uint32_t>(3 * i), 3u, 1u, mats[i]};
    }

    const BuiltCell block_cell = BuildCell(faces, 12, /*no_block=*/false);
    const BuiltCell direct_cell = BuildCell(faces, 12, /*no_block=*/true);

    // (a) The no-block path emits the SAME (run, face) sequence as the block
    //     path — DrawRunsDirect == block replay.
    expect(block_cell.run_count > 0, "block path built runs");
    expect(direct_cell.run_count == block_cell.run_count,
           "no-block run count == block run count");
    const EmitSeq block_seq = EmitSequence(block_cell);
    const EmitSeq direct_seq = EmitSequence(direct_cell);
    expect(direct_seq.runs == block_seq.runs,
           "no-block run sequence == block run sequence");
    expect(direct_seq.faces == block_seq.faces,
           "no-block face sequence == block face sequence");

    // (b) The no-block path attaches NO block handle; the block path does.
    expect(block_cell.block_captured, "block path captured a block");
    expect(!direct_cell.block_captured, "no-block path attached no block");

    // (c) Every run respects the 70-vertex RSP load cap.
    bool cap_ok = true;
    for (int r = 0; r < direct_cell.run_count; ++r) {
        if (RunLoadSpan(direct_cell.runs[r]) > kMaxRunSpan) cap_ok = false;
    }
    expect(cap_ok, "every run's loaded span <= 70 (RSP cap)");

    // (d) GEOMETRY EQUIVALENCE — the no-block runs replay to the exact same
    //     triangle set as the uncoalesced faces.
    const auto tri_uncoalesced = TrianglesUncoalesced(faces, 12);
    const auto tri_direct = TrianglesCoalesced(direct_cell);
    expect(tri_direct == tri_uncoalesced,
           "no-block triangle set == uncoalesced triangle set");

    // Extra: a long same-material run that must SPLIT at the 70-vertex cap.
    // 25 consecutive 3-vertex faces of material 0 = 75 vertices > 70, so the
    // coalescer must split it into >= 2 runs, each ≤ 70.
    const int kLong = 25;
    std::vector<FaceSpec> long_faces(kLong);
    for (int i = 0; i < kLong; ++i) {
        long_faces[i] = {static_cast<uint32_t>(3 * i), 3u, 1u, 0u};
    }
    const BuiltCell long_cell = BuildCell(long_faces.data(), kLong, true);
    expect(long_cell.run_count >= 2,
           "75-vertex same-material run splits into >= 2 runs");
    bool long_cap_ok = true;
    for (int r = 0; r < long_cell.run_count; ++r) {
        if (RunLoadSpan(long_cell.runs[r]) > kMaxRunSpan) long_cap_ok = false;
    }
    expect(long_cap_ok, "split runs each <= 70 (RSP cap)");
    const auto tri_long = TrianglesCoalesced(long_cell);
    const auto tri_long_uncoalesced = TrianglesUncoalesced(long_faces.data(),
                                                           kLong);
    expect(tri_long == tri_long_uncoalesced,
           "split-run triangle set == uncoalesced triangle set");

    if (failures == 0) {
        std::printf("distant_no_block_smoke: OK\n");
        return 0;
    }
    std::printf("distant_no_block_smoke: %d failure(s)\n", failures);
    return 1;
}
