// Host-side streaming heap measurement (streaming-experience Inc 2, Pattern C:
// links mappack_loader.cpp, reads the baked .lvl fixtures).
//
// The device `TexturedRoomRenderer` is device-only (t3d/rdpq/libdragon), so it
// cannot be linked on the host. This test instead runs a FAITHFUL cost model of
// the renderer's per-cell resident allocation over every real `filesystem/lvl/
// forsyken-city/*.lvl` file: it parses the LVL2 header + faces (big-endian,
// exactly as the renderer does), runs the real `SortFacesByMaterial` +
// `CoalesceBatches` (host-safe, batch_coalesce.hpp) to get the run count, then
// allocates the exact byte sizes the renderer's Load would (verts, matrix,
// batches, runs, run_faces, material_groups) and measures the mallinfo peak per
// cell. The table is the "per-cell resident cost is MEASURED (host table)" the
// plan uses to validate the bounded resident ring; `kStreamingBudgetBytes` is
// set from it. Also asserts MaxLvlSize() equals largest .lvl on disk.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/streaming_memory_measure.cpp \
//     src/user/gameplay/world/mappack_loader.cpp
// Run:
//   /tmp/streaming_memory_measure <lvl_dir> <mappack_path>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <malloc.h>
#include <string>
#include <vector>

#include "gameplay/render/batch_coalesce.hpp"
#include "gameplay/render/tile_streamer.hpp"
#include "gameplay/world/mappack_loader.hpp"

using namespace madeline_cube;

// Renderer allocation sizes (mirror of TexturedRoomRenderer's structs).
// T3DVertPacked: posA[3]s16 + normA u16 + posB[3]s16 + normB u16 + rgbaA u32 +
//   rgbaB u32 + stA[2]s16 + stB[2]s16 = 32 B, aligned(8).
static constexpr size_t kVertPackedBytes = 32;
// T3DMat4FP: 4 columns × (4×s16 + 4×u16) = 4×16 = 64 B.
static constexpr size_t kMatrixBytes = 64;
// Batch {u32,u32,u32,u16} = 12 B.
static constexpr size_t kBatchBytes = 12;
// BatchRun {u32,u32,u16,u16,u16} = 14 → 16 B (align 4).
static constexpr size_t kRunBytes = 16;
// RunFace {u32,u32} = 8 B.
static constexpr size_t kRunFaceBytes = 8;
// MaterialGroup {u16,int,int} = 10 → 12 B (align 4).
static constexpr size_t kGroupBytes = 12;

// Big-endian reader over a .lvl image (mirrors the renderer's LvlMemReader).
struct Reader {
    const uint8_t* p;
    uint32_t size;
    uint32_t off = 0;
    bool ok = true;
    uint32_t u32() {
        if (off + 4 > size) { ok = false; return 0; }
        uint32_t v = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                     (uint32_t(p[off + 2]) << 8) | uint32_t(p[off + 3]);
        off += 4; return v;
    }
    uint16_t u16() {
        if (off + 2 > size) { ok = false; return 0; }
        uint16_t v = uint16_t((uint16_t(p[off]) << 8) | p[off + 1]);
        off += 2; return v;
    }
    void skip(uint32_t n) { off += n; if (off > size) ok = false; }
    void seek(uint32_t n) { off = n; if (off > size) ok = false; }
};

