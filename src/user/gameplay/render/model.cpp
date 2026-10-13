#include "gameplay/render/model.hpp"

#include <cstdio>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <rdpq_sprite.h>

namespace madeline_cube {

bool StaticModel::Load(const char* dfs_path, const char* texture_path) {
    Free();
    model_path_ = dfs_path;
    texture_path_ = texture_path;
    last_error_ = nullptr;

    // Probe DFS paths first. tiny3d/sprite loaders assert on missing files.
    FILE* model_file = dfs_path ? std::fopen(dfs_path, "rb") : nullptr;
    if (!model_file) {
        SetFailure("model path missing");
        debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
               dfs_path ? dfs_path : "(null)",
               texture_path ? texture_path : "(none)", LastError());
        return false;
    }
    std::fclose(model_file);
    if (texture_path) {
        FILE* texture_file = std::fopen(texture_path, "rb");
        if (!texture_file) {
            SetFailure("texture path missing");
            debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
                   dfs_path, texture_path, LastError());
            return false;
        }
        std::fclose(texture_file);
    }

    model_ = t3d_model_load(dfs_path);
    if (!model_) {
        SetFailure("model decode failed");
        debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
               dfs_path, texture_path ? texture_path : "(none)", LastError());
        return false;
    }
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    if (!matrix_fp_) {
        SetFailure("matrix allocation failed");
        debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
               dfs_path, texture_path ? texture_path : "(none)", LastError());
        Free();
        return false;
    }
    if (texture_path) {
        texture_ = sprite_load(texture_path);
        if (!texture_) {
            SetFailure("texture decode failed");
            debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
                   dfs_path, texture_path, LastError());
            Free();
            return false;
        }
        draw_conf_.userData = this;
        draw_conf_.dynTextureCb = &StaticModel::DynamicTexture;
    }
    // Record model draw in display list
    rspq_block_begin();
    if (texture_) t3d_model_draw_custom(model_, draw_conf_);
    else t3d_model_draw(model_);
    dpl_ = rspq_block_end();
    if (!dpl_) {
        SetFailure("display-list allocation failed");
        debugf("[model] load FAILED: model=%s texture=%s reason=%s\n",
               dfs_path, texture_path ? texture_path : "(none)", LastError());
        Free();
        return false;
    }

    debugf("[model] loaded: model=%s texture=%s\n",
           dfs_path, texture_path ? texture_path : "(none)");
    return true;
}

void StaticModel::Free() {
    if (dpl_) {
        rspq_block_free(dpl_);
        dpl_ = nullptr;
    }
    if (model_) {
        t3d_model_free(model_);
        model_ = nullptr;
    }
    if (texture_) {
        sprite_free(texture_);
        texture_ = nullptr;
    }
    draw_conf_ = {};
    if (matrix_fp_) {
        free_uncached(matrix_fp_);
        matrix_fp_ = nullptr;
    }
}

void StaticModel::UpdateMatrix(const Vec3& pos, float scale, float yaw_rad) {
    if (!model_ || !matrix_fp_) return;

    T3DMat4 matrix;
    const float rot[3] = {0.0f, yaw_rad, 0.0f};
    const float scl[3] = {scale, scale, scale};
    const float p[3] = {pos.x, pos.y, pos.z};
    t3d_mat4_from_srt_euler(&matrix, scl, rot, p);
    t3d_mat4_to_fixed(matrix_fp_, &matrix);
}

void StaticModel::Draw() const {
    if (!dpl_ || !matrix_fp_) return;
    t3d_matrix_push(matrix_fp_);
    rspq_block_run(dpl_);
    t3d_matrix_pop(1);
}

void StaticModel::DynamicTexture(void* user_data, const T3DMaterial*,
                                 rdpq_texparms_t* tile_params, rdpq_tile_t tile) {
    auto* self = static_cast<StaticModel*>(user_data);
    if (!self || !self->texture_) return;
    rdpq_sprite_upload(tile, self->texture_, tile_params);
}

}  // namespace madeline_cube
