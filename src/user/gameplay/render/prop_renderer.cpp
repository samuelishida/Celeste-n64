#include "gameplay/render/prop_renderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

namespace madeline_cube {

int PropRenderer::FindOrLoadModel(const char* stem) {
    for (int i = 0; i < model_count_; ++i) {
        if (strcmp(models_[i].name, stem) == 0) {
            return i;
        }
    }

    if (model_count_ >= kMaxModels) {
        debugf("[prop] model limit reached\n");
        return -1;
    }

    char mdl_path[64];
    snprintf(mdl_path, sizeof(mdl_path), "rom:/mdl/%s.t3dm", stem);
    T3DModel* model = t3d_model_load(mdl_path);
    if (!model) {
        debugf("[prop] model %s missing\n", stem);
        return -1;
    }

    strncpy(models_[model_count_].name, stem, 15);
    models_[model_count_].name[15] = '\0';
    models_[model_count_].model = model;
    return model_count_++;
}

bool PropRenderer::Init(const Room& room) {
    model_count_ = 0;
    instance_count_ = 0;

    for (int i = 0; i < room.prop_count && instance_count_ < kMaxInstances; ++i) {
        auto& prop = room.props[i];
        int model_idx = FindOrLoadModel(prop.model_name);
        if (model_idx < 0) continue;

        T3DMat4FP* mat = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
        if (!mat) continue;

        T3DMat4 model;
        float scale[3] = {1.0f, 1.0f, 1.0f};
        float rot[3] = {0.0f, prop.angle_rad, 0.0f};
        float pos[3] = {prop.position.x, prop.position.y, prop.position.z};
        t3d_mat4_from_srt_euler(&model, scale, rot, pos);
        t3d_mat4_to_fixed(mat, &model);

        instances_[instance_count_].model_index = model_idx;
        instances_[instance_count_].matrix_fp = mat;
        instance_count_++;
    }

    return true;
}

void PropRenderer::Draw() const {
    for (int i = 0; i < instance_count_; ++i) {
        t3d_matrix_push(instances_[i].matrix_fp);
        t3d_model_draw(models_[instances_[i].model_index].model);
        t3d_tri_sync();
        t3d_matrix_pop(1);
    }
}

void PropRenderer::Free() {
    for (int i = 0; i < model_count_; ++i) {
        if (models_[i].model) {
            t3d_model_free(models_[i].model);
        }
    }
    for (int i = 0; i < instance_count_; ++i) {
        if (instances_[i].matrix_fp) {
            free_uncached(instances_[i].matrix_fp);
        }
    }
    model_count_ = 0;
    instance_count_ = 0;
}

}  // namespace madeline_cube
