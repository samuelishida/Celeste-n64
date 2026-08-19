// Host smoke test for streaming-memory-opt Inc 4 (global near-pass material
// grouping). Pattern A: header-only, no renderer linking (the renderers pull
// <t3d/t3dmodel.h>, N64-only).
//
// The near pass is fully opaque (the depth buffer resolves order), so draws
// can be reordered by material without changing the visible result. Inc 4
// groups the near pass per-material → per-cell so each TMEM sprite is uploaded
// ONCE per material instead of once per (material, cell). This test verifies
// the host-safe triple-list helpers that drive that grouping:
//   1. SortMaterialTriplesByMaterial groups all cells of one material
//      contiguously (and is stable within a material).
//   2. CountDistinctMaterials == the number of distinct materials (== the
//      number of sprite uploads per frame), which is strictly less than the
//      sum of per-cell distinct counts when materials overlap across cells.
//   3. The (face, material) multiset is preserved: per-material total run_count
//      and per-(cell, material) run_count are unchanged by the sort.
//   4. No run is split or merged: each (cell, material) triple keeps its own
//      run_count (the per-cell 70-vertex cap is a property of the runs, which
//      the grouping never touches).
//
// Build: g++ -std=c++17 -Isrc/user tests/near_global_sort_smoke.cpp -o /tmp/near_global_sort_smoke
// Run:   /tmp/near_global_sort_smoke

#include <cstdio>
#include <cstdint>

#include "gameplay/render/tile_streamer.hpp"

using madeline_cube::NearMaterialTriple;
using madeline_cube::SortMaterialTriplesByMaterial;
using madeline_cube::CountDistinctMaterials;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// A synthetic per-cell material group (mirrors TexturedRoomRenderer::MaterialGroup).
struct CellGroup {
    uint16_t material_id;
    int first_run;
    int run_count;
};

// Build the triple list exactly as TileStreamer::DrawHighPriority does: iterate
// cells in order, append each cell's material groups in order.
int BuildTriples(const CellGroup* const groups[], const int group_counts[],
                 int cell_count, NearMaterialTriple* out) {
    int n = 0;
    for (int c = 0; c < cell_count; ++c) {
        for (int g = 0; g < group_counts[c]; ++g) {
            out[n].material_id = groups[c][g].material_id;
            out[n].cell_index = (int16_t)c;
            out[n].first_run = (uint16_t)groups[c][g].first_run;
            out[n].run_count = (uint16_t)groups[c][g].run_count;
            ++n;
        }
    }
    return n;
}

}  // namespace

int main() {
    // 3 synthetic cells with overlapping materials.
    //   cell 0: mat 0 (2 runs), mat 1 (1 run), mat 2 (3 runs)
    //   cell 1: mat 1 (1 run), mat 2 (2 runs), mat 3 (1 run)
    //   cell 2: mat 0 (1 run), mat 2 (2 runs)
    const CellGroup cell0[] = {
        {0, 0, 2}, {1, 2, 1}, {2, 3, 3},
    };
    const CellGroup cell1[] = {
        {1, 0, 1}, {2, 1, 2}, {3, 3, 1},
    };
    const CellGroup cell2[] = {
        {0, 0, 1}, {2, 1, 2},
    };
    const CellGroup* const groups[] = {cell0, cell1, cell2};
    const int group_counts[] = {3, 3, 2};
    const int cell_count = 3;

    // Expected per-material total run_count (the (face, material) multiset).
    //   mat 0: cell0(2) + cell2(1) = 3
    //   mat 1: cell0(1) + cell1(1) = 2
    //   mat 2: cell0(3) + cell1(2) + cell2(2) = 7
    //   mat 3: cell1(1) = 1
    // Total triples = 3 + 3 + 2 = 8 (one per (cell, material) group).
    // Total run_count sum = 6 + 4 + 3 = 13 (the (face, material) multiset size).
    const int expected_triple_count = 8;
    const int expected_run_sum = 13;
    const int expected_per_material[4] = {3, 2, 7, 1};

    NearMaterialTriple triples[64];
    const int n = BuildTriples(groups, group_counts, cell_count, triples);
    Check(n == expected_triple_count, "triple count == 8 (one per (cell, material))");

    int run_sum = 0;
    for (int i = 0; i < n; ++i) run_sum += triples[i].run_count;
    Check(run_sum == expected_run_sum, "run_count sum == 13 ((face, material) multiset)");

    // Sort by material (the production path sorts before counting uploads).
    SortMaterialTriplesByMaterial(triples, n);

    // Sum of per-cell distinct counts (the legacy per-(material, cell) upload
    // count). The global grouping must be strictly less when materials overlap.
    const int legacy_uploads = group_counts[0] + group_counts[1] + group_counts[2];  // 8
    const int distinct = CountDistinctMaterials(triples, n);
    Check(distinct == 4, "distinct materials == 4");
    Check(distinct < legacy_uploads,
          "global uploads (4) < legacy per-(material,cell) uploads (8)");

    // 1. Grouped: all cells of one material are contiguous.
    for (int i = 1; i < n; ++i) {
        Check(triples[i - 1].material_id <= triples[i].material_id,
              "triples sorted by material (non-decreasing)");
    }

    // 2. Stable within a material: cells appear in ascending cell_index order.
    for (int i = 1; i < n; ++i) {
        if (triples[i].material_id == triples[i - 1].material_id) {
            Check(triples[i].cell_index >= triples[i - 1].cell_index,
                  "stable within a material (cell_index non-decreasing)");
        }
    }

    // 3. (face, material) multiset preserved: per-material total run_count
    //    matches the expected value, and each (cell, material) triple keeps its
    //    own run_count (no run split/merged — the per-cell 70-vertex cap is a
    //    property of the runs, which the grouping never touches).
    int per_material[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; ++i) {
        const int m = triples[i].material_id;
        Check(m >= 0 && m < 4, "material id in range");
        per_material[m] += triples[i].run_count;
    }
    for (int m = 0; m < 4; ++m) {
        Check(per_material[m] == expected_per_material[m],
              "per-material total run_count preserved");
    }

    // Each (cell, material) pair appears exactly once with its original
    // run_count (the grouping reorders triples, never splits or merges runs).
    for (int c = 0; c < cell_count; ++c) {
        for (int g = 0; g < group_counts[c]; ++g) {
            const CellGroup& cg = groups[c][g];
            int found = 0;
            for (int i = 0; i < n; ++i) {
                if (triples[i].cell_index == c &&
                    triples[i].material_id == cg.material_id) {
                    Check(triples[i].run_count == cg.run_count,
                          "(cell, material) run_count preserved");
                    Check(triples[i].first_run == cg.first_run,
                          "(cell, material) first_run preserved");
                    ++found;
                }
            }
            Check(found == 1, "each (cell, material) appears exactly once");
        }
    }

    if (g_failures == 0) {
        std::printf("near_global_sort_smoke: OK\n");
        return 0;
    }
    std::printf("near_global_sort_smoke: %d failure(s)\n", g_failures);
    return 1;
}
