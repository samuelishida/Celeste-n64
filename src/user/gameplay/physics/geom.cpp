#include "geom.hpp"

#include <cmath>
#include <cfloat>

namespace madeline_cube::physics {

// ---------------------------------------------------------------------------
// Vec3 helpers (avoid dependency on T3DVec3 / fm_vec3_t in this pure layer)
// ---------------------------------------------------------------------------

namespace {

inline Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 Scale(const Vec3& v, float s)     { return {v.x*s, v.y*s, v.z*s}; }
inline float Dot(const Vec3& a, const Vec3& b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}
inline float Len2(const Vec3& v) { return Dot(v, v); }
inline Vec3 Norm(const Vec3& v) {
    float l = std::sqrt(Len2(v));
    if (l < 1e-12f) return {0.f,1.f,0.f};
    float inv = 1.f / l;
    return {v.x*inv, v.y*inv, v.z*inv};
}
inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float ClampF(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Closest point on segment ab to point p; returns parameter t ∈ [0,1].
inline Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p, float& out_t) {
    Vec3 ab = Sub(b, a);
    float denom = Len2(ab);
    if (denom < 1e-12f) { out_t = 0.f; return a; }
    out_t = Clamp01(Dot(Sub(p, a), ab) / denom);
    return Add(a, Scale(ab, out_t));
}

}  // namespace

// ---------------------------------------------------------------------------
// Möller–Trumbore
// ---------------------------------------------------------------------------

TriHit SegmentTriangle(const Vec3& p0, const Vec3& p1,
                       const Vec3& a,  const Vec3& b,  const Vec3& c,
                       BackfaceCull cull)
{
    constexpr float kEps = 1e-7f;
    TriHit result;

    Vec3 d = Sub(p1, p0);
    Vec3 edge1 = Sub(b, a);
    Vec3 edge2 = Sub(c, a);

    Vec3  h = Cross(d, edge2);
    float det = Dot(edge1, h);

    if (cull == BackfaceCull::Cull) {
        if (det < kEps) return result;  // backface or parallel
    } else {
        if (std::fabs(det) < kEps) return result;  // parallel
    }

    float inv_det = 1.f / det;
    Vec3  s = Sub(p0, a);

    // Two-sided barycentric bounds: when det < 0 the u/v numerators must have
    // opposite sign to det for u,v ∈ [0,1].  Multiplying by det and flipping
    // the inequality is equivalent and avoids a branch.
    float u_num = Dot(s, h);
    if (det > 0.f ? (u_num < 0.f || u_num > det) : (u_num > 0.f || u_num < det))
        return result;

    Vec3  q = Cross(s, edge1);
    float v_num = Dot(d, q);
    if (det > 0.f ? (v_num < 0.f || u_num + v_num > det) : (v_num > 0.f || u_num + v_num < det))
        return result;

    float t = inv_det * Dot(edge2, q);
    if (t < 0.f || t > 1.f) return result;   // outside segment [0,1]

    result.hit    = true;
    result.t      = t;
    result.point  = Add(p0, Scale(d, t));
    result.normal = Cross(edge1, edge2);      // unnormalized; caller normalizes if needed
    return result;
}

// ---------------------------------------------------------------------------
// Sphere vs triangle — Ericson §5.2.7 closest-point-on-triangle
// ---------------------------------------------------------------------------

SphereTriHit SphereTriangle(const Vec3& P, float radius,
                             const Vec3& a, const Vec3& b, const Vec3& c)
{
    SphereTriHit result;

    // Barycentric coordinate computation (Ericson §5.1.5 / §5.2.7)
    Vec3 ab = Sub(b, a);
    Vec3 ac = Sub(c, a);
    Vec3 ap = Sub(P, a);

    float d1 = Dot(ab, ap);
    float d2 = Dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) {
        // Vertex region A
        Vec3 diff = Sub(P, a);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = a;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    Vec3 bp = Sub(P, b);
    float d3 = Dot(ab, bp);
    float d4 = Dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) {
        // Vertex region B
        Vec3 diff = Sub(P, b);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = b;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    float vc = d1*d4 - d3*d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        // Edge AB
        float denom = d1 - d3;
        if (std::fabs(denom) < 1e-12f) return result;  // degenerate edge
        float v = d1 / denom;
        Vec3 closest = Add(a, Scale(ab, v));
        Vec3 diff = Sub(P, closest);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = closest;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    Vec3 cp = Sub(P, c);
    float d5 = Dot(ab, cp);
    float d6 = Dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) {
        // Vertex region C
        Vec3 diff = Sub(P, c);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = c;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    float vb = d5*d2 - d1*d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        // Edge AC
        float denom = d2 - d6;
        if (std::fabs(denom) < 1e-12f) return result;  // degenerate edge
        float w = d2 / denom;
        Vec3 closest = Add(a, Scale(ac, w));
        Vec3 diff = Sub(P, closest);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = closest;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    float va = d3*d6 - d5*d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        // Edge BC
        float denom = (d4 - d3) + (d5 - d6);
        if (std::fabs(denom) < 1e-12f) return result;  // degenerate edge
        float w = (d4 - d3) / denom;
        Vec3 bc = Sub(c, b);
        Vec3 closest = Add(b, Scale(bc, w));
        Vec3 diff = Sub(P, closest);
        float d2_ = Len2(diff);
        if (d2_ <= radius*radius) {
            result.hit     = true;
            result.closest = closest;
            result.dist    = std::sqrt(d2_);
            result.normal  = Norm(diff);
        }
        return result;
    }

