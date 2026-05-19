#include "gameplay/render/static_room_model.hpp"

namespace madeline_cube {

bool StaticRoomModel::Load(const char* dfs_path) {
    if (!model_.Load(dfs_path)) return false;
    model_.UpdateMatrix({0.0f, 0.0f, 0.0f}, 10.0f, 0.0f);
    return true;
}

void StaticRoomModel::Free() {
    model_.Free();
}

void StaticRoomModel::Draw() const {
    model_.Draw();
}

}  // namespace madeline_cube
