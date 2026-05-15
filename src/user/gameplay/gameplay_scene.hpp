#pragma once

#include "scene.hpp"

namespace madeline_cube {

// Encapsulates the current gameplay loop (player, island, collectible, camera).
class GameplayScene : public Scene {
public:
    void Init() override;
    void Shutdown() override;
    void Update(float delta_seconds) override;
    void Render() override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace madeline_cube
