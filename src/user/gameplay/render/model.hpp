#pragma once

#include <t3d/t3dmodel.h>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

class StaticModel {
public:
    bool Load(const char* dfs_path);
    void Free();
    void UpdateMatrix(const Vec3& pos, float scale, float yaw_rad);
    void Draw() const;
    bool IsLoaded() const { return model_ != nullptr; }

private:
    T3DModel* model_ = nullptr;
    T3DMat4FP* matrix_fp_ = nullptr;
    rspq_block_t* dpl_ = nullptr;
};

}  // namespace madeline_cube
