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
    std::printf("debug_flags_contract: all checks passed\n");
    return 0;
}
