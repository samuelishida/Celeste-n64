// Host test for the DMA/alignment audit (memory-opt Inc 3). Pattern A:
// header-only, no N64 deps. Runs in `make test`.
//
// Enumerates every DMA-facing buffer in the renderer/streamer/loader code
// (the Inc 3 audit table) and asserts:
//   (a) every registered buffer's expected alignment matches its kind
//       (uncached-dma → 16, sprite-upload → 8, cpu-copied → 0 = n/a);
//   (b) the registry's per-file malloc_uncached / malloc counts match the
//       actual allocation sites in the audited sources (comment lines are
//       skipped) — a NEW allocation must be registered and classified or
//       this test fails.
//
// The audit classification (see the Inc 3 plan section):
//   - malloc_uncached buffers (staging pools, packed verts, matrices) are
//     DMA-direct and 16-byte aligned by libdragon contract (n() =
//     malloc_uncached_aligned(16, ...)); the code asserts this at each site.
//   - sprite->data feeds rdpq_sprite_upload (RSP DMA, 8-byte contract);
//     sprite_load mallocs the struct (8-aligned) and data is a flexible
//     array at offset 8, so it holds by construction.
//   - fopen/asset_load read buffers and the CPU-side parse intermediates
//     (faces, verts, batches, runs, specs, counts, material groups) are
//     CPU-copied — no DMA dependency, alignment n/a (documented in code).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/alignment_audit_contract.cpp
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct AuditEntry {
    const char* name;    // buffer name
    const char* file;    // source file (repo-relative)
    int line;            // allocation line (informational)
    const char* kind;    // "uncached-dma" | "sprite-upload" | "cpu-copied"
    int expect_align;    // 16 / 8 / 0 (n/a)
    bool is_alloc;       // true if this entry IS a malloc site (counted)
};

