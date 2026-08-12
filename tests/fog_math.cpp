// Host test for fog parameter math (Pattern A: header-only, no N64 deps).
// Asserts `MakeFog` clamps `min_dist` correctly (arch.md §27) and that
// `ValidateFogRange` rejects an inverted range.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/fog_math.cpp
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/fog_math.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // Normal range: min < max, both >= 0.
    {
        const FogParams f = MakeFog(300.0f, 1200.0f, {120.0f, 150.0f, 180.0f});
        expect(f.enabled, "fog enabled by default");
        expect(f.min == 300.0f, "min preserved when within clamp");
        expect(f.max == 1200.0f, "max preserved");
        expect(ValidateFogRange(f), "valid range accepted");
    }

    // Clamp: an excessively high min is clamped to kFogMaxMinDistance.
    {
        const FogParams f = MakeFog(5000.0f, 6000.0f, {120.0f, 150.0f, 180.0f});
        expect(f.min == kFogMaxMinDistance, "excessive min clamped to max");
        // After clamping, min (4000) < max (6000) still holds.
        expect(ValidateFogRange(f), "clamped range still valid");
    }

    // Inverted range: min > max is rejected.
    {
        const FogParams f = MakeFog(1200.0f, 300.0f, {120.0f, 150.0f, 180.0f});
        expect(!ValidateFogRange(f), "inverted range rejected");
    }

    // Zero min is valid (fog starts at the camera).
    {
        const FogParams f = MakeFog(0.0f, 500.0f, {120.0f, 150.0f, 180.0f});
        expect(ValidateFogRange(f), "zero min valid");
    }

    // Negative min is invalid.
    {
        const FogParams f = MakeFog(-10.0f, 500.0f, {120.0f, 150.0f, 180.0f});
        expect(!ValidateFogRange(f), "negative min rejected");
    }

    if (failures == 0) {
        std::printf("fog_math: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "fog_math: %d failures\n", failures);
    return 1;
}
