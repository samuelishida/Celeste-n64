#include <cassert>
#include <cmath>

#include "gameplay/render/player_hair_sphere.hpp"

using namespace madeline_cube;

int main() {
    PlayerHairSphere sphere;
    assert(sphere.Build());
    assert(PlayerHairSphere::kVertexCount == 20);
    assert(PlayerHairSphere::kIndexCount == 108);
    assert(sphere.indices[0] == 0 && sphere.indices[1] == 2 && sphere.indices[2] == 1);
    assert(sphere.indices[3] == 19 && sphere.indices[4] == 13 && sphere.indices[5] == 14);
    for (const Vec3& vertex : sphere.positions) {
        const float length = std::sqrt(vertex.x * vertex.x + vertex.y * vertex.y + vertex.z * vertex.z);
        assert(std::fabs(length - 1.0f) < 1.0e-5f);
    }
    for (uint8_t index : sphere.indices) assert(index < PlayerHairSphere::kVertexCount);
    for (int index = 0; index < PlayerHairSphere::kIndexCount; index += 3) {
        const Vec3& a = sphere.positions[sphere.indices[index]];
        const Vec3& b = sphere.positions[sphere.indices[index + 1]];
        const Vec3& c = sphere.positions[sphere.indices[index + 2]];
        const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 normal{ab.y * ac.z - ab.z * ac.y,
                          ab.z * ac.x - ab.x * ac.z,
                          ab.x * ac.y - ab.y * ac.x};
        const Vec3 centroid{(a.x + b.x + c.x) / 3.0f,
                            (a.y + b.y + c.y) / 3.0f,
                            (a.z + b.z + c.z) / 3.0f};
        assert(normal.x * centroid.x + normal.y * centroid.y +
               normal.z * centroid.z < 0.0f);
    }
    return 0;
}
