#pragma once
#include <cmath>

namespace madeline_cube {
struct AnimationQuaternion { float v[4] = {0, 0, 0, 1}; };
struct AnimationPose {
    float scale[3] = {1, 1, 1};
    float position[3] = {0, 0, 0};
    AnimationQuaternion rotation;
};

// Matches System.Numerics Quaternion.Slerp used by pinned SharpGLTF.
inline AnimationQuaternion AnimationSlerp(const AnimationQuaternion& a,
                                         const AnimationQuaternion& b, float amount) {
    float dot = 0;
    for (int axis = 0; axis < 4; ++axis) dot += a.v[axis] * b.v[axis];
    const float sign = dot < 0 ? -1.0f : 1.0f;
    dot = std::fmin(std::fabs(dot), 1.0f);
    float left = 1.0f - amount;
    float right = amount * sign;
    if (dot < 1.0f - 1.0e-6f) {
        const float angle = std::acos(dot);
        const float inverse_sine = 1.0f / std::sin(angle);
        left = std::sin((1.0f - amount) * angle) * inverse_sine;
        right = std::sin(amount * angle) * inverse_sine * sign;
    }
    AnimationQuaternion result;
    for (int axis = 0; axis < 4; ++axis) result.v[axis] = a.v[axis] * left + b.v[axis] * right;
    return result;
}

// Input order and weights are OG GetBlendedWeights' reverse tracks and 1-blend.
// alpha0031 weights S/T directly but reverses quaternion accumulation arguments.
inline AnimationPose BlendAnimationPoses(const AnimationPose* inputs,
                                        const float* weights, int count) {
    if (count <= 0) return {};
    if (count == 1) return inputs[0]; // sole zero-weight track still samples
    float sum = 0;
    for (int i = 0; i < count; ++i) sum += weights[i];
    const float normalization = sum == 0 ? 1.0f : 1.0f / sum;
    AnimationPose result;
    for (int axis = 0; axis < 3; ++axis) result.scale[axis] = 0;
    float cumulative = 0;
    for (int i = 0; i < count; ++i) {
        const float weight = weights[i] * normalization;
        for (int axis = 0; axis < 3; ++axis) {
            result.scale[axis] += inputs[i].scale[axis] * weight;
            result.position[axis] += inputs[i].position[axis] * weight;
        }
        cumulative += weight;
        if (i == 0) result.rotation = inputs[i].rotation;
        else result.rotation = AnimationSlerp(inputs[i].rotation, result.rotation,
            cumulative > 0 ? weight / cumulative : 0);
    }
    float length_sq = 0;
    for (float component : result.rotation.v) length_sq += component * component;
    if (length_sq > 0) {
        const float inverse_length = 1.0f / std::sqrt(length_sq);
        for (float& component : result.rotation.v) component *= inverse_length;
    }
    return result;
}
} // namespace madeline_cube
