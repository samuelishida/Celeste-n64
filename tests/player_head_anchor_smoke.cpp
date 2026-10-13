#include <cassert>
#include <cmath>

#include "gameplay/render/player_head_anchor.hpp"

using namespace madeline_cube;

namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }
}

int main() {
    // Head joint applies +90-degree Z rotation in model space, then actor
    // applies nonuniform scale and translation. This catches reversed matrix
    // composition and omitted Tiny3D base-scale on OG's local hair origin.
    const float head[4][4] = {
        {0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    const float actor[4][4] = {
        {2.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 4.0f, 0.0f},
        {10.0f, 20.0f, 30.0f, 1.0f},
    };
    const Vec3 anchor = PlayerHeadAnchor(head, actor, {0.0f, 1.0f, -0.4f});
    assert(Near(anchor.x, -118.0f));
    assert(Near(anchor.y, 20.0f));
    assert(Near(anchor.z, -72.4f));

    const Vec3 world = FromOgAxes({2.0f, -3.0f, 5.0f});
    const Vec3 round_trip = ToOgAxes(world);
    assert(Near(round_trip.x, 2.0f));
    assert(Near(round_trip.y, -3.0f));
    assert(Near(round_trip.z, 5.0f));
    return 0;
}
