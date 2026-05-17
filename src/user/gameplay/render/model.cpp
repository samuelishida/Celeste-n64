#include "gameplay/render/model.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

namespace madeline_cube {

bool StaticModel::Load(const char* dfs_path) {
    // Check if file exists before attempting to load
    int fd = open(dfs_path, O_RDONLY);
    if (fd < 0) {
        debugf("[model] file not found: %s\n", dfs_path);
        return false;
    }
    close(fd);

    model_ = t3d_model_load(dfs_path);
    if (!model_) {
        debugf("[model] load FAILED: %s\n", dfs_path);
        return false;
    }
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    debugf("[model] loaded: %s\n", dfs_path);

    // Record model draw in display list
    rspq_block_begin();
    t3d_model_draw(model_);
    dpl_ = rspq_block_end();

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

}  // namespace madeline_cube
