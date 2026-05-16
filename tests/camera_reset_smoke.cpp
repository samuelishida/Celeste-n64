#include <cassert>
#include <cmath>
#include <limits>

#include "../src/user/gameplay/camera_controller.hpp"

using namespace madeline_cube;

int main() {
    CameraController controller;
    CameraState camera;
    camera.target_forward = {std::numeric_limits<float>::denorm_min(), 0.0f, 1.0f};

    controller.Reset(camera, {0.0f, 0.0f, 0.0f});

    assert(camera.target_forward.x == 0.0f);
    assert(std::fpclassify(camera.position.x) != FP_SUBNORMAL);

    CameraState dirty_origin;
    controller.Reset(dirty_origin, {std::numeric_limits<float>::denorm_min(), 0.0f, 0.0f});
    assert(dirty_origin.origin.x == 0.0f);
    assert(std::fpclassify(dirty_origin.position.x) != FP_SUBNORMAL);
    return 0;
}
