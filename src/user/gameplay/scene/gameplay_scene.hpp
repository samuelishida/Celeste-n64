#pragma once

#include "gameplay/scene/scene.hpp"

namespace madeline_cube {

// Encapsulates the current gameplay loop (player, island, collectible, camera).
class GameplayScene : public Scene {
public:
    // Called by TitleScene before transitioning to set which level to load.
    void SetLevel(const char* lvl_path, const char* level_name);

    void Init() override;
    void Shutdown() override;
    void Update(float delta_seconds) override;
    void Render() override;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    const char* lvl_path_  = "rom:/lvl/1-1.lvl";
    const char* level_name_ = "1-1";
};

}  // namespace madeline_cube
