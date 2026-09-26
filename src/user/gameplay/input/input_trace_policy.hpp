#pragma once

#include <cstdint>

namespace madeline_cube {

// Raw N64 stick range is roughly +/-80. Bucket small changes so analog noise
// does not turn this diagnostic path back into per-tick USB serial logging.
inline int InputTraceStickBucket(int raw_value) {
    return raw_value / 8;
}

// Contact normals change continuously while traversing ramps. Quantize each
// component to 1/16 so the trace can emit meaningful slope/corner transitions
// without turning every tiny normal delta into USB-serial spam.
inline int InputTraceNormalBucket(float component) {
    return static_cast<int>(component * 16.0f);
}

// Live traces run through USB serial. Sample every 6 fixed ticks (~10 Hz), but
// always emit input/state/contact transitions so short actions are not hidden.
inline constexpr uint32_t kInputTraceSampleIntervalTicks = 6;
inline constexpr float kInputTraceSlowFrameSeconds = 0.040f;

inline bool ShouldEmitInputTrace(uint32_t sequence, bool input_changed,
                                 bool state_changed, bool contact_changed,
                                 bool timing_anomaly = false) {
    return sequence % kInputTraceSampleIntervalTicks == 0 || input_changed ||
           state_changed || contact_changed || timing_anomaly;
}

}  // namespace madeline_cube
