#pragma once

#include <cmath>
#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// N64 hair sphere: four latitude divisions and six slices. OG simulation and
// node counts stay unchanged; this lower-poly render mesh reduces per-frame
// vertex transforms and RSP triangles while keeping the same silhouette axis.
struct PlayerHairSphere {
    static constexpr int kLatitudeDivisions = 4;
    static constexpr int kSlices = 6;
    static constexpr int kRingCount = kLatitudeDivisions - 1;
    static constexpr int kVertexCount = 2 + kRingCount * kSlices;
    static constexpr int kIndexCount = 3 *
        (2 * kSlices + (kRingCount - 1) * kSlices * 2);

    Vec3 positions[kVertexCount]{};
    uint8_t indices[kIndexCount]{};

    bool Build() {
        constexpr float pi = 3.14159265358979323846f;
        int vertex = 0;
        positions[vertex++] = {0.0f, 0.0f, 1.0f};
        for (int stack = 0; stack < kRingCount; ++stack) {
            const float phi = pi * static_cast<float>(stack + 1) /
                              static_cast<float>(kLatitudeDivisions);
            for (int slice = 0; slice < kSlices; ++slice) {
                const float theta = 2.0f * pi * static_cast<float>(slice) /
                                    static_cast<float>(kSlices);
                positions[vertex++] = {
                    std::sin(phi) * std::cos(theta),
                    std::sin(phi) * std::sin(theta),
                    std::cos(phi),
                };
            }
        }
        positions[vertex++] = {0.0f, 0.0f, -1.0f};

        int index = 0;
        for (int slice = 0; slice < kSlices; ++slice) {
            const int first = slice + 1;
            const int next = (slice + 1) % kSlices + 1;
            indices[index++] = 0;
            indices[index++] = static_cast<uint8_t>(next);
            indices[index++] = static_cast<uint8_t>(first);

            const int bottom = kVertexCount - 1;
            const int last_ring = 1 + (kRingCount - 1) * kSlices;
            indices[index++] = static_cast<uint8_t>(bottom);
            indices[index++] = static_cast<uint8_t>(last_ring + slice);
            indices[index++] = static_cast<uint8_t>(last_ring +
                                                    (slice + 1) % kSlices);
        }
        for (int stack = 0; stack < kRingCount - 1; ++stack) {
            const int ring0 = stack * kSlices + 1;
            const int ring1 = (stack + 1) * kSlices + 1;
            for (int slice = 0; slice < kSlices; ++slice) {
                const int next = (slice + 1) % kSlices;
                const int i0 = ring0 + slice;
                const int i1 = ring0 + next;
                const int i2 = ring1 + next;
                const int i3 = ring1 + slice;
                indices[index++] = static_cast<uint8_t>(i0);
                indices[index++] = static_cast<uint8_t>(i1);
                indices[index++] = static_cast<uint8_t>(i2);
                indices[index++] = static_cast<uint8_t>(i0);
                indices[index++] = static_cast<uint8_t>(i2);
                indices[index++] = static_cast<uint8_t>(i3);
            }
        }
        return vertex == kVertexCount && index == kIndexCount;
    }
};

}  // namespace madeline_cube
