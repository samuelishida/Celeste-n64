#include <cassert>
#include <cmath>

#include "gameplay/world/actor_update_view.hpp"

using namespace madeline_cube;

namespace {
bool Near(float a, float b, float epsilon = 0.02f) {
    return std::fabs(a - b) <= epsilon;
}
}

int main() {
    const ActorUpdateView view =
        BuildActorUpdateView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f);
    assert(view.valid);
    // OG World: 45-degree vertical FOV, 16:9 640x360 target, near 20, far 800,
    // then BoundingBox.Inflate(10).
    assert(Near(view.min.x, -599.1037f));
    assert(Near(view.max.x, 599.1037f));
    assert(Near(view.min.y, -341.3708f));
    assert(Near(view.max.y, 341.3708f));
    assert(Near(view.min.z, 10.0f));
    assert(Near(view.max.z, 810.0f));

    assert(view.IntersectsBounds({-1.0f, -1.0f, 11.0f},
                                 {1.0f, 1.0f, 12.0f}));
    // Actor at x=550 lies inside OG's 16:9 update AABB; old 4:3 box culled it.
    assert(view.IntersectsBounds({550.0f, -1.0f, 799.0f},
                                 {551.0f, 1.0f, 800.0f}));
    assert(!view.IntersectsBounds({-1.0f, -1.0f, -12.0f},
                                  {1.0f, 1.0f, -11.0f}));
    assert(!view.IntersectsBounds({700.0f, 0.0f, 100.0f},
                                  {701.0f, 1.0f, 101.0f}));

    const ActorUpdateView wide_fov =
        BuildActorUpdateView({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.2f);
    assert(wide_fov.min.x < view.min.x);
    assert(wide_fov.min.y < view.min.y);

    const ActorUpdateView invalid =
        BuildActorUpdateView({1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f}, 1.0f);
    assert(!invalid.valid);
    // Invalid camera must fail open instead of freezing world actors.
    assert(invalid.IntersectsBounds({10000.0f, 10000.0f, 10000.0f},
                                    {10001.0f, 10001.0f, 10001.0f}));
}
