#pragma once

#include "gameplay/scene/scene.hpp"

namespace madeline_cube {

// Encapsulates the current gameplay loop (player, island, collectible, camera).
class GameplayScene : public Scene {
public:
    // Called by TitleScene before transitioning to set which level to load.
    void SetLevel(const char* lvl_path, const char* level_name);

    // Set the map-pack manifest to boot as a multi-room map. When set
    // (before Init), the scene boots the map-pack's start room and streams
    // chunks across boundaries. When null, the legacy single-room path
    // (SetLevel) is used.
    void SetMapPack(const char* mappack_path);

    void Init() override;
    void Shutdown() override;
    void Update(float delta_seconds) override;
    void Render() override;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    const char* lvl_path_  = "rom:/lvl/1-1.lvl";
    const char* level_name_ = "1-1";
    const char* mappack_path_pending_ = nullptr;  // set by SetMapPack before Init
};

}  // namespace madeline_cube
