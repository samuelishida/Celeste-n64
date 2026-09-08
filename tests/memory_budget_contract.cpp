// Host test for the [memory] bucket report (memory-opt Inc 1).
//
// Asserts:
//   (a) the bucket table is bounded at MemorySnapshot::kMaxBuckets (a 9th
//       AddBucket is ignored);
//   (b) BucketBytes() sums every live bucket;
//   (c) FormatLine keeps the legacy parse-compatible prefix
//       "[memory] total=%u used=%u free=%u" and appends " name=bytes" tokens;
//   (d) bucket-sum <= arena `total_bytes` for a realistic snapshot (the budget
//       invariant the inc5 warn builds on);
//   (e) an empty bucket table emits no trailing tokens;
//   (f) a null-name bucket is skipped.
//
// Build: g++ -std=c++17 -Isrc/user tests/memory_budget_contract.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "n64/profiler.hpp"

#include "gameplay/memory_budget.hpp"  // memory-opt Inc 5

using n64::MemorySnapshot;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // (e) empty buckets: prefix only, no trailing tokens.
    {
        MemorySnapshot snap;
        snap.total_bytes = 1000;
        snap.used_bytes = 900;
        snap.free_bytes = 100;
        char line[256];
        const size_t n = snap.FormatLine(line, sizeof(line));
        expect(n == std::strlen(line), "FormatLine length matches content");
        expect(std::strncmp(line, "[memory] total=1000 used=900 free=100", 38) == 0,
               "legacy prefix preserved");
        expect(std::strstr(line, " =") == nullptr, "no bucket tokens when empty");
    }

    // (a) capped at kMaxBuckets; (b) bucket sum.
    {
        MemorySnapshot snap;
        for (int i = 0; i < MemorySnapshot::kMaxBuckets + 1; ++i) {
            char name[16];
            std::snprintf(name, sizeof(name), "b%d", i);
            snap.AddBucket(name, 64);
        }
        expect(snap.bucket_count == MemorySnapshot::kMaxBuckets, "bucket count capped");
        expect(snap.BucketBytes() == 64u * static_cast<size_t>(MemorySnapshot::kMaxBuckets),
               "bucket sum");
    }

    // (c) appended tokens parse with realistic arena numbers.
    {
        MemorySnapshot snap;
        snap.total_bytes = 2160000;  // realistic stationary arena total
        snap.used_bytes = 2150000;
        snap.free_bytes = 10000;
        snap.AddBucket("frame_arena", 40 * 1024);
        snap.AddBucket("staging", 120 * 1024);
        snap.AddBucket("res_render", 25 * 1024);
        char line[256];
        snap.FormatLine(line, sizeof(line));
        expect(std::strstr(line, "[memory] total=2160000 used=2150000 free=10000") != nullptr,
               "legacy prefix with realistic numbers");
        expect(std::strstr(line, " frame_arena=40960") != nullptr, "frame_arena token");
        expect(std::strstr(line, " staging=122880") != nullptr, "staging token");
        expect(std::strstr(line, " res_render=25600") != nullptr, "res_render token");
    }

    // (d) bucket-sum <= arena total.
    {
        MemorySnapshot snap;
        snap.total_bytes = 2160000;
        snap.used_bytes = 2150000;
        snap.free_bytes = 10000;
        snap.AddBucket("frame_arena", 64 * 1024);
        snap.AddBucket("staging", 120 * 1024);
        snap.AddBucket("coll_mesh", 1500000);
        snap.AddBucket("mappack", 60 * 1024);
        snap.AddBucket("res_render", 200 * 1024);
        expect(snap.BucketBytes() <= snap.total_bytes, "bucket sum <= arena total");
    }

    // (f) null-name bucket is skipped.
    {
        MemorySnapshot snap;
        snap.AddBucket(nullptr, 123);
        expect(snap.bucket_count == 0, "null-name bucket skipped");
    }

    // memory-opt Inc 5: BudgetTracker — over-cap buckets emit [budget] WARN
    // lines; under-cap emit nothing; unmatched caps are skipped; the warn
    // line shape is stable ("[budget] WARN <name>=<bytes> cap=<cap>").
    {
        madeline_cube::BudgetTracker budget;
        budget.RegisterCap({"frame_arena", 64 * 1024});
        budget.RegisterCap({"staging", 128 * 1024});
        budget.RegisterCap({"coll_mesh", 700 * 1024});
        budget.RegisterCap({"unmatched_cap", 1});  // no matching bucket

        MemorySnapshot snap;
        snap.total_bytes = 2160000;
        snap.used_bytes = 2150000;
        snap.free_bytes = 10000;
        snap.AddBucket("frame_arena", 40 * 1024);   // under cap
        snap.AddBucket("staging", 200 * 1024);      // over cap
        snap.AddBucket("coll_mesh", 634168);        // under cap

        char line[512];
        const int warns = budget.Check(snap, line, sizeof(line));
        expect(warns == 1, "exactly one over-cap warn");
        expect(std::strstr(line, "[budget] WARN staging=204800 cap=131072") != nullptr,
               "warn line shape stable");
        expect(std::strstr(line, "frame_arena") == nullptr, "under-cap bucket silent");
        expect(std::strstr(line, "unmatched_cap") == nullptr, "unmatched cap skipped");

        // All under-cap: no warns, empty output.
        MemorySnapshot ok;
        ok.total_bytes = 2160000;
        ok.used_bytes = 2150000;
        ok.free_bytes = 10000;
        ok.AddBucket("frame_arena", 40 * 1024);
        ok.AddBucket("staging", 120 * 1024);
        ok.AddBucket("coll_mesh", 634168);
        char ok_line[512];
        expect(budget.Check(ok, ok_line, sizeof(ok_line)) == 0, "healthy run: no warns");
        expect(ok_line[0] == '\0', "healthy run: empty output");
    }

    if (failures == 0) {
        std::printf("PASS: memory_budget_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
