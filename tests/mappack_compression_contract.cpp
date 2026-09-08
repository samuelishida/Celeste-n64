// Host test for the memory-opt Inc 2 load-path plumbing.
//
// Asserts:
//   (a) kEnableCompressedCells is FALSE on host builds — host tests exercise
//       the plain-fopen path (the asset layer is N64-only);
//   (b) the refactored LoadMapPackV2 still parses the baked staging manifest
//       through the fopen path (the opener plumbing did not break the plain
//       path that host tests and the flag-off ROM use).
//
// Build (Pattern C — links mappack_loader.cpp):
//   g++ -std=c++17 -Isrc/user tests/mappack_compression_contract.cpp \
//       src/user/gameplay/world/mappack_loader.cpp -- <staging manifest>
#include <cstdio>
#include <cstdlib>

#include "gameplay/debug_flags.hpp"
#include "gameplay/world/mappack_loader.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main(int argc, char** argv) {
    // (a) host builds must take the plain-fopen path.
    expect(!kEnableCompressedCells,
           "kEnableCompressedCells is false on host builds");

    // (b) the refactored loader parses the staging manifest via fopen.
    if (argc < 2) {
        std::fprintf(stderr, "SKIP: no staging manifest argument\n");
        return 0;
    }
    MapSpecV2 spec;
    const bool ok = LoadMapPackV2(argv[1], spec);
    expect(ok, "LoadMapPackV2 parses the staging manifest (fopen path)");
    if (ok) {
        expect(spec.room_count > 0, "manifest has rooms");
        expect(spec.FindStartSpawn() != nullptr, "manifest has a Start spawn");
    }

    if (failures == 0) {
        std::printf("PASS: mappack_compression_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
