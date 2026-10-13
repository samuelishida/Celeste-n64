#pragma once

#include <t3d/t3dmodel.h>
#include <sprite.h>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

class StaticModel {
public:
    bool Load(const char* dfs_path, const char* texture_path = nullptr);
    void Free();
    void UpdateMatrix(const Vec3& pos, float scale, float yaw_rad);
    void Draw() const;
    bool IsLoaded() const { return model_ != nullptr; }
    const char* ModelPath() const { return model_path_; }
    const char* TexturePath() const { return texture_path_; }
    const char* LastError() const { return last_error_; }

private:
    void SetFailure(const char* reason) { last_error_ = reason; }
    static void DynamicTexture(void* user_data, const T3DMaterial* material,
                               rdpq_texparms_t* tile_params, rdpq_tile_t tile);
    T3DModel* model_ = nullptr;
    T3DMat4FP* matrix_fp_ = nullptr;
    rspq_block_t* dpl_ = nullptr;
    sprite_t* texture_ = nullptr;
    T3DModelDrawConf draw_conf_ = {};
    const char* model_path_ = nullptr;
    const char* texture_path_ = nullptr;
    const char* last_error_ = nullptr;
};

}  // namespace madeline_cube
