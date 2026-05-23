#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include "gameplay/world/world.hpp"

namespace madeline_cube {

class PropRenderer {
public:
    bool Init(const Room& room);
    void Draw() const;
    void Free();

private:
    static constexpr int kMaxModels = 8;
    static constexpr int kMaxInstances = Room::kMaxProps;

    struct LoadedModel {
        char name[16];
        T3DModel* model;
    };
    LoadedModel models_[kMaxModels];
    int model_count_ = 0;

    struct Instance {
        int model_index;
        T3DMat4FP* matrix_fp;
    };
    Instance instances_[kMaxInstances];
    int instance_count_ = 0;

    int FindOrLoadModel(const char* stem);
};

}  // namespace madeline_cube
