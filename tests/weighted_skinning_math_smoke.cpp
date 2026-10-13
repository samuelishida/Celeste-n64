#include "gameplay/render/weighted_skinning_math.hpp"

#include <cmath>
#include <cstdio>

using namespace madeline_cube;

namespace {
void Identity(WeightedSkinMatrix& matrix) {
    matrix = {};
    matrix.value[0] = matrix.value[5] = matrix.value[10] = matrix.value[15] = 1.0f;
}
}

int main() {
    WeightedSkinMatrix globals[2]{};
    WeightedSkinMatrix inverse_binds[2]{};
    WeightedSkinMatrix skin_matrices[2]{};
    Identity(globals[0]);
    Identity(inverse_binds[0]);
    Identity(globals[1]);
    Identity(inverse_binds[1]);

    // Non-commuting transforms catch row/column and matrix-order mistakes:
    // current global = translate(+3 X), inverse bind = scale(2 X), so
    // current * inverse-bind maps (1,2,3) to (5,2,3).
    globals[1].value[12] = 3.0f;
    inverse_binds[1].value[0] = 2.0f;
    MultiplySkinMatrices(skin_matrices[0], globals[0], inverse_binds[0]);
    MultiplySkinMatrices(skin_matrices[1], globals[1], inverse_binds[1]);

    WeightedSkinVertex vertex{};
    vertex.position[0] = 1.0f;
    vertex.position[1] = 2.0f;
    vertex.position[2] = 3.0f;
    vertex.normal[1] = 1.0f;
    vertex.joints[0] = 0;
    vertex.joints[1] = 1;
    vertex.weights[0] = 0.25f;
    vertex.weights[1] = 0.75f;

    float output[3]{};
    if (!EvaluateWeightedSkinPosition(vertex, skin_matrices, 2, output) ||
        std::fabs(output[0] - 4.0f) > 1e-6f ||
        std::fabs(output[1] - 2.0f) > 1e-6f ||
        std::fabs(output[2] - 3.0f) > 1e-6f) {
        std::fprintf(stderr, "weighted skin matrix sum/order mismatch: %.6f %.6f %.6f\n",
                     output[0], output[1], output[2]);
        return 1;
    }
    float blended[16]{};
    for (int influence = 0; influence < 4; ++influence) {
        for (int element = 0; element < 16; ++element) {
            blended[element] += skin_matrices[vertex.joints[influence]].value[element] * vertex.weights[influence];
        }
    }
    for (int row = 0; row < 3; ++row) {
        const float shader_reference = blended[row] * vertex.position[0] +
            blended[4 + row] * vertex.position[1] + blended[8 + row] * vertex.position[2] + blended[12 + row];
        if (std::fabs(shader_reference - output[row]) > 1e-6f) {
            std::fprintf(stderr, "distributed weighted transform diverges from shader matrix sum\n");
            return 1;
        }
    }
    float normal[3]{};
    EvaluateLocalShaderNormal(vertex, normal);
    if (normal[0] != 0.0f || normal[1] != 1.0f || normal[2] != 0.0f) {
        std::fprintf(stderr, "source shader contract changed: normals must bypass joint skinning\n");
        return 1;
    }

    WeightedSkinMatrix four_skin_matrices[4]{};
    for (WeightedSkinMatrix& matrix : four_skin_matrices) Identity(matrix);
    four_skin_matrices[1].value[12] = 2.0f;
    four_skin_matrices[2].value[5] = 0.5f;
    four_skin_matrices[3].value[0] = 0.0f;
    four_skin_matrices[3].value[1] = 1.0f;
    four_skin_matrices[3].value[4] = -1.0f;
    four_skin_matrices[3].value[5] = 0.0f;
    four_skin_matrices[3].value[13] = -1.0f;
    WeightedSkinVertex four_vertex{};
    four_vertex.position[0] = -2.0f;
    four_vertex.position[1] = 3.0f;
    four_vertex.position[2] = 0.5f;
    for (uint8_t lane = 0; lane < 4; ++lane) four_vertex.joints[lane] = lane;
    four_vertex.weights[0] = 0.1f;
    four_vertex.weights[1] = 0.2f;
    four_vertex.weights[2] = 0.3f;
    four_vertex.weights[3] = 0.4f;
    float four_output[3]{};
    if (!EvaluateWeightedSkinPosition(four_vertex, four_skin_matrices, 4, four_output)) {
        std::fprintf(stderr, "four-influence weighted skin evaluation rejected valid data\n");
        return 1;
    }
    float four_blended[16]{};
    for (int influence = 0; influence < 4; ++influence) {
        for (int element = 0; element < 16; ++element) {
            four_blended[element] += four_skin_matrices[influence].value[element] * four_vertex.weights[influence];
        }
    }
    for (int row = 0; row < 3; ++row) {
        const float shader_reference = four_blended[row] * four_vertex.position[0] +
            four_blended[4 + row] * four_vertex.position[1] +
            four_blended[8 + row] * four_vertex.position[2] + four_blended[12 + row];
        if (std::fabs(shader_reference - four_output[row]) > 1e-6f) {
            std::fprintf(stderr, "four-influence CPU skin differs from OG shader sum\n");
            return 1;
        }
    }

    // The production sidecar has many rigid vertices. The single-influence
    // fast path must exactly match its one-matrix affine transform.
    WeightedSkinVertex rigid{};
    rigid.position[0] = -2.0f;
    rigid.position[1] = 3.0f;
    rigid.position[2] = 0.5f;
    rigid.joints[0] = 1;
    rigid.weights[0] = 1.0f;
    float rigid_output[3]{};
    if (!EvaluateWeightedSkinPosition(rigid, four_skin_matrices, 4,
                                      rigid_output)) {
        std::fprintf(stderr, "single-influence skin path rejected valid data\n");
        return 1;
    }
    for (int row = 0; row < 3; ++row) {
        const float expected = four_skin_matrices[1].value[row] * rigid.position[0] +
            four_skin_matrices[1].value[4 + row] * rigid.position[1] +
            four_skin_matrices[1].value[8 + row] * rigid.position[2] +
            four_skin_matrices[1].value[12 + row];
        if (rigid_output[row] != expected) {
            std::fprintf(stderr, "single-influence result changed: %.7f != %.7f\n",
                         rigid_output[row], expected);
            return 1;
        }
    }

    // A non-unit single weight and malformed trailing joint IDs keep the
    // helper's existing validation/arithmetic contract.
    rigid.weights[0] = 0.25f;
    rigid.joints[3] = 4;
    if (EvaluateWeightedSkinPosition(rigid, four_skin_matrices, 4,
                                     rigid_output)) {
        std::fprintf(stderr, "invalid zero-weight joint unexpectedly accepted\n");
        return 1;
    }
    rigid.joints[3] = 0;
    if (!EvaluateWeightedSkinPosition(rigid, four_skin_matrices, 4,
                                      rigid_output)) {
        std::fprintf(stderr, "non-unit single-influence skin path rejected valid data\n");
        return 1;
    }
    for (int row = 0; row < 3; ++row) {
        const float transformed = four_skin_matrices[1].value[row] * rigid.position[0] +
            four_skin_matrices[1].value[4 + row] * rigid.position[1] +
            four_skin_matrices[1].value[8 + row] * rigid.position[2] +
            four_skin_matrices[1].value[12 + row];
        if (std::fabs(rigid_output[row] - 0.25f * transformed) > 1e-6f) {
            std::fprintf(stderr, "non-unit single-influence transform mismatch\n");
            return 1;
        }
    }

    vertex.joints[1] = 2;
    if (EvaluateWeightedSkinPosition(vertex, skin_matrices, 2, output)) {
        std::fprintf(stderr, "out-of-range influence unexpectedly accepted\n");
        return 1;
    }
    std::puts("weighted_skinning_math_smoke: PASS");
    return 0;
}