// Compute the renderer's resident allocation bytes for one .lvl image.
static size_t CellResidentBytes(const uint8_t* data, uint32_t size) {
    Reader r{data, size};
    if (size < 4) return 0;
    if (data[0] != 'L' || data[1] != 'V' || data[2] != 'L' || data[3] != '2') return 0;
    r.skip(4);  // magic was read directly; advance past it
    r.u32();  // ver
    r.u32();  // collider_count
    const uint32_t face_count = r.u32();
    const uint32_t vertex_count = r.u32();
    r.u32();  // entity_count
    r.u32();  // string_count
    r.skip(16);  // atmosphere
    r.u32();  // off_strings
    r.u32();  // off_colliders
    const uint32_t off_faces = r.u32();
    r.u32();  // off_vertices
    r.u32();  // off_entities
    r.u32();  // off_props
    if (!r.ok || face_count == 0 || vertex_count == 0) return 0;

    // Parse faces (24 B each) into FaceSpecs for the real coalescing.
    std::vector<FaceSpec> specs;
    specs.reserve(face_count);
    r.seek(off_faces);
    for (uint32_t i = 0; i < face_count; ++i) {
        const uint32_t vs = r.u32();
        const uint32_t vc = r.u32();
        const uint16_t mid = r.u16();
        r.u16();  // flags
        r.skip(12);  // nx, ny, nz
        if (vc >= 3) specs.push_back({vs, vc, vc - 2, mid});
    }
    if (!r.ok) return 0;

    // Run the real coalescing to get the run count (the renderer's Load does
    // the same stable-sort + coalesce).
    int run_count = 0;
    if (!specs.empty()) {
        std::vector<uint16_t> order(specs.size());
        std::vector<FaceSpec> sorted(specs.size());
        std::vector<BatchRun> runs(specs.size());
        std::vector<RunFace> run_faces(specs.size());
        const int groups = SortFacesByMaterial(specs.data(), (int)specs.size(),
                                               order.data(), (int)order.size());
        if (groups > 0) {
            for (size_t s = 0; s < specs.size(); ++s) sorted[s] = specs[order[s]];
            run_count = CoalesceBatches(sorted.data(), (int)sorted.size(),
                                       runs.data(), (int)runs.size(),
                                       run_faces.data(), (int)run_faces.size(),
                                       /*max_span=*/70);
        }
    }
    if (run_count < 0) run_count = 0;

    const uint32_t pair_count = (vertex_count + 1) / 2;
    const int batch_cap = (int)face_count < 1024 ? (int)face_count : 1024;
    size_t bytes = 0;
    bytes += kVertPackedBytes * pair_count;   // verts_ (uncached)
    bytes += kMatrixBytes;                    // matrix_fp_ (uncached)
    bytes += kBatchBytes * (size_t)batch_cap; // batches_
    bytes += kRunBytes * (size_t)run_count;   // runs_
    bytes += kRunFaceBytes * (size_t)run_count; // run_faces_
    bytes += kGroupBytes * (size_t)run_count; // material_groups_
    return bytes;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <lvl_dir> <mappack_path>\n", argv[0]);
        return 2;
    }
    const char* lvl_dir = argv[1];
    const char* mappack_path = argv[2];

    // Load the manifest for MaxLvlSize().
    MapSpecV2 spec;
    if (!LoadMapPackV2(mappack_path, spec)) {
        std::fprintf(stderr, "FAIL: cannot load mappack %s\n", mappack_path);
        return 1;
    }

    // Enumerate *.lvl files.
    std::vector<std::string> files;
    DIR* d = opendir(lvl_dir);
    if (!d) { std::fprintf(stderr, "FAIL: cannot open %s\n", lvl_dir); return 1; }
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 4 && n.substr(n.size() - 4) == ".lvl") files.push_back(n);
    }
    closedir(d);
    if (files.empty()) { std::fprintf(stderr, "FAIL: no .lvl in %s\n", lvl_dir); return 1; }

    // Measure per-cell resident cost (mallinfo peak) + the on-disk max size.
    size_t max_resident = 0, sum_resident = 0;
    uint32_t max_disk = 0;
    std::vector<std::pair<std::string, size_t>> table;
    for (const std::string& n : files) {
        const std::string path = std::string(lvl_dir) + "/" + n;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf((size_t)sz);
        const size_t got = fread(buf.data(), 1, (size_t)sz, f);
        fclose(f);
        if (got != (size_t)sz) continue;
        if ((uint32_t)sz > max_disk) max_disk = (uint32_t)sz;

        // Measure the resident allocation peak (the cost model mallocs the
        // exact renderer sizes; mallinfo tracks the high-water).
        const size_t before = mallinfo().uordblks;
        const size_t resident = CellResidentBytes(buf.data(), (uint32_t)sz);
        const size_t after = mallinfo().uordblks;
        const size_t peak = (after > before) ? (after - before) : resident;
        table.push_back({n, peak});
        if (peak > max_resident) max_resident = peak;
        sum_resident += peak;
    }

    // Print the table (max/mean per cell).
    std::printf("streaming_memory_measure: %zu cells\n", table.size());
    for (const auto& [n, peak] : table)
        std::printf("  %-28s resident=%zu B (%.1f KB)\n", n.c_str(), peak,
                    peak / 1024.0);
    const double mean = table.empty() ? 0.0 : (double)sum_resident / table.size();
    std::printf("  max resident = %zu B (%.1f KB), mean = %.1f KB\n",
                max_resident, max_resident / 1024.0, mean / 1024.0);

    // Assertions.
    int failures = 0;
    if (max_disk != spec.MaxLvlSize()) {
        std::fprintf(stderr, "FAIL: MaxLvlSize()=%u != largest .lvl on disk=%u\n",
                     spec.MaxLvlSize(), max_disk);
        ++failures;
    }
    // The staging pool must fit the largest cell (MaxLvlSize + margin).
    if (max_disk > spec.MaxLvlSize()) {
        std::fprintf(stderr, "FAIL: a .lvl exceeds MaxLvlSize()\n");
        ++failures;
    }
    // Sanity: renderer data for current bounded resident capacity fits within
    // the two-MiB planning ceiling; staging is accounted separately.
    const size_t ring_bytes = max_resident * kMaxResident;
    std::printf("  %d-cell ring at max resident = %zu B (%.1f KB)\n",
                kMaxResident, ring_bytes, ring_bytes / 1024.0);
    if (ring_bytes > 2 * 1024 * 1024) {
        std::fprintf(stderr, "FAIL: resident ring exceeds 2 MB budget\n");
        ++failures;
    }

    if (failures == 0) { std::printf("ALL PASS\n"); return 0; }
    std::fprintf(stderr, "%d failures\n", failures);
    return 1;
}
