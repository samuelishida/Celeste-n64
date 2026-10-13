#include <cassert>
#include <cmath>
#include "gameplay/render/animation_pose_math.hpp"
using namespace madeline_cube;
static bool Near(float a, float b) { return std::fabs(a-b) < .00002f; }
static AnimationQuaternion Yaw(float radians) {
    return {{0, std::sin(radians/2), 0, std::cos(radians/2)}};
}
static float Angle(const AnimationQuaternion& q) { return 2*std::atan2(q.v[1],q.v[3]); }
int main() {
    constexpr float pi = 3.141592653589793f;
    AnimationPose poses[3];
    poses[0].position[0] = 10;
    poses[0].scale[1] = 3;
    poses[0].rotation = Yaw(pi/2);
    const float zero[] = {0};
    auto sole = BlendAnimationPoses(poses, zero, 1);
    assert(sole.position[0] == 10 && sole.scale[1] == 3);
    assert(Near(Angle(sole.rotation), pi/2));
    const float two[] = {.75f,.25f};
    auto blend = BlendAnimationPoses(poses, two, 2);
    assert(Near(blend.position[0],7.5f) && Near(blend.scale[1],2.5f));
    assert(Near(Angle(blend.rotation), pi/8));
    const float unnormalized[] = {3,1};
    auto normalized = BlendAnimationPoses(poses, unnormalized, 2);
    assert(Near(normalized.position[0],blend.position[0]));
    assert(Near(Angle(normalized.rotation),Angle(blend.rotation)));
    poses[0].rotation = Yaw(pi * 2/3);
    poses[1].rotation = Yaw(pi /3);
    poses[2].rotation = Yaw(0);
    poses[1].position[0] = 20;
    poses[2].position[0] = 30;
    const float three[] = {.2f,.3f,.5f};
    auto interrupted = BlendAnimationPoses(poses,three,3);
    assert(Near(interrupted.position[0],23));
    // Quaternion: 60 -> 120 at .6 = 96; then 0 -> 96 at .5 = 48 degrees.
    assert(Near(Angle(interrupted.rotation),pi*48/180));
    auto close = AnimationSlerp(Yaw(.000001f),Yaw(.000002f),.3f);
    assert(Near(Angle(close),.0000013f));
    auto a = Yaw(.7f), antipodal = a;
    for (float& component : antipodal.v) component = -component;
    auto same = AnimationSlerp(a,antipodal,.5f);
    assert(Near(Angle(same),.7f));
    assert(Near(Angle(AnimationSlerp(a,a,.4f)),.7f));
    assert(BlendAnimationPoses(nullptr,nullptr,0).scale[0] == 1);
}
