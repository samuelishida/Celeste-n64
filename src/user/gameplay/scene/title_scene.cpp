#include "gameplay/scene/title_scene.hpp"

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_font.h>
#include <rdpq_text.h>
#include <rdpq_rect.h>

namespace madeline_cube {

namespace {

static constexpr uint8_t kFontId = 2;

struct MapEntry {
    const char* display_name;
    const char* lvl_path;
    const char* level_name;
};

static constexpr MapEntry kMaps[] = {
    {"1-1",        "rom:/lvl/1-1.lvl",         "1-1"},
    {"first-room", "rom:/lvl/first-room.lvl",   "first-room"},
};
static constexpr int kMapCount = static_cast<int>(sizeof(kMaps) / sizeof(kMaps[0]));

}  // namespace

TitleScene::TitleScene(GameplayScene* gameplay) : gameplay_(gameplay) {}

void TitleScene::Init() {
    font_ = rdpq_font_load("rom:/fnt/Renogare.font64");
    if (font_) rdpq_text_register_font(kFontId, font_);
}

void TitleScene::Shutdown() {
    if (font_) {
        rdpq_font_free(font_);
        font_ = nullptr;
    }
}

void TitleScene::Update(float /*delta_seconds*/) {
    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    const bool up   = pressed.d_up   || pressed.c_up;
    const bool down = pressed.d_down || pressed.c_down;
    const bool a    = pressed.a;

    if (up)   selected_ = (selected_ - 1 + kMapCount) % kMapCount;
    if (down) selected_ = (selected_ + 1) % kMapCount;

    if (a && gameplay_) {
        gameplay_->SetLevel(kMaps[selected_].lvl_path,
                            kMaps[selected_].level_name);
        RequestTransition(1);
    }
}

void TitleScene::Render() {
    surface_t* fb = display_get();
    rdpq_attach_clear(fb, nullptr);

    // Background — deep navy
    rdpq_set_mode_fill(RGBA32(15, 14, 38, 255));
    rdpq_fill_rectangle(0, 0, 320, 240);

    if (font_) {
        // Title
        rdpq_text_print(nullptr, kFontId, 60, 70, "CELESTE 64 DEMAKE");

        // Map list
        for (int i = 0; i < kMapCount; ++i) {
            const int y = 130 + i * 28;
            if (i == selected_) {
                rdpq_set_mode_fill(RGBA32(220, 80, 120, 255));
                rdpq_fill_rectangle(50, y - 16, 270, y + 8);
            }
            rdpq_text_print(nullptr, kFontId, 60, y, kMaps[i].display_name);
        }

        // Footer
        rdpq_text_print(nullptr, kFontId, 60, 220, "A: Play  D-Pad: Select");
    }

    rdpq_detach_show();
}

}  // namespace madeline_cube
