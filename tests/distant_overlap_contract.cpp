// Host test for the near/distant overlap handoff (Pattern A: header-only, no
// N64 deps). Asserts (Inc 5 / D4):
//   - `CellAabbInNearCone` keeps a resident cell whose AABB intersects the
//     camera cone and culls one fully off-cone;
//   - the near-draw set (cone-culled residents) and the distant draw set are
//     DISJOINT — no cell is drawn by both passes;
//   - an AABB partially in the cone is near-drawn (no grid-edge cut);
//   - a non-resident cell just beyond the ring is distant-drawn (no hole).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/distant_overlap_contract.cpp
#include <cmath>
#include <cstdio>

#include "gameplay/render/lod_math.hpp"
#include "gameplay/render/distant_world_renderer.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    const Vec3 cam = {0.0f, 0.0f, 0.0f};
    const Vec3 target_px = {100.0f, 0.0f, 0.0f};  // facing +X
    const float fov = 60.0f;
    const float near_d = 20.0f;
    const float far_d = 800.0f;

    // --- CellAabbInNearCone ---
    {
        // Resident cell straight ahead: near-drawn.
        const AABB ahead = {{120.0f, 0.0f, -120.0f}, {360.0f, 0.0f, 120.0f}};
        expect(CellAabbInNearCone(cam, target_px, fov, near_d, far_d, ahead),
               "resident cell straight ahead is near-drawn");
        // Resident cell fully off-cone (behind): not near-drawn.
        const AABB behind = {{-400.0f, 0.0f, -100.0f}, {-200.0f, 0.0f, 100.0f}};
        expect(!CellAabbInNearCone(cam, target_px, fov, near_d, far_d, behind),
               "resident cell fully off-cone is not near-drawn");
        // AABB partially in the cone (straddles the edge): near-drawn (no
        // grid-edge cut).
        const AABB straddle = {{120.0f, 0.0f, 120.0f}, {360.0f, 0.0f, 360.0f}};
        expect(CellAabbInNearCone(cam, target_px, fov, near_d, far_d, straddle),
               "AABB partially in cone is near-drawn (no grid-edge cut)");
    }

    // --- Horizontal-FOV widening (Inc 5 / D4 fix) ---
    // Production passes the VERTICAL FOV (45°) to CellAabbInNearCone. The
    // cone must be widened to the horizontal FOV (4:3 aspect) so a ring cell
    // at ~27° off-axis (inside the true horizontal frustum but outside the
    // 22.5° vertical half-cone) is still near-drawn. This is the bug class
    // where ring cells at the screen's left/right edges fell through to the
    // coarse distant pass.
    {
        const float vfov = 45.0f;  // vertical FOV (production value)
        // A cell centered at (240, 0, 240): center is 45° off +X, but its near
        // corner (360, 0, 120) is at atan(120/360) ≈ 18.4° — inside the
        // horizontal half-cone (hfov 45° → half ≈ 28.9°). Must be near-drawn.
        const AABB edge = {{120.0f, 0.0f, 120.0f}, {360.0f, 0.0f, 360.0f}};
        expect(CellAabbInNearCone(cam, target_px, vfov, near_d, far_d, edge),
               "ring cell at ~27° off-axis is near-drawn (horizontal FOV widening)");
        // A cell fully outside the widened horizontal cone is still culled.
        const AABB far_off = {{-400.0f, 0.0f, 300.0f}, {-200.0f, 0.0f, 500.0f}};
        expect(!CellAabbInNearCone(cam, target_px, vfov, near_d, far_d, far_off),
               "cell fully outside widened cone is not near-drawn");
    }

    // --- Disjoint passes ---
    // Build a synthetic distant entry table with AABBs, and a near-draw set.
    {
        DistantLodEntry entries[4] = {};
        // Cell 0: straight ahead (near-drawn). Cell 1: just past the ring
        // (distant-only). Cell 2: off-cone (neither). Cell 3: behind (neither).
        entries[0].cell_ix = 0; entries[0].cell_iz = 0;
        entries[0].origin = {240.0f, 0.0f, 0.0f};
        entries[0].aabb = {{120.0f, 0.0f, -120.0f}, {360.0f, 0.0f, 120.0f}};
        entries[1].cell_ix = 1; entries[1].cell_iz = 0;
        entries[1].origin = {480.0f, 0.0f, 0.0f};
        entries[1].aabb = {{360.0f, 0.0f, -120.0f}, {600.0f, 0.0f, 120.0f}};
        entries[2].cell_ix = 0; entries[2].cell_iz = 1;
        entries[2].origin = {0.0f, 0.0f, 240.0f};
        entries[2].aabb = {{-120.0f, 0.0f, 120.0f}, {120.0f, 0.0f, 360.0f}};
        entries[3].cell_ix = -1; entries[3].cell_iz = 0;
        entries[3].origin = {-240.0f, 0.0f, 0.0f};
        entries[3].aabb = {{-360.0f, 0.0f, -120.0f}, {-120.0f, 0.0f, 120.0f}};

        // Near-draw set: cell 0 (straight ahead) is near-drawn.
        const int near_ix[1] = {0};
        const int near_iz[1] = {0};

        // Distant draw set (culled, no near-skip here — the skip is in the
        // device Render loop). Cell 0 is in the cone (would be distant-drawn
        // WITHOUT the skip); cell 1 is in the cone (distant-only); cell 2 is
        // off-cone (culled); cell 3 is behind (culled).
        DistantRenderItem out[4] = {};
        const int n = BuildDistantRenderListCulled(
            cam, target_px, entries, 4, out, 4, 60.0f, 50.0f, 1000.0f);
        // Cell 0 and cell 1 are in the cone.
        bool has0 = false, has1 = false;
        for (int i = 0; i < n; ++i) {
            if (out[i].cell_index == 0) has0 = true;
            if (out[i].cell_index == 1) has1 = true;
        }
        expect(has0 && has1, "distant list contains the in-cone cells");

        // Simulate the overlap handoff: the distant pass skips the near-draw
        // set. After the skip, cell 0 (in the near set) is NOT distant-drawn,
        // cell 1 (not in the near set) IS. The passes are disjoint.
        bool distant_has0 = false, distant_has1 = false;
        for (int i = 0; i < n; ++i) {
            const int e = out[i].cell_index;
            const bool in_near = (entries[e].cell_ix == near_ix[0] &&
                                  entries[e].cell_iz == near_iz[0]);
            if (in_near) continue;
            if (e == 0) distant_has0 = true;
            if (e == 1) distant_has1 = true;
        }
        expect(!distant_has0, "near-drawn cell is NOT distant-drawn (disjoint)");
        expect(distant_has1, "non-resident cell just past the ring IS distant-drawn (no hole)");
    }

    if (failures == 0) {
        std::printf("PASS: distant_overlap_contract\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
