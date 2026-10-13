#include <cassert>
#include <cmath>
#include <limits>

#include "gameplay/render/animation_source_curve.hpp"

using namespace madeline_cube;

static bool Near(float a, float b, float epsilon = 1e-5f) {
    return std::fabs(a - b) <= epsilon;
}

int main() {
    AnimationCurveKey keys[] = {
        {0.25f, {1.0f, 2.0f, 3.0f, 0.0f}},
        {0.75f, {3.0f, 4.0f, 5.0f, 0.0f}},
    };
    AnimationCurve curve{0, 2, 0, 0, AnimationCurvePath::Translation,
                         AnimationCurveInterpolation::Linear};
    float value[4]{};
    assert(SampleAnimationCurve(curve, keys, 0.0f, value));
    assert(Near(value[0], 1.0f) && Near(value[2], 3.0f));
    assert(SampleAnimationCurve(curve, keys, 0.5f, value));
    assert(Near(value[0], 2.0f) && Near(value[1], 3.0f) && Near(value[2], 4.0f));
    assert(SampleAnimationCurve(curve, keys, 1.0f, value));
    assert(Near(value[0], 3.0f) && Near(value[2], 5.0f));

    curve.interpolation = AnimationCurveInterpolation::Step;
    assert(SampleAnimationCurve(curve, keys, 0.74999f, value));
    assert(Near(value[0], 1.0f));
    assert(SampleAnimationCurve(curve, keys, 0.75f, value));
    assert(Near(value[0], 3.0f));

    AnimationCurveKey rotations[] = {
        {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 0.0f}},
    };
    curve = {0, 2, 0, 0, AnimationCurvePath::Rotation,
             AnimationCurveInterpolation::Linear};
    assert(SampleAnimationCurve(curve, rotations, 0.5f, value));
    const float half = std::sqrt(0.5f);
    assert(Near(value[2], half) && Near(value[3], half));
    assert(!SampleAnimationCurve(curve, rotations,
                                 std::numeric_limits<float>::quiet_NaN(), value));
    curve.key_count = 0;
    assert(!SampleAnimationCurve(curve, rotations, 0.5f, value));
    return 0;
}
