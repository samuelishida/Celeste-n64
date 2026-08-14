// Host test for the dynamic distant streaming (Pattern A: header-only, no N64
// deps). Asserts (Inc 6 / D5):
//   - `ChebyshevCellDistance` computes the Chebyshev distance between cells;
//   - the D5 stream-radius invariant holds for the real constants: radius ≥
//     ceil(fog_complete/cell + 0.5), so a cell is fully fogged before it can
//     become drawable (no pop-in at the radius edge);
//   - `MinStreamRadiusForFog` returns the correct minimum radius.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_streaming_contract.cpp
#include <cmath>
#include <cstdio>

#include "gameplay/render/lod_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // 1. ChebyshevCellDistance.
    {
        expect(ChebyshevCellDistance(0, 0, 0, 0) == 0, "same cell dist 0");
        expect(ChebyshevCellDistance(0, 0, 1, 0) == 1, "axis neighbor dist 1");
        expect(ChebyshevCellDistance(0, 0, 1, 1) == 1, "diagonal neighbor dist 1");
        expect(ChebyshevCellDistance(0, 0, 3, 2) == 3, "max-axis dist 3");
        expect(ChebyshevCellDistance(3, 2, 0, 0) == 3, "symmetric dist 3");
    }

    // 2. D5 invariant for the real constants. Cell = 240u; fog completes at
    //    0.9 × sqrt(kDistantMaxDist2) ≈ 1197u. The stream radius (6) must be ≥
    //    ceil(fog_complete/cell + 0.5).
    {
        const float cell = 240.0f;
        const float fog_complete = 0.9f * std::sqrt(kDistantMaxDist2);
        const int min_radius = MinStreamRadiusForFog(cell, fog_complete);
        // Worst-case load distance = radius × cell − half-cell.
        const float worst_load = 6.0f * cell - cell * 0.5f;
        expect(worst_load >= fog_complete,
               "worst-case load distance (radius 6) ≥ fog-complete distance");
        expect(min_radius <= 6, "min radius for fog ≤ the stream radius (6)");
        expect(min_radius >= 1, "min radius is positive");
        // The invariant formula: radius ≥ ceil(fog_complete/cell + 0.5).
        const float r = (fog_complete + cell * 0.5f) / cell;
        expect(min_radius == static_cast<int>(std::ceil(r)),
               "MinStreamRadiusForFog matches ceil(fog_complete/cell + 0.5)");
    }

    // 3. A larger map scales the radius automatically (the D5 invariant).
    {
        const float cell = 240.0f;
        const float fog_complete = 2000.0f;  // a map with a longer fog ramp
        const int min_radius = MinStreamRadiusForFog(cell, fog_complete);
        expect(min_radius > 6, "larger fog-complete distance needs a larger radius");
        const float worst_load = min_radius * cell - cell * 0.5f;
        expect(worst_load >= fog_complete,
               "scaled radius still satisfies the invariant");
    }

    if (failures == 0) {
        std::printf("PASS: distant_streaming_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
