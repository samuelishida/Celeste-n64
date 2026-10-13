#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube {

constexpr float kPlayerHeadAnchorBaseScale = 64.0f;

// Tiny3D/fgeom stores row-vector transforms with translation in matrix row 3.
// Match fm_mat4_mul_vec3 so this host-testable adapter also covers the runtime
// Head-bone -> actor-matrix composition used by procedural hair.
// Templated on the row width: fgeom bone matrices are [4][4] while T3DMat4 is
// [4][3]; both only ever index columns 0..2 here.
template <int Cols>
inline Vec3 TransformPlayerAnchorPoint(const float (&matrix)[4][Cols], const Vec3& point) {
    return {
        matrix[0][0] * point.x + matrix[1][0] * point.y +
            matrix[2][0] * point.z + matrix[3][0],
        matrix[0][1] * point.x + matrix[1][1] * point.y +
            matrix[2][1] * point.z + matrix[3][1],
        matrix[0][2] * point.x + matrix[1][2] * point.y +
            matrix[2][2] * point.z + matrix[3][2],
    };
}

template <int HeadCols, int ActorCols>
inline Vec3 PlayerHeadAnchor(const float (&head_model)[4][HeadCols],
                             const float (&actor_model)[4][ActorCols],
                             const Vec3& source_local_offset) {
    const Vec3 scaled_offset = {
        source_local_offset.x * kPlayerHeadAnchorBaseScale,
        source_local_offset.y * kPlayerHeadAnchorBaseScale,
        source_local_offset.z * kPlayerHeadAnchorBaseScale,
    };
    const Vec3 head_model_point = TransformPlayerAnchorPoint(head_model, scaled_offset);
    return TransformPlayerAnchorPoint(actor_model, head_model_point);
}

}  // namespace madeline_cube
