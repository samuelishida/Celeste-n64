#include "debug_hud.hpp"

#include <debug.h>
#include <rdpq.h>
#include <rdpq_font.h>
#include <rdpq_text.h>

namespace madeline_cube {

static constexpr uint8_t kFontId = 1;

void DebugHUD::Init() {
    font_ = rdpq_font_load("rom:/fnt/Renogare.font64");
    if (font_) {
        rdpq_text_register_font(kFontId, font_);
        font_loaded_ = true;
        debugf("[hud] rdpq_font_load rom:/fnt/Renogare.font64 OK\n");
    } else {
        debugf("[hud] rdpq_font_load FAILED\n");
    }
}

void DebugHUD::Shutdown() {
    if (font_) {
        rdpq_font_free(font_);
        font_ = nullptr;
        font_loaded_ = false;
    }
}

void DebugHUD::Update(const DebugCounters& counters) {
    counters_ = counters;
}

void DebugHUD::Render() const {
    if (font_loaded_) {
        rdpq_text_print(NULL, kFontId, 10, 20, "Strawberries: 0/30");
    } else {
        debugf("[hud] ft=%.2fms scene=%d actors=%d\n",
               static_cast<double>(counters_.frame_time_ms),
               counters_.active_scene_id,
               counters_.actor_count);
    }
}

}  // namespace madeline_cube
