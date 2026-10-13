#pragma once

#include <cmath>
#include <cstdint>

#include "gameplay/render/animation_pose_math.hpp"

namespace madeline_cube {

enum class AnimationCurvePath : uint8_t { Translation = 0, Rotation = 1, Scale = 2 };
enum class AnimationCurveInterpolation : uint8_t { Linear = 0, Step = 1 };

struct AnimationCurveKey {
    float time = 0.0f;
    float value[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct AnimationCurve {
    uint32_t key_offset = 0;
    uint16_t key_count = 0;
    uint8_t clip_index = 0;
    uint8_t bone_index = 0;
    AnimationCurvePath path = AnimationCurvePath::Translation;
    AnimationCurveInterpolation interpolation = AnimationCurveInterpolation::Linear;
};

inline bool SampleAnimationCurve(const AnimationCurve& curve,
                                 const AnimationCurveKey* keys,
                                 float time,
                                 float output[4]) {
    if (!keys || !output || curve.key_count == 0 || !std::isfinite(time)) return false;
    const AnimationCurveKey* first = keys + curve.key_offset;
    uint16_t left = 0;
    if (time <= first[0].time) {
        left = 0;
    } else if (time >= first[curve.key_count - 1].time) {
        left = static_cast<uint16_t>(curve.key_count - 1);
    } else {
        uint16_t low = 0;
        uint16_t high = static_cast<uint16_t>(curve.key_count - 1);
        while (low + 1 < high) {
            const uint16_t middle = static_cast<uint16_t>(low + (high - low) / 2);
            if (first[middle].time <= time) low = middle;
            else high = middle;
        }
        left = low;
        if (first[high].time <= time) left = high;
    }
    if (time <= first[0].time || time >= first[curve.key_count - 1].time ||
        curve.interpolation == AnimationCurveInterpolation::Step) {
        for (int component = 0; component < 4; ++component) output[component] = first[left].value[component];
        return true;
    }
    const auto& a = first[left];
    const auto& b = first[left + 1];
    const float span = b.time - a.time;
    if (!std::isfinite(span) || span <= 0.0f) return false;
    const float amount = (time - a.time) / span;
    if (curve.path == AnimationCurvePath::Rotation) {
        AnimationQuaternion qa, qb;
        for (int component = 0; component < 4; ++component) {
            qa.v[component] = a.value[component];
            qb.v[component] = b.value[component];
        }
        const auto result = AnimationSlerp(qa, qb, amount);
        for (int component = 0; component < 4; ++component) output[component] = result.v[component];
    } else {
        for (int component = 0; component < 4; ++component) {
            output[component] = a.value[component] + (b.value[component] - a.value[component]) * amount;
        }
    }
    return true;
}

}  // namespace madeline_cube
