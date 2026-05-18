#pragma once
namespace madeline_cube {

bool OnInterval(float elapsed, float delta, float interval, float offset = 0.0f);

struct FixedStepAccumulator {
    static constexpr float kTickDt          = 1.0f / 60.0f;
    static constexpr int   kMaxTicksPerFrame = 5;

    float accumulator = 0.0f;

    // Consumes dt, returns tick count for this frame (capped to kMaxTicksPerFrame).
    int BeginFrame(float dt);

    // Fractional carry ∈ [0, 1] for render interpolation.
    // Clamped to 1.0 when spiral cap leaves carry >= kTickDt.
    float Alpha() const {
        float a = accumulator / kTickDt;
        return a > 1.0f ? 1.0f : a;
    }
};

}
