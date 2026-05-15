#pragma once

#include <cstdint>

namespace madeline_cube {

// Lightweight debug overlay that prints live counters.
// On N64 this uses rdpq_text; on host it uses printf.
struct DebugCounters {
    float frame_time_ms = 0.0f;
    uint32_t memory_used = 0;
    uint32_t memory_free = 0;
    int active_scene_id = -1;
    int actor_count = 0;
};

class DebugHUD {
public:
    void Update(const DebugCounters& counters);
    void Render() const;

private:
    DebugCounters counters_;
};

}  // namespace madeline_cube
