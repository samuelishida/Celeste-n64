// Host test for debug visualization helpers (Pattern A: header-only, no N64
// deps). Asserts `DebugColorForLod`/`DebugColorForPass` are distinct and that
// `TileBoundaryCorners` produces a closed square.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/debug_visualization_contract.cpp
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/debug_visualization.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // LOD colors are distinct across the supported levels.
    {
        const uint32_t c0 = DebugColorForLod(0);
        const uint32_t c1 = DebugColorForLod(1);
        const uint32_t c2 = DebugColorForLod(2);
        const uint32_t c3 = DebugColorForLod(3);
        expect(c0 != c1 && c1 != c2 && c2 != c3 && c0 != c3,
               "LOD colors distinct");
    }

    // Pass colors are distinct for the four passes.
    {
        const uint32_t d = DebugColorForPass("distant");
        const uint32_t l = DebugColorForPass("low_priority");
        const uint32_t h = DebugColorForPass("high_priority");
        const uint32_t s = DebugColorForPass("skybox");
        expect(d != l && l != h && h != s && d != h && d != s && l != s,
               "pass colors distinct");
    }

    // TileBoundaryCorners forms a closed square: 4 corners, each at the
    // expected half-extent, and the loop closes (last -> first).
    {
        const Vec3 origin = {100.0f, 0.0f, -50.0f};
        const float size = 240.0f;
        const float y = 0.0f;
        Vec3 corners[4];
        TileBoundaryCorners(origin, size, y, corners);

        const float half = size * 0.5f;
        // Each corner must be at (origin.x ± half, y, origin.z ± half).
        for (int i = 0; i < 4; ++i) {
            const float dx = corners[i].x - origin.x;
            const float dz = corners[i].z - origin.z;
            expect((dx == half || dx == -half) && (dz == half || dz == -half),
                   "corner at expected half-extent");
            expect(corners[i].y == y, "corner at expected y");
        }
        // The four corners are distinct.
        expect(corners[0].x != corners[1].x || corners[0].z != corners[1].z,
               "corner 0 != corner 1");
        expect(corners[1].x != corners[2].x || corners[1].z != corners[2].z,
               "corner 1 != corner 2");
        expect(corners[2].x != corners[3].x || corners[2].z != corners[3].z,
               "corner 2 != corner 3");
        // Closed square: the loop closes via the edge corner 3 -> corner 0,
        // which shares the x coordinate (a vertical edge in z).
        expect(corners[3].x == corners[0].x,
               "loop closes (corner 3 shares x with corner 0)");
    }

    if (failures == 0) {
        std::printf("debug_visualization_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "debug_visualization_contract: %d failures\n", failures);
    return 1;
}
