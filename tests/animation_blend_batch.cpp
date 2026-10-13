#include <cstdint>
#include <cstdio>
#include <vector>

#include "gameplay/render/animation_pose_math.hpp"

using namespace madeline_cube;

int main() {
    uint32_t record_count = 0;
    if (std::fread(&record_count, sizeof(record_count), 1, stdin) != 1) return 1;
    for (uint32_t record = 0; record < record_count; ++record) {
        uint32_t count = 0;
        if (std::fread(&count, sizeof(count), 1, stdin) != 1 || count == 0 || count > 7) return 2;
        std::vector<AnimationPose> poses(count);
        std::vector<float> weights(count);
        for (uint32_t index = 0; index < count; ++index) {
            float values[10];
            if (std::fread(&weights[index], sizeof(float), 1, stdin) != 1 ||
                std::fread(values, sizeof(float), 10, stdin) != 10) return 3;
            for (int axis = 0; axis < 3; ++axis) poses[index].scale[axis] = values[axis];
            for (int axis = 0; axis < 3; ++axis) poses[index].position[axis] = values[axis + 3];
            for (int component = 0; component < 4; ++component) {
                poses[index].rotation.v[component] = values[component + 6];
            }
        }
        const AnimationPose blended = BlendAnimationPoses(poses.data(), weights.data(), count);
        float output[10];
        for (int axis = 0; axis < 3; ++axis) output[axis] = blended.scale[axis];
        for (int axis = 0; axis < 3; ++axis) output[axis + 3] = blended.position[axis];
        for (int component = 0; component < 4; ++component) {
            output[component + 6] = blended.rotation.v[component];
        }
        if (std::fwrite(output, sizeof(float), 10, stdout) != 10) return 4;
    }
    return std::getchar() == EOF ? 0 : 5;
}
