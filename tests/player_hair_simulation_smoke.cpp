#include <cassert>
#include <cmath>

#include "gameplay/render/player_hair_simulation.hpp"

using namespace madeline_cube;

namespace {
bool Near(float a, float b, float epsilon = 1.0e-5f) {
    return std::fabs(a - b) <= epsilon;
}

float Distance(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
}

int main() {
    // OG Player.Hair.Update operates Z-up; N64 world uses +Y-up and maps OG
    // +Y travel to -Z. Confirm boundary conversion before checking the port.
    const Vec3 og_forward{1.0f, 0.0f, 0.0f};
    assert(FromOgAxes(og_forward).x == 1.0f);
    assert(FromOgAxes(og_forward).y == 0.0f);
    assert(FromOgAxes(og_forward).z == 0.0f);
    const Vec3 og_side{0.0f, -1.0f, 0.0f};
    const Vec3 world_side = FromOgAxes(og_side);
    assert(Near(world_side.z, 1.0f));
    assert(Near(ToOgAxes(world_side).y, -1.0f));

    PlayerHairSimulation hair;
    const Vec3 head_world = FromOgAxes({0.0f, 0.0f, 8.0f});
    const Vec3 no_forward{};
    assert(hair.Update(10, 1.0f / 60.0f, head_world, no_forward, false));
    assert(hair.NodeCount() == 10);
    assert(Near(hair.Wave(), 1.0f / 15.0f));
    assert(Near(hair.NodeOg(0).z, 8.0f));

    // With zero forward, first-frame node 1 starts at Vec3.Zero in OG Hair.cs;
    // its target is the head itself (the step is applied after each node),
    // approached by exactly 25%.
    assert(Near(hair.NodeOg(1).x, 0.0f));
    assert(Near(hair.NodeOg(1).y, 0.0f));
    assert(Near(hair.NodeOg(1).z, 7.2f));
    assert(hair.Update(10, 1.0f / 60.0f, head_world, no_forward, false));
    assert(Near(hair.Wave(), 2.0f / 15.0f));
    assert(Near(hair.NodeOg(1).z, 7.4f));
    const Vec3 expected_world = FromOgAxes(hair.NodeOg(1));
    const Vec3 actual_world = hair.NodeWorld(1);
    assert(Near(actual_world.x, expected_world.x));
    assert(Near(actual_world.y, expected_world.y));
    assert(Near(actual_world.z, expected_world.z));

    // Repeated grounded updates preserve source constraints: each node stays
    // behind the face plane, no farther than .8 from parent, and above OG's
    // per-node floor clamp (4..6 units below root).
    const Vec3 forward_world = FromOgAxes(og_forward);
    for (int frame = 0; frame < 180; ++frame) {
        assert(hair.Update(10, 1.0f / 60.0f, head_world, forward_world, true));
    }
    for (int i = 1; i < hair.NodeCount(); ++i) {
        const Vec3 node = hair.NodeOg(i);
        const Vec3 parent = hair.NodeOg(i - 1);
        const float p = static_cast<float>(i) / hair.NodeCount();
        const float plane_distance = node.x - (0.0f + 1.0f);
        // OG projects against the face plane, then applies the parent .8-unit
        // reach constraint; first node can finish .2 behind that plane.
        assert(plane_distance >= -0.2001f);
        assert(Distance(node, parent) <= 0.8001f);
        // Ground clamp also precedes parent reach, so allow that same .8 bound.
        assert(node.z >= 8.0f - (4.0f + 2.0f * p) - 0.8001f);
    }

    // OG trims nodes immediately and newly grown nodes start at zero.
    assert(hair.Update(6, 1.0f / 60.0f, head_world, forward_world, false));
    assert(hair.NodeCount() == 6);
    assert(!hair.Update(PlayerHairSimulation::kMaxNodes + 1, 1.0f / 60.0f,
                        head_world, forward_world, false));
    assert(hair.NodeCount() == 6);
    assert(!hair.Update(10, -1.0f, head_world, forward_world, false));
    assert(hair.NodeCount() == 6);
    return 0;
}
