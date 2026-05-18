#pragma once

#include "gameplay/math_types.hpp"

namespace madeline_cube::physics {

// ---------------------------------------------------------------------------
// Ray / segment vs triangle (Möller–Trumbore)
// ---------------------------------------------------------------------------

enum class BackfaceCull : bool { Ignore = false, Cull = true };

struct TriHit {
    bool  hit = false;
    float t = 0.0f;     // param in [0,1] along segment p0→p1 (or along ray for RayHit)
    Vec3  point;
    Vec3  normal;       // un-normalized face normal (b-a)×(c-a); caller normalises
};

// Segment p0→p1 vs triangle (a, b, c).
// t in [0,1] (segment — not unbounded ray).
TriHit SegmentTriangle(const Vec3& p0, const Vec3& p1,
                       const Vec3& a, const Vec3& b, const Vec3& c,
                       BackfaceCull cull = BackfaceCull::Ignore);

// ---------------------------------------------------------------------------
// Sphere vs triangle (Ericson §5.2.7 — closest point on triangle)
// ---------------------------------------------------------------------------

struct SphereTriHit {
    bool  hit = false;
    Vec3  closest;   // closest point on triangle to sphere centre
    Vec3  normal;    // (centre - closest), normalised; points from tri surface toward centre
    float dist = 0.0f;   // signed distance (positive = outside sphere surface touching tri)
};

SphereTriHit SphereTriangle(const Vec3& centre, float radius,
                             const Vec3& a, const Vec3& b, const Vec3& c);

// ---------------------------------------------------------------------------
// AABB vs AABB
// ---------------------------------------------------------------------------

struct AABB {
    Vec3 min;
    Vec3 max;
};

inline bool AABBOverlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// ---------------------------------------------------------------------------
// AABB vs sphere
// ---------------------------------------------------------------------------

struct AabbSphereHit {
    bool  hit = false;
    float dist2 = 0.0f;   // squared distance from centre to nearest AABB point
};

AabbSphereHit AABBSphere(const AABB& box, const Vec3& centre, float radius);

// ---------------------------------------------------------------------------
// Segment vs AABB (slab method — Kay–Kajiya)
// ---------------------------------------------------------------------------

struct SegAabbHit {
    bool  hit = false;
    float t_enter = 0.0f;   // entry param in [0,1]
    float t_exit  = 1.0f;   // exit param in [0,1]
};

SegAabbHit SegmentAABB(const Vec3& p0, const Vec3& p1, const AABB& box);

}  // namespace madeline_cube::physics
