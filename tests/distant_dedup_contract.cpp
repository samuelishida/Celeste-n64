// Host test for the distant double-free fix (Pattern A: header-only, no N64
// deps). Asserts `CollectDistinctMeshes` dedupes the directional mesh slots of
// a `DistantLodEntry` so `FreeEntries` frees each distinct mesh exactly once.
//
// The helper only compares pointers (never dereferences them), so this test
// passes FAKE pointer values — it must NOT instantiate the N64 `LvlRoomRenderer`
// on host.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_dedup_contract.cpp
#include <cstdio>

#include "gameplay/render/distant_world_renderer.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Fake mesh pointers — never dereferenced, only compared.
static LvlRoomRenderer* fake(uintptr_t v) {
    return reinterpret_cast<LvlRoomRenderer*>(v);
}

int main() {
    // 1. Shared-slot entry: all 4 slots point at the SAME mesh → 1 distinct.
    {
        DistantLodEntry en = {};
        LvlRoomRenderer* shared = fake(0x1000);
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d)
            en.meshes[d] = shared;
        LvlRoomRenderer* out[DistantLodEntry::kMaxDirMeshes] = {};
        const int n = CollectDistinctMeshes(en, out);
        expect(n == 1, "shared-slot entry yields 1 distinct mesh");
        expect(out[0] == shared, "distinct mesh is the shared pointer");
    }

    // 2. Distinct entry: all 4 slots point at DIFFERENT meshes → 4 distinct.
    {
        DistantLodEntry en = {};
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d)
            en.meshes[d] = fake(0x2000 + d);
        LvlRoomRenderer* out[DistantLodEntry::kMaxDirMeshes] = {};
        const int n = CollectDistinctMeshes(en, out);
        expect(n == 4, "distinct-slot entry yields 4 distinct meshes");
        for (int k = 0; k < n; ++k)
            expect(out[k] == fake(0x2000 + k), "distinct pointers preserved");
    }

    // 3. Mixed entry: some shared, some distinct → each distinct freed once.
    {
        DistantLodEntry en = {};
        LvlRoomRenderer* a = fake(0x3000);
        LvlRoomRenderer* b = fake(0x4000);
        en.meshes[0] = a;
        en.meshes[1] = a;  // shared with slot 0
        en.meshes[2] = b;
        en.meshes[3] = nullptr;
        LvlRoomRenderer* out[DistantLodEntry::kMaxDirMeshes] = {};
        const int n = CollectDistinctMeshes(en, out);
        expect(n == 2, "mixed entry yields 2 distinct meshes");
        expect((out[0] == a && out[1] == b) || (out[0] == b && out[1] == a),
               "mixed entry distinct pointers are a and b");
    }

    // 4. All-null entry → nothing to free.
    {
        DistantLodEntry en = {};
        LvlRoomRenderer* out[DistantLodEntry::kMaxDirMeshes] = {};
        const int n = CollectDistinctMeshes(en, out);
        expect(n == 0, "all-null entry yields 0 distinct meshes");
    }

    // 5. FreeEntries-style loop: simulate the dedupe freeing each distinct
    //    pointer once for a shared-slot entry (the double-free bug class).
    {
        DistantLodEntry en = {};
        LvlRoomRenderer* shared = fake(0x5000);
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d)
            en.meshes[d] = shared;
        LvlRoomRenderer* distinct[DistantLodEntry::kMaxDirMeshes] = {};
        const int n = CollectDistinctMeshes(en, distinct);
        int frees = 0;
        for (int k = 0; k < n; ++k) {
            if (distinct[k]) ++frees;  // Free() + delete once per distinct
        }
        expect(frees == 1, "shared-slot entry frees exactly once (no double-free)");
    }

    if (failures == 0) {
        std::printf("PASS: distant_dedup_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
