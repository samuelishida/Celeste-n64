#include <cassert>
#include <cmath>
#include "gameplay/player/camera_controller.hpp"
using namespace madeline_cube;
int main() {
    const float zooms[] = {0, 1.0f/3.0f, .5f, 2.0f/3.0f, 1};
    const float heights[] = {1, 30, 55, 80, 180};
    const float distances[] = {30, 60, 85, 110, 110};
    for (int i=0;i<5;++i) {
        CameraController controller;
        CameraState camera; controller.Reset(camera,{10,20,30});
        camera.target_distance=zooms[i]; controller.OrientForward(camera,{0,0,1});
        assert(std::fabs(camera.target.y-32)<.001f);
        assert(std::fabs(camera.position.y-(20+heights[i]))<.001f);
        assert(std::fabs(camera.position.z-(30-distances[i]))<.001f);
        controller.OrientForward(camera,{1,0,0});
        assert(std::fabs(camera.position.x-(10-distances[i]))<.001f);
        assert(std::fabs(camera.position.y-(20+heights[i]))<.001f);
    }
    // Source zoom changes normalized intent1/s, but keeps normal follow decay.
    CameraController controller; CameraState camera;
    controller.Reset(camera,{0,0,0});
    controller.Step(camera,{0,0,0},false,true,0,{0,1},.1f);
    const float alpha=1-std::pow(.01f,.1f);
    assert(std::fabs(camera.target_distance-.6f)<.0001f);
    assert(std::fabs(camera.position.y-(55+15*alpha))<.001f);
    assert(std::fabs(camera.position.z-(-85-15*alpha))<.001f);
    const float saved=camera.target_distance;const Vec3 saved_forward=camera.target_forward;
    controller.Reset(camera,{10,20,30});
    assert(camera.target_distance==saved && camera.target_forward.z==saved_forward.z);
    camera.target_distance=.36f;
    controller.InputPhase(camera,{},.01f);
    assert(std::fabs(camera.target_distance-.355f)<.0001f);
}
