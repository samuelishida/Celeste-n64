#include "gameplay/input/input_trace_policy.hpp"

#include <cstdio>

using madeline_cube::ShouldEmitInputTrace;
using madeline_cube::InputTraceStickBucket;
using madeline_cube::InputTraceNormalBucket;

int main() {
    if (!ShouldEmitInputTrace(0, false, false, false) ||
        ShouldEmitInputTrace(1, false, false, false) ||
        ShouldEmitInputTrace(5, false, false, false) ||
        !ShouldEmitInputTrace(6, false, false, false) ||
        !ShouldEmitInputTrace(7, true, false, false) ||
        !ShouldEmitInputTrace(8, false, true, false) ||
        !ShouldEmitInputTrace(9, false, false, true) ||
        !ShouldEmitInputTrace(5, false, false, false, true)) {
        std::fprintf(stderr, "input trace sampling dropped periodic or event sample\n");
        return 1;
    }

    if (InputTraceStickBucket(0) != InputTraceStickBucket(7) ||
        InputTraceStickBucket(7) == InputTraceStickBucket(8) ||
        InputTraceStickBucket(0) != InputTraceStickBucket(-7) ||
        InputTraceStickBucket(-7) == InputTraceStickBucket(-8)) {
        std::fprintf(stderr, "input trace stick buckets do not suppress small analog noise\n");
        return 1;
    }

    if (InputTraceNormalBucket(0.93f) != InputTraceNormalBucket(0.92f) ||
        InputTraceNormalBucket(0.92f) == InputTraceNormalBucket(0.87f) ||
        InputTraceNormalBucket(-0.93f) != InputTraceNormalBucket(-0.92f) ||
        InputTraceNormalBucket(-0.92f) == InputTraceNormalBucket(-0.87f)) {
        std::fprintf(stderr, "input trace normal buckets do not bound contact-change logging\n");
        return 1;
    }

    std::puts("PASS: input trace is bounded and preserves input/state/contact events");
    return 0;
}
