#pragma once
namespace madeline_cube {

bool OnInterval(float elapsed, float delta, float interval, float offset = 0.0f);

struct FixedStepAccumulator {
    static constexpr float kTickDt          = 1.0f / 60.0f;
    static constexpr int   kMaxTicksPerFrame = 5;
    // Keep short renderer/input hitches queued for catch-up, but bound backlog
    // after app suspension or very long stalls so resuming cannot freeze play.
    static constexpr int   kMaxAccumulatedTicks = 10;

    float accumulator = 0.0f;
    float last_discarded_seconds = 0.0f;

    // Consumes dt, returns tick count for this frame (capped to kMaxTicksPerFrame).
    // Remaining whole ticks stay queued for later frames; only time beyond the
    // bounded accumulator is discarded and exposed in last_discarded_seconds.
    int BeginFrame(float dt);

    int PendingTicks() const {
        return static_cast<int>(accumulator / kTickDt);
    }

    // Fractional carry ∈ [0, 1] for render interpolation.
    // Clamped to 1.0 while simulation is catching up or carry >= kTickDt.
    float Alpha() const {
        float a = accumulator / kTickDt;
        return a > 1.0f ? 1.0f : a;
    }
};

}
