#pragma once

#include <cstdint>

struct rdpq_font_s;

namespace madeline_cube {

struct DebugCounters {
    float frame_time_ms = 0.0f;
    uint32_t memory_used = 0;
    uint32_t memory_free = 0;
    int active_scene_id = -1;
    int actor_count = 0;
};

class DebugHUD {
public:
    void Init();
    void Shutdown();
    void Update(const DebugCounters& counters);
    void Render() const;

private:
    DebugCounters counters_;
    rdpq_font_s* font_ = nullptr;
    bool font_loaded_ = false;
};

}  // namespace madeline_cube
