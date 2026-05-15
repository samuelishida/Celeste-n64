#include "debug_hud.hpp"

#include <debug.h>
#include <rdpq.h>
#include <rdpq_text.h>

namespace madeline_cube {

void DebugHUD::Update(const DebugCounters& counters) {
    counters_ = counters;
}

void DebugHUD::Render() const {
    // Use rdpq_text for on-screen debug output.
    // This requires a font to be registered; for now we use debugf
    // as a fallback if no font is loaded.
    debugf("[hud] ft=%.2fms mem=%u/%u scene=%d actors=%d\n",
           static_cast<double>(counters_.frame_time_ms),
           static_cast<unsigned int>(counters_.memory_used),
           static_cast<unsigned int>(counters_.memory_free),
           counters_.active_scene_id,
           counters_.actor_count);
}

}  // namespace madeline_cube
