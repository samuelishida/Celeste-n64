#pragma once

#include "gameplay/scene/scene.hpp"
#include "gameplay/scene/gameplay_scene.hpp"

struct rdpq_font_s;

namespace madeline_cube {

class TitleScene : public Scene {
public:
    explicit TitleScene(GameplayScene* gameplay);

    void Init() override;
    void Shutdown() override;
    void Update(float delta_seconds) override;
    void Render() override;

private:
    GameplayScene* gameplay_;
    rdpq_font_s* font_ = nullptr;

    int selected_ = 0;

    bool prev_a_    = false;
    bool prev_up_   = false;
    bool prev_down_ = false;
};

}  // namespace madeline_cube
