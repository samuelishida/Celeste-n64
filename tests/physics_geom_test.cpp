#include <cassert>
#include <cmath>

#include "../src/user/gameplay/physics/geom.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool NearVec(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

// -------- triangle fixture --------
// CCW floor triangle in XZ plane at y=0: a(0,0,0), b(4,0,0), c(0,0,4)
constexpr Vec3 TA = {0.f, 0.f, 0.f};
constexpr Vec3 TB = {4.f, 0.f, 0.f};
constexpr Vec3 TC = {0.f, 0.f, 4.f};

}  // namespace

// ============================================================
// 1. SegmentTriangle (Möller–Trumbore)
// ============================================================

static void test_segment_tri_hit_centre() {
    // Shoot straight down through centre of triangle
    auto h = SegmentTriangle({1.f,1.f,1.f}, {1.f,-1.f,1.f}, TA, TB, TC);
    assert(h.hit);
    assert(Near(h.t, 0.5f));
    assert(NearVec(h.point, {1.f, 0.f, 1.f}));
}

static void test_segment_tri_miss_beside() {
    // Shoot outside the triangle boundary
    auto h = SegmentTriangle({5.f,1.f,5.f}, {5.f,-1.f,5.f}, TA, TB, TC);
    assert(!h.hit);
}

static void test_segment_tri_parallel_miss() {
    // Segment parallel to the triangle plane
    auto h = SegmentTriangle({1.f,0.f,1.f}, {2.f,0.f,1.f}, TA, TB, TC);
    assert(!h.hit);
}

static void test_segment_tri_backface_ignore() {
    // Triangle (TA,TB,TC) has face normal pointing -Y (frontface is bottom).
    // Shoot from ABOVE downward: hits the backface (+Y side).
    // Ignore mode should still register the hit.
    auto h = SegmentTriangle({1.f,1.f,1.f}, {1.f,-1.f,1.f}, TA, TB, TC,
                              BackfaceCull::Ignore);
    assert(h.hit);
}

static void test_segment_tri_backface_cull() {
    // Same ray from above downward = backface hit.
    // Cull mode must miss.
    auto h = SegmentTriangle({1.f,1.f,1.f}, {1.f,-1.f,1.f}, TA, TB, TC,
                              BackfaceCull::Cull);
    assert(!h.hit);
}

static void test_segment_tri_edge_contact() {
    // Shoot at the midpoint of edge AB (y=0 plane, within [0,1] segment)
    // Segment from (2,1,0) down to (2,-1,0): should hit edge AB at t=0.5
    auto h = SegmentTriangle({2.f,1.f,0.f}, {2.f,-1.f,0.f}, TA, TB, TC);
    assert(h.hit);
    assert(NearVec(h.point, {2.f, 0.f, 0.f}, 1e-3f));
}

static void test_segment_tri_vertex_contact() {
    // Shoot directly at vertex A
    auto h = SegmentTriangle({0.f,1.f,0.f}, {0.f,-1.f,0.f}, TA, TB, TC);
    assert(h.hit);
    assert(NearVec(h.point, {0.f, 0.f, 0.f}, 1e-3f));
}

static void test_segment_tri_outside_segment() {
    // t would be 2.0 — outside segment [0,1]; must not report hit
    auto h = SegmentTriangle({1.f,3.f,1.f}, {1.f,2.f,1.f}, TA, TB, TC);
    assert(!h.hit);
}

// ============================================================
// 2. SphereTriangle (Ericson §5.2.7)
// ============================================================

static void test_sphere_tri_interior_hit() {
    // Sphere at (1,0.4,1) radius 0.5 — interior region
    auto h = SphereTriangle({1.f, 0.4f, 1.f}, 0.5f, TA, TB, TC);
    assert(h.hit);
    assert(Near(h.dist, 0.4f, 1e-3f));
    assert(NearVec(h.closest, {1.f, 0.f, 1.f}, 1e-3f));
    // Normal should point upward (from triangle toward sphere centre)
    assert(h.normal.y > 0.9f);
}

static void test_sphere_tri_interior_miss() {
    // Sphere 0.6 above triangle, radius 0.5 — miss
    auto h = SphereTriangle({1.f, 0.6f, 1.f}, 0.5f, TA, TB, TC);
    assert(!h.hit);
}

static void test_sphere_tri_vertex_hit() {
    // Sphere near vertex A
    auto h = SphereTriangle({-0.3f, 0.f, -0.3f}, 0.5f, TA, TB, TC);
    assert(h.hit);
}

static void test_sphere_tri_edge_hit() {
    // Sphere near midpoint of edge AB, slightly above
    auto h = SphereTriangle({2.f, 0.3f, -0.1f}, 0.4f, TA, TB, TC);
    assert(h.hit);
}

