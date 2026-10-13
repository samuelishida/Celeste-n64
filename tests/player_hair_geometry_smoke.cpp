#include <cassert>
#include <cmath>

#include "gameplay/render/player_hair_geometry.hpp"
#include "gameplay/render/player_hair_sphere.hpp"

using namespace madeline_cube;

namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }

Vec3 SourceHairVertex(const Vec3& v, const Vec3& node, int index, int count,
                      const Vec3& forward, const Vec3& squish, float roundness) {
    const float progress = static_cast<float>(index) / static_cast<float>(count);
    const float source_xz_start = (1.0f - roundness) * 3.2f + roundness * 3.0f;
    const float source_xz = (1.0f - progress) * source_xz_start + progress;
    const float source_y_start = (1.0f - roundness) * 4.0f + roundness * 3.0f;
    const float source_y_end = (1.0f - roundness) * 2.0f + roundness;
    const float source_y = (1.0f - progress) * source_y_start + progress * source_y_end;
    const float rotation = std::atan2(forward.y, forward.x);
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const float x = v.x * source_xz * squish.x;
    const float y = v.y * source_y * squish.y;
    const float z = v.z * source_xz * squish.z;
    // System.Numerics row-vector order: Scale * RotationZ * Translation.
    return {node.x + x * cosine - y * sine,
            node.y + x * sine + y * cosine,
            node.z + z};
}
}

int main() {
    // N64 hair vertices are rounded to int16 every frame. The fast packer
    // must match std::lround exactly, including halfway-away-from-zero and
    // the original rejection range.
    for (int quarter = -131072; quarter <= 131068; ++quarter) {
        const float value = static_cast<float>(quarter) * 0.25f;
        int16_t packed = 0;
        const bool accepted = PackPlayerHairCoordinate(value, packed);
        const bool expected_accepted = value >= -32768.0f && value <= 32767.0f;
        assert(accepted == expected_accepted);
        if (expected_accepted) {
            assert(packed == static_cast<int16_t>(std::lround(value)));
        }
    }
    const float invalid_values[] = {NAN, INFINITY, -INFINITY,
                                    -32768.25f, 32767.25f};
    for (float value : invalid_values) {
        int16_t packed = 0;
        assert(!PackPlayerHairCoordinate(value, packed));
    }

    PlayerHairSphere sphere;
    assert(sphere.Build());
    constexpr int node_counts[] = {10, 16, 18};
    constexpr float roundness_values[] = {0.0f, 0.37f, 1.0f};
    const Vec3 nodes[] = {{12.5f, -3.1f, 24.8f}, {-8.0f, 6.25f, -2.5f}};
    const Vec3 forwards[] = {{1.0f, 0.0f, 0.0f},
                             {0.6f, -0.8f, 0.0f},
                             {-0.70710678f, 0.70710678f, 0.0f}};
    const Vec3 squishes[] = {{1.0f, 1.0f, 1.0f},
                             {0.9f, 1.1f, 0.85f}};

    for (int count : node_counts) {
        const int indices[] = {0, 1, count / 2, count - 1};
        for (const Vec3& forward : forwards) {
            const PlayerHairRotation rotation = MakePlayerHairRotation(forward);
            for (int index : indices) {
                for (const Vec3& node : nodes) {
                    for (const Vec3& squish : squishes) {
                        for (float roundness : roundness_values) {
                            const PlayerHairNodeTransform transform =
                                MakePlayerHairNodeTransform(node, index, count,
                                                            squish, roundness,
                                                            rotation);
                            for (const Vec3& sphere_vertex : sphere.positions) {
                                const Vec3 expected = SourceHairVertex(
                                    sphere_vertex, node, index, count, forward,
                                    squish, roundness);
                                const Vec3 actual = PlayerHairSourceVertex(
                                    sphere_vertex, node, index, count, forward,
                                    squish, roundness);
                                const Vec3 optimized = PlayerHairSourceVertex(
                                    sphere_vertex, transform);
                                assert(Near(actual.x, expected.x));
                                assert(Near(actual.y, expected.y));
                                assert(Near(actual.z, expected.z));
                                assert(Near(optimized.x, expected.x));
                                assert(Near(optimized.y, expected.y));
                                assert(Near(optimized.z, expected.z));
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
