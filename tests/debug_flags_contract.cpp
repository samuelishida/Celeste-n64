// Host test for the debug-logging gate (Pattern A: header-only, no N64 deps).
// Asserts `kVerboseFrameLogging` defaults to false (release behavior) so the
// per-frame debugf spam + 60-frame telemetry burst are suppressed by default
// (Inc 1 / D5 of the N64 perf fixup). Host tests never depend on the debug
// prints, so the flag staying off is a hard contract.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/debug_flags_contract.cpp
#include <cstdio>

#include "gameplay/debug_flags.hpp"

using namespace madeline_cube;

int main() {
    if (kVerboseFrameLogging) {
        std::fprintf(stderr, "FAIL: kVerboseFrameLogging must default to false\n");
        return 1;
    }
    if (kDebugPhysicsAutoplay) {
        std::fprintf(stderr, "FAIL: kDebugPhysicsAutoplay must default to false\n");
        return 1;
    }
    if (kDebugAutoWalk) {
        std::fprintf(stderr, "FAIL: kDebugAutoWalk must default to false\n");
        return 1;
    }
    if (kDebugInputRecording) {
        std::fprintf(stderr, "FAIL: kDebugInputRecording must default to false\n");
        return 1;
    }
    if (kEnableCellReveal) {
        std::fprintf(stderr, "FAIL: cell terrain must not scale during async load\n");
        return 1;
    }
    // Backface culling must stay OFF by default: the og-baked Forsaken City
    // map loses floor textures with culling enabled (device-verified fix,
    // 2026-08-24 backcull baselines). Flip this contract + the flag together
    // only after every baked surface renders correctly under culling.
    if (kEnableBackCull) {
        std::fprintf(stderr, "FAIL: kEnableBackCull must default to false\n");
        return 1;
    }
    if (kEnableDistantTier) {
        std::fprintf(stderr, "FAIL: stable profile must keep unqualified horizon split disabled\n");
        return 1;
    }
    if (kEnablePlayerHair) {
        std::fprintf(stderr, "FAIL: stable profile must keep unqualified procedural hair disabled\n");
        return 1;
    }
    std::printf("debug_flags_contract: all checks passed\n");
    return 0;
}
