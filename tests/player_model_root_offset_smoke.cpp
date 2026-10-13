#include "gameplay/player/player_model_clearance.hpp"

#include <cmath>
#include <cstdio>

using madeline_cube::PlayerModelRootOffset;
using madeline_cube::Vec3;

namespace {
bool Near(float actual, float expected) {
    return std::fabs(actual - expected) < 1.0e-5f;
}
}

int main() {
    constexpr float offset = madeline_cube::kMadelineModelRootOffsetX;
    const Vec3 forward = PlayerModelRootOffset({0.0f, 0.0f, 1.0f}, 1.0f);
    const Vec3 right = PlayerModelRootOffset({1.0f, 0.0f, 0.0f}, 1.0f);
    const Vec3 backward = PlayerModelRootOffset({0.0f, 0.0f, -1.0f}, 0.5f);
    if (!Near(forward.x, offset) || !Near(forward.z, 0.0f) ||
        !Near(right.x, 0.0f) || !Near(right.z, -offset) ||
        !Near(backward.x, -offset * 0.5f) || !Near(backward.z, 0.0f)) {
        std::fprintf(stderr, "source root offset does not follow model-local +X/right basis\n");
        return 1;
    }
    std::puts("player model root offset smoke: PASS");
    return 0;
}
