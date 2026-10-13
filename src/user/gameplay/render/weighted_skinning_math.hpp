#pragma once

#include <cstdint>

namespace madeline_cube {

// Column-major affine matrix matching glTF and Tiny3D's T3DMat4 storage.
struct WeightedSkinMatrix {
    float value[16]{};
};

// WSK1 source-vertex record. Keep binary layout synchronized with the
// big-endian `>3f3f4B4f` writer in tools/build_player_skin_sidecar.py.
struct WeightedSkinVertex {
    float position[3]{};
    float normal[3]{};
    uint8_t joints[4]{};
    float weights[4]{};
};

static_assert(sizeof(WeightedSkinMatrix) == 64);
static_assert(sizeof(WeightedSkinVertex) == 44);

inline void MultiplySkinMatrices(
    WeightedSkinMatrix& result,
    const WeightedSkinMatrix& left,
    const WeightedSkinMatrix& right) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.value[column * 4 + row] =
                left.value[row] * right.value[column * 4] +
                left.value[4 + row] * right.value[column * 4 + 1] +
                left.value[8 + row] * right.value[column * 4 + 2] +
                left.value[12 + row] * right.value[column * 4 + 3];
        }
    }
}

inline bool EvaluateWeightedSkinPosition(
    const WeightedSkinVertex& vertex,
    const WeightedSkinMatrix* joint_skin_matrices,
    int joint_count,
    float result[3]) {
    if (!joint_skin_matrices || joint_count <= 0 || !result) return false;

    // The production player sidecar has 1,011/2,492 vertices with exactly one
    // influence of weight 1.0. Keep this common rigid-vertex case out of the
    // weighted accumulation loop; output is the same affine transform.
    if (vertex.weights[0] != 0.0f && vertex.weights[1] == 0.0f) {
        for (int influence = 0; influence < 4; ++influence) {
            if (vertex.joints[influence] >= joint_count) return false;
        }
        const auto& matrix = joint_skin_matrices[vertex.joints[0]];
        const float weight = vertex.weights[0];
        for (int row = 0; row < 3; ++row) {
            const float transformed = matrix.value[row] * vertex.position[0] +
                                      matrix.value[4 + row] * vertex.position[1] +
                                      matrix.value[8 + row] * vertex.position[2] +
                                      matrix.value[12 + row];
            result[row] = weight == 1.0f ? transformed : weight * transformed;
        }
        return true;
    }

    result[0] = result[1] = result[2] = 0.0f;
    for (int influence = 0; influence < 4; ++influence) {
        const int joint = vertex.joints[influence];
        if (joint >= joint_count) return false;
        const float weight = vertex.weights[influence];
        if (weight == 0.0f) break;
        const auto& matrix = joint_skin_matrices[joint];
        // Distribute the shader's matrix sum over its position multiply:
        // sum(weight * M) * p == sum(weight * (M * p)). This avoids building
        // 16 blended matrix elements per vertex and skips zero-weight lanes.
        for (int row = 0; row < 3; ++row) {
            const float transformed = matrix.value[row] * vertex.position[0] +
                                      matrix.value[4 + row] * vertex.position[1] +
                                      matrix.value[8 + row] * vertex.position[2] +
                                      matrix.value[12 + row];
            result[row] += weight * transformed;
        }
    }
    return true;
}

inline void EvaluateLocalShaderNormal(const WeightedSkinVertex& vertex, float result[3]) {
    // Celeste64-og/Content/Shaders/Default.glsl applies u_model to normals,
    // not the blended joint matrix. Keep normal in actor-local basis here.
    for (int axis = 0; axis < 3; ++axis) result[axis] = vertex.normal[axis];
}

}  // namespace madeline_cube
