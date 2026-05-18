#pragma once

#include "gameplay/render/model.hpp"

namespace madeline_cube {

// Native tiny3d render path for static room geometry. Room collision/entities
// stay in LVL1; this wrapper owns only the visible .t3dm artifact.
class StaticRoomModel {
public:
    bool Load(const char* dfs_path);
    void Free();
    void Draw() const;
    bool IsLoaded() const { return model_.IsLoaded(); }

private:
    StaticModel model_;
};

}  // namespace madeline_cube