// The Inc 3 audit table, updated for Inc 4 (the textured renderer's CPU-side
// parse buffers moved into the pool's per-slot buffers / shared scratch —
// 16-aligned by design, covered by renderer_pool_smoke). Keep in sync with
// the code — the per-file count assertions below enforce that a new
// allocation must be registered here.
const AuditEntry kAudit[] = {
    // tile_streamer.cpp — staging pools feed dma_read_async (RSP DMA).
    {"staging pool (ReserveStreamingBudget)", "src/user/gameplay/render/tile_streamer.cpp", 101, "uncached-dma", 16, true},
    {"staging fallback (IssueAsyncRead)", "src/user/gameplay/render/tile_streamer.cpp", 378, "uncached-dma", 16, true},
    {"staging pool (SetAsyncStagingPool, caller-owned)", "src/user/gameplay/render/tile_streamer.cpp", 64, "uncached-dma", 16, false},
    // textured_room_renderer.cpp — RSP-DMA'd data (16-aligned, asserted).
    // The CPU-side parse buffers (file buf, faces, lvl_verts, batches, specs,
    // runs, run_faces, counts, material groups) moved to the pool slot buffer
    // + shared scratch in Inc 4 (16-aligned by design; renderer_pool_smoke
    // covers the alignment) — no plain malloc remains in this file.
    {"packed verts (T3DVertPacked)", "src/user/gameplay/render/textured_room_renderer.cpp", 212, "uncached-dma", 16, true},
    {"matrix (T3DMat4FP)", "src/user/gameplay/render/textured_room_renderer.cpp", 346, "uncached-dma", 16, true},
    // level_renderer.cpp — legacy single-room flat path (not instantiated by
    // the map-pack path; kept for the record).
    {"packed verts (T3DVertPacked)", "src/user/gameplay/render/level_renderer.cpp", 38, "uncached-dma", 16, true},
    {"identity matrix (T3DMat4FP)", "src/user/gameplay/render/level_renderer.cpp", 100, "uncached-dma", 16, true},
    // lvl_room_renderer.cpp — flat path used by tile_streamer. Load()'s
    // CPU-side buffers moved to the pool in Inc 4; the remaining plain
    // mallocs are the DLOD append paths (dead at runtime — the distant tier
    // is not loaded; documented, not pooled).
    {"packed verts (T3DVertPacked, Load)", "src/user/gameplay/render/lvl_room_renderer.cpp", 141, "uncached-dma", 16, true},
    {"matrix (T3DMat4FP, Load)", "src/user/gameplay/render/lvl_room_renderer.cpp", 236, "uncached-dma", 16, true},
    {"packed verts (T3DVertPacked, LoadFromDlod)", "src/user/gameplay/render/lvl_room_renderer.cpp", 275, "uncached-dma", 16, true},
    {"matrix (T3DMat4FP, LoadFromDlod)", "src/user/gameplay/render/lvl_room_renderer.cpp", 328, "uncached-dma", 16, true},
    {"matrix (T3DMat4FP, AppendFromDlod)", "src/user/gameplay/render/lvl_room_renderer.cpp", 418, "uncached-dma", 16, true},
    {"batches (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 297, "cpu-copied", 0, true},
    {"specs (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 315, "cpu-copied", 0, true},
    {"runs (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 454, "cpu-copied", 0, true},
    {"run_faces (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 455, "cpu-copied", 0, true},
    {"order (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 462, "cpu-copied", 0, true},
    {"sorted_specs (DLOD append)", "src/user/gameplay/render/lvl_room_renderer.cpp", 464, "cpu-copied", 0, true},
    // coll_mesh.cpp — CPU-copied (fopen→fread / memcpy), no DMA.
    {"coll mesh file buffer", "src/user/gameplay/physics/coll_mesh.cpp", 199, "cpu-copied", 0, true},
    {"coll mesh from-buffer copy", "src/user/gameplay/physics/coll_mesh.cpp", 215, "cpu-copied", 0, true},
    // texture.cpp — sprite data feeds rdpq_sprite_upload (RSP DMA, 8-byte).
    {"sprite data (sprite_load)", "src/user/gameplay/render/texture.cpp", 0, "sprite-upload", 8, false},
    // mappack_loader.cpp / level_loader.cpp — CPU-copied reads, no DMA.
    {"manifest buffer (OpenMapPackFile)", "src/user/gameplay/world/mappack_loader.cpp", 0, "cpu-copied", 0, false},
    {"lvl buffer (LoadLevelInto)", "src/user/gameplay/world/level_loader.cpp", 0, "cpu-copied", 0, false},
};

std::string ReadFile(const char* path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Count allocation sites on non-comment lines. A line is skipped when its
// first non-whitespace char is '/', '*', or it is empty (comments mention
// malloc_uncached(...) and must not count).
int CountAllocSites(const std::string& text, const char* needle) {
    int n = 0;
    std::istringstream lines(text);
    std::string line;
    const size_t needle_len = std::string(needle).size();
    while (std::getline(lines, line)) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) continue;              // blank
        if (line[i] == '/' || line[i] == '*') continue;  // comment
        size_t pos = 0;
        while ((pos = line.find(needle, pos)) != std::string::npos) {
            ++n;
            pos += needle_len;
        }
    }
    return n;
}

void TestRegistrySanity() {
    std::printf("[alignment_audit_contract] registry sanity\n");
    for (const AuditEntry& e : kAudit) {
        const std::string kind(e.kind);
        if (kind == "uncached-dma") {
            expect(e.expect_align == 16, e.name);
        } else if (kind == "sprite-upload") {
            expect(e.expect_align == 8, e.name);
        } else if (kind == "cpu-copied") {
            expect(e.expect_align == 0, e.name);
        } else {
            expect(false, e.name);
        }
    }
}

void TestInventoryMatch() {
    std::printf("[alignment_audit_contract] source inventory match\n");
    // Per-file expected counts from the registry (allocation entries only).
    struct FileCounts {
        std::string file;
        int uncached = 0;
        int plain = 0;
    };
    std::vector<FileCounts> files;
    for (const AuditEntry& e : kAudit) {
        if (!e.is_alloc) continue;
        FileCounts* fc = nullptr;
        for (FileCounts& f : files) {
            if (f.file == e.file) { fc = &f; break; }
        }
        if (!fc) {
            files.push_back(FileCounts{e.file, 0, 0});
            fc = &files.back();
        }
        if (std::string(e.kind) == "uncached-dma") ++fc->uncached;
        else if (std::string(e.kind) == "cpu-copied") ++fc->plain;
    }
    for (const FileCounts& fc : files) {
        const std::string text = ReadFile(fc.file.c_str());
        if (text.empty()) {
            expect(false, fc.file.c_str());
            continue;
        }
        const int uncached = CountAllocSites(text, "malloc_uncached(");
        // "malloc(" is NOT a substring of "malloc_uncached(" (underscore), so
        // this counts plain malloc sites directly.
        const int plain = CountAllocSites(text, "malloc(");
        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s: malloc_uncached count (%d vs %d)",
                      fc.file.c_str(), uncached, fc.uncached);
        expect(uncached == fc.uncached, msg);
        std::snprintf(msg, sizeof(msg), "%s: plain malloc count (%d vs %d)",
                      fc.file.c_str(), plain, fc.plain);
        expect(plain == fc.plain, msg);
    }
}

}  // namespace

int main() {
    TestRegistrySanity();
    TestInventoryMatch();
    if (g_failures == 0) {
        std::printf("PASS: alignment_audit_contract\n");
        return 0;
    }
    std::printf("FAILED: alignment_audit_contract (%d)\n", g_failures);
    return 1;
}
