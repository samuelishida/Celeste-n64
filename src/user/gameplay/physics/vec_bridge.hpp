#pragma once

// Bridge between project Vec3 and N64-specific types (fm_vec3_t / T3DVec3).
// Guards: only compiled on the N64 target where libdragon headers are present.
//
// Layout compatibility assertions prevent silent mismatches if any type's
// definition changes. The reinterpret_cast is safe only when sizeof + alignof
// are equal AND the fields are in the same order (float x, y, z).

#ifdef __mips__  // GCC/Clang MIPS; NOT __MIPS__ (that is Metrowerks/CodeWarrior)

#include <fgeom.h>
#include <t3d/t3dmath.h>
#include "gameplay/math_types.hpp"

#include <type_traits>
#include <cstddef>

namespace madeline_cube::physics {

// fm_vec3_t is a union {struct{float x,y,z}; float v[3]}.
// Vec3 is struct{float x,y,z} — same ABI.
static_assert(sizeof(fm_vec3_t) == sizeof(Vec3),
    "fm_vec3_t / Vec3 size mismatch — vec_bridge reinterpret_cast unsafe");
static_assert(alignof(fm_vec3_t) == alignof(Vec3),
    "fm_vec3_t / Vec3 align mismatch");
// T3DVec3 == fm_vec3_t (typedef in t3dmath.h), so same static_asserts apply.

inline Vec3& AsVec3(fm_vec3_t& v) {
    return reinterpret_cast<Vec3&>(v);
}
inline const Vec3& AsVec3(const fm_vec3_t& v) {
    return reinterpret_cast<const Vec3&>(v);
}
inline fm_vec3_t& AsFmVec3(Vec3& v) {
    return reinterpret_cast<fm_vec3_t&>(v);
}
inline const fm_vec3_t& AsFmVec3(const Vec3& v) {
    return reinterpret_cast<const fm_vec3_t&>(v);
}

}  // namespace madeline_cube::physics

#endif  // __mips__
