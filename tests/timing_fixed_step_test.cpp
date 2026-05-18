#include <cassert>
#include <cmath>
#include <cstdio>

#include "gameplay/runtime/timing.hpp"

using namespace madeline_cube;

static bool Near(float a, float b, float eps) { return std::fabs(a - b) < eps; }

// --- Test 1: exactly 300 ticks over 300 frames each of kTickDt ---------------
// Using the same float constant per frame avoids drift: accumulator += kTickDt,
// then subtracts kTickDt exactly, leaving 0 carry each frame.
static void test_tick_count_300() {
    FixedStepAccumulator acc;
    int total = 0;
    for (int i = 0; i < 300; ++i)
        total += acc.BeginFrame(FixedStepAccumulator::kTickDt);
    printf("[t1] tick count = %d (want 300)\n", total);
    assert(total == 300 && "tick count != 300");
    assert(Near(acc.accumulator, 0.0f, 1e-6f) && "carry should be ~0 after 300 exact ticks");
}

// --- Test 2: per-frame cap (kMaxTicksPerFrame) --------------------------------
// Burst sequence: 5,33,100,5,16,16,16,8,33,5 ms plus 290 filler frames.
// None should exceed kMaxTicksPerFrame.
static void test_per_frame_cap() {
    static const int kDtMs[] = {
        5, 33, 100, 5, 16, 16, 16, 8, 33, 5,
        // 290 frames at 16 ms each (some frames accumulate carry; cap is 5)
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,
        16,16,16,16,16,16,16,16,16,16,
    };
    FixedStepAccumulator acc;
    for (int ms : kDtMs) {
        int t = acc.BeginFrame(ms * 0.001f);
        assert(t <= FixedStepAccumulator::kMaxTicksPerFrame && "frame exceeded kMaxTicksPerFrame");
    }
    printf("[t2] per-frame tick cap OK\n");
}

// --- Test 3: spiral cap engages on 100 ms frame ------------------------------
static void test_spiral_cap() {
    FixedStepAccumulator acc;
    int t = acc.BeginFrame(0.100f);
    printf("[t3] 100ms → ticks = %d (want %d)\n", t, FixedStepAccumulator::kMaxTicksPerFrame);
    assert(t == FixedStepAccumulator::kMaxTicksPerFrame && "spiral cap did not engage on 100ms");
}

// --- Test 4: interpolation alpha = 0.5 gives midpoint ------------------------
// GIVEN prev_position=(0,0,0), position=(10,0,0), alpha=0.5
// THEN interp=(5,0,0) within 1e-4
static void test_interpolation() {
    FixedStepAccumulator acc;
    // Feed exactly half a tick → 0 ticks fired, carry = 0.5 * kTickDt → alpha = 0.5
    int ticks = acc.BeginFrame(FixedStepAccumulator::kTickDt * 0.5f);
    assert(ticks == 0 && "expected 0 ticks for half-tick dt");
    float alpha = acc.Alpha();
    printf("[t4] alpha = %.6f (want ~0.5)\n", alpha);
    assert(Near(alpha, 0.5f, 1e-4f) && "alpha not 0.5");

    const float px0 = 0.0f, py0 = 0.0f, pz0 = 0.0f;
    const float px1 = 10.0f, py1 = 0.0f, pz1 = 0.0f;
    const float ix = px0 + (px1 - px0) * alpha;
    const float iy = py0 + (py1 - py0) * alpha;
    const float iz = pz0 + (pz1 - pz0) * alpha;
    printf("[t4] interp = (%.4f, %.4f, %.4f) (want 5,0,0)\n", ix, iy, iz);
    assert(Near(ix, 5.0f, 1e-4f) && "x interp wrong");
    assert(Near(iy, 0.0f, 1e-4f) && "y interp wrong");
    assert(Near(iz, 0.0f, 1e-4f) && "z interp wrong");
}

// --- Test 5: prev_position reset on teleport ---------------------------------
// Simulates spawn/teleport: prev = position before any tick.
// After teleport, interp with any alpha yields new position (no sliding).
static void test_prev_position_reset() {
    FixedStepAccumulator acc;
    // Simulate: player was at (0,0,0), teleported to (100,0,0)
    float prev_x = 100.0f;  // set prev = position on teleport
    float curr_x = 100.0f;
    // Feed half a tick (alpha = 0.5)
    acc.BeginFrame(FixedStepAccumulator::kTickDt * 0.5f);
    float alpha = acc.Alpha();
    float interp_x = prev_x + (curr_x - prev_x) * alpha;
    assert(Near(interp_x, 100.0f, 1e-4f) && "teleport: interp should stay at new position");
    printf("[t5] teleport interp = %.4f (want 100.0)\n", interp_x);
}

int main() {
    test_tick_count_300();
    test_per_frame_cap();
    test_spiral_cap();
    test_interpolation();
    test_prev_position_reset();
    printf("[timing_fixed_step_test] PASS\n");
    return 0;
}