    // Interior of triangle — project P onto plane
    float tri_area = va + vb + vc;
    if (std::fabs(tri_area) < 1e-12f) return result;  // degenerate triangle
    float denom = 1.f / tri_area;
    float v_c = vb * denom;
    float w_c = vc * denom;
    Vec3 closest = Add(Add(a, Scale(ab, v_c)), Scale(ac, w_c));
    Vec3 diff = Sub(P, closest);
    float d2_ = Len2(diff);
    if (d2_ <= radius*radius) {
        result.hit     = true;
        result.closest = closest;
        result.dist    = std::sqrt(d2_);
        // When sphere centre is on the plane, normal falls back to face normal
        if (d2_ < 1e-12f)
            result.normal = Norm(Cross(ab, ac));
        else
            result.normal = Norm(diff);
    }
    return result;
}

// ---------------------------------------------------------------------------
// AABB vs sphere
// ---------------------------------------------------------------------------

AabbSphereHit AABBSphere(const AABB& box, const Vec3& centre, float radius)
{
    float dx = std::fmax(box.min.x - centre.x, 0.f);
    dx = std::fmax(dx, centre.x - box.max.x);
    float dy = std::fmax(box.min.y - centre.y, 0.f);
    dy = std::fmax(dy, centre.y - box.max.y);
    float dz = std::fmax(box.min.z - centre.z, 0.f);
    dz = std::fmax(dz, centre.z - box.max.z);
    float dist2 = dx*dx + dy*dy + dz*dz;
    return { dist2 <= radius*radius, dist2 };
}

// ---------------------------------------------------------------------------
// Segment vs AABB (Kay–Kajiya slab method)
// ---------------------------------------------------------------------------

SegAabbHit SegmentAABB(const Vec3& p0, const Vec3& p1, const AABB& box)
{
    constexpr float kEps = 1e-7f;
    float t_enter = 0.f, t_exit = 1.f;

    float dx[3] = { p1.x-p0.x, p1.y-p0.y, p1.z-p0.z };
    float ox[3] = { p0.x,       p0.y,       p0.z      };
    float bmin[3] = { box.min.x, box.min.y, box.min.z };
    float bmax[3] = { box.max.x, box.max.y, box.max.z };

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dx[i]) < kEps) {
            if (ox[i] < bmin[i] || ox[i] > bmax[i]) return {};
        } else {
            float inv = 1.f / dx[i];
            float t1 = (bmin[i] - ox[i]) * inv;
            float t2 = (bmax[i] - ox[i]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            t_enter = t_enter > t1 ? t_enter : t1;
            t_exit  = t_exit  < t2 ? t_exit  : t2;
            if (t_enter > t_exit) return {};
        }
    }
    if (t_exit < 0.f || t_enter > 1.f) return {};
    return { true, t_enter, t_exit };
}

}  // namespace madeline_cube::physics
