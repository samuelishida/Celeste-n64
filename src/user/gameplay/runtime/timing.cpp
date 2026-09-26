#include "gameplay/runtime/timing.hpp"
#include <algorithm>
#include <cmath>
namespace madeline_cube {

bool OnInterval(float elapsed, float delta, float interval, float offset) {
    if (interval <= 0.0f || delta <= 0.0f) return false;
    const float now = std::floor((elapsed - offset) / interval);
    const float before = std::floor((elapsed - delta - offset) / interval);
    return now > before;
}

int FixedStepAccumulator::BeginFrame(float dt) {
    constexpr float kMaxAccumulator = kMaxAccumulatedTicks * kTickDt;
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    const float available = accumulator + dt;
    last_discarded_seconds = std::max(0.0f, available - kMaxAccumulator);
    accumulator = std::min(available, kMaxAccumulator);
    int ticks = 0;
    while (accumulator >= kTickDt && ticks < kMaxTicksPerFrame) {
        accumulator -= kTickDt;
        ++ticks;
    }
    return ticks;
}

}