static void test_sphere_tri_miss_far() {
    // Sphere far from triangle
    auto h = SphereTriangle({10.f, 10.f, 10.f}, 1.0f, TA, TB, TC);
    assert(!h.hit);
}

// ============================================================
// 3. AABB vs AABB
// ============================================================

static void test_aabb_overlap_hit() {
    AABB a = {{0,0,0},{2,2,2}};
    AABB b = {{1,1,1},{3,3,3}};
    assert(AABBOverlap(a, b));
}

static void test_aabb_overlap_miss_x() {
    AABB a = {{0,0,0},{1,1,1}};
    AABB b = {{2,0,0},{3,1,1}};
    assert(!AABBOverlap(a, b));
}

static void test_aabb_overlap_touching_edge() {
    AABB a = {{0,0,0},{1,1,1}};
    AABB b = {{1,0,0},{2,1,1}};
    assert(AABBOverlap(a, b));  // touching counts as overlap
}

// ============================================================
// 4. AABB vs sphere
// ============================================================

static void test_aabb_sphere_hit_inside() {
    AABB box = {{-1,-1,-1},{1,1,1}};
    auto h = AABBSphere(box, {0,0,0}, 0.5f);
    assert(h.hit);
    assert(Near(h.dist2, 0.f));
}

static void test_aabb_sphere_hit_outside_nearby() {
    AABB box = {{-1,-1,-1},{1,1,1}};
    auto h = AABBSphere(box, {2.f, 0.f, 0.f}, 1.5f);
    assert(h.hit);
}

static void test_aabb_sphere_miss() {
    AABB box = {{-1,-1,-1},{1,1,1}};
    auto h = AABBSphere(box, {3.f, 0.f, 0.f}, 1.f);
    assert(!h.hit);
}

static void test_aabb_sphere_miss_corner() {
    // Sphere just beyond corner (1,1,1) of unit AABB
    AABB box = {{0,0,0},{1,1,1}};
    // distance from (2,2,2) to corner (1,1,1) = sqrt(3) ≈ 1.732
    auto h = AABBSphere(box, {2.f, 2.f, 2.f}, 1.f);
    assert(!h.hit);
}

// ============================================================
// 5. Segment vs AABB (slab method)
// ============================================================

static void test_seg_aabb_hit_through() {
    AABB box = {{-1,-1,-1},{1,1,1}};
    auto h = SegmentAABB({-2.f,0.f,0.f}, {2.f,0.f,0.f}, box);
    assert(h.hit);
    assert(Near(h.t_enter, 0.25f));
    assert(Near(h.t_exit,  0.75f));
}

static void test_seg_aabb_miss_parallel() {
    AABB box = {{-1,-1,-1},{1,1,1}};
    auto h = SegmentAABB({2.f,-2.f,0.f}, {2.f,2.f,0.f}, box);
    assert(!h.hit);
}

static void test_seg_aabb_miss_short() {
    // Segment ends before reaching box
    AABB box = {{3.f,-1,-1},{5.f,1,1}};
    auto h = SegmentAABB({0.f,0.f,0.f}, {2.f,0.f,0.f}, box);
    assert(!h.hit);
}

static void test_seg_aabb_hit_inside() {
    // Origin inside box
    AABB box = {{-5,-5,-5},{5,5,5}};
    auto h = SegmentAABB({0.f,0.f,0.f}, {10.f,0.f,0.f}, box);
    assert(h.hit);
    assert(h.t_enter <= 0.f);
}

static void test_seg_aabb_oblique() {
    AABB box = {{0,0,0},{1,1,1}};
    auto h = SegmentAABB({-1.f,-1.f,0.5f},{2.f,2.f,0.5f}, box);
    assert(h.hit);
}

// ============================================================
// main
// ============================================================

int main() {
    test_segment_tri_hit_centre();
    test_segment_tri_miss_beside();
    test_segment_tri_parallel_miss();
    test_segment_tri_backface_ignore();
    test_segment_tri_backface_cull();
    test_segment_tri_edge_contact();
    test_segment_tri_vertex_contact();
    test_segment_tri_outside_segment();

    test_sphere_tri_interior_hit();
    test_sphere_tri_interior_miss();
    test_sphere_tri_vertex_hit();
    test_sphere_tri_edge_hit();
    test_sphere_tri_miss_far();

    test_aabb_overlap_hit();
    test_aabb_overlap_miss_x();
    test_aabb_overlap_touching_edge();

    test_aabb_sphere_hit_inside();
    test_aabb_sphere_hit_outside_nearby();
    test_aabb_sphere_miss();
    test_aabb_sphere_miss_corner();

    test_seg_aabb_hit_through();
    test_seg_aabb_miss_parallel();
    test_seg_aabb_miss_short();
    test_seg_aabb_hit_inside();
    test_seg_aabb_oblique();

    return 0;
}
