// Host-side reveal state-machine + matrix-formula contract test
// (streaming-experience Inc 4, Pattern A: header-only, no N64 deps).
//
// Mirrors the per-resident reveal logic in `TileStreamer::UpdateCamera` and
// the pivot-scale matrix formula in `TexturedRoomRenderer::BuildRevealMatrix`:
//
//   - preloaded (READY before first drawn frame) → instant (scale 1.0)
//   - enters drawn 3×3 while renderer slot null → starts revealing from
//     kRevealEpsilon, advances toward 1.0 over kRevealDurationSec
//   - late-land (ParseStaged lands while already drawn) → animates
//   - leaves drawn set → reset to 1.0 (fresh on re-entry)
//   - gate OFF (kEnableCellReveal=false) → always 1.0
//
// Matrix formula (matches BuildRevealMatrix exactly, in the renderer's local
// "around render origin" frame). The model matrix is
//   M'(L) = S·L + (o − cam)
// where L = w − o (the LVL2 writer stores verts relative to the per-cell
// render origin, pre-scaled), S = (kInvScale·s)·B (B the cell basis; kInvScale
// folded into this test's unit B), and (o − cam) is the CAMERA-relative
// translation. The translation is invariant in `s`, so the pivot (the vertex
// that never moves) is the render origin o itself: M'(L=0) = o − cam for any
// s, and a cell's extent shrinks by s about o.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/reveal_state_machine.cpp
#include <cassert>
#include <cstdio>

#include "gameplay/debug_flags.hpp"
#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

namespace {

// Mirror of the reveal state machine in `TileStreamer::UpdateCamera`.
struct RevealState {
    float scale = 1.0f;
    bool revealing = false;

    // One frame of the state machine. `drawn` = cell in the drawn 3×3;
    // `ready` = renderer slot non-null. `dt` in seconds.
    void Step(bool drawn, bool ready, float dt,
              bool enabled = kEnableCellReveal) {
        if (!enabled) {
            scale = 1.0f;
            revealing = false;
            return;
        }
        if (drawn) {
            if (!ready) {
                // Late load: (re)start at epsilon; advances once ready.
                revealing = true;
                scale = kRevealEpsilon;
            } else if (revealing) {
                scale += dt / kRevealDurationSec;
                if (scale >= 1.0f) {
                    scale = 1.0f;
                    revealing = false;
                }
            }
            // else: ready & not revealing & scale==1 → instant, leave.
        } else {
            revealing = false;
            scale = 1.0f;
        }
    }
};

// Mirror of the pivot-scale matrix formula (BuildRevealMatrix): M'(L) = s·B·L
// + (o − cam). `L` is the local offset around the render origin (what the
// LVL2 vertices hold); bx/by/bz are the basis ROWS, exactly as tiny3d's
// t3d_mat4_from_srt_euler lays the matrix out (row 0 = bx, row 1 = by, row 2
// = gz), so each output coordinate is a dot product of one basis row with L.
Vec3 RevealTransform(const Vec3& L, const Vec3& o, const Vec3& cam,
                     const Vec3& bx, const Vec3& by, const Vec3& bz,
                     float s) {
    const float bxl = bx.x * L.x + bx.y * L.y + bx.z * L.z;
    const float byl = by.x * L.x + by.y * L.y + by.z * L.z;
    const float bzl = bz.x * L.x + bz.y * L.y + bz.z * L.z;
    const Vec3 d{ o.x - cam.x, o.y - cam.y, o.z - cam.z };
    return Vec3{s * bxl + d.x, s * byl + d.y, s * bzl + d.z};
}

}  // namespace

int main() {
    const float dt = 1.0f / 30.0f;

    // --- State machine ---
    {
        // Preloaded: READY before first drawn frame → instant (never reveals).
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/true, dt, /*enabled=*/true);
        assert(s.scale == 1.0f && !s.revealing);
        printf("PASS: preloaded (ready before drawn) is instant\n");
    }
    {
        // Enters drawn while null → starts revealing from epsilon.
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/false, dt, /*enabled=*/true);
        assert(s.revealing && s.scale == kRevealEpsilon);
        // Advances toward 1.0 once ready.
        s.Step(/*drawn=*/true, /*ready=*/true, dt, /*enabled=*/true);
        assert(s.scale > kRevealEpsilon && s.scale < 1.0f);
        // Completes after enough frames.
        int frames = 0;
        while (s.revealing && frames < 1000) {
            s.Step(/*drawn=*/true, /*ready=*/true, dt, /*enabled=*/true);
            ++frames;
        }
        assert(!s.revealing && s.scale == 1.0f);
        printf("PASS: drawn-while-null animates epsilon→1 over ~0.4s\n");
    }
    {
        // Late-land: cell was drawn+null, then lands (ready) → animates.
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/false, dt, /*enabled=*/true);
        s.Step(/*drawn=*/true, /*ready=*/true, dt, /*enabled=*/true);
        assert(s.revealing && s.scale > kRevealEpsilon);
        printf("PASS: late-land while drawn animates\n");
    }
    {
        // Leaves drawn set → reset to 1.0 (fresh on re-entry).
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/false, dt, /*enabled=*/true);
        assert(s.revealing);
        s.Step(/*drawn=*/false, /*ready=*/true, dt, /*enabled=*/true);
        assert(!s.revealing && s.scale == 1.0f);
        printf("PASS: leaving drawn set resets reveal\n");
    }
    {
        // An explicitly disabled gate must never scale loaded terrain.
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/false, dt, /*enabled=*/false);
        assert(s.scale == 1.0f && !s.revealing);
        printf("PASS: disabled reveal keeps terrain scale at 1.0\n");
    }
    {
        // Production default follows the project flag: no scale transition on
        // late cell arrival, the artifact reported in the Ares screenshots.
        RevealState s;
        s.Step(/*drawn=*/true, /*ready=*/false, dt);
        assert(!kEnableCellReveal);
        assert(s.scale == 1.0f && !s.revealing);
        printf("PASS: late cell arrival does not warp terrain by default\n");
    }

    // --- Matrix formula (M'(L) = s·B·L + (o − cam); pivot at the render
    // origin, translation invariant in s) ---
    {
        // Identity unit basis, a plausible cell origin, near camera.
        const Vec3 o{120.0f, -45.0f, 330.0f};   // cell render origin
        const Vec3 cam{100.0f, -44.0f, 315.0f}; // the near camera
        const Vec3 bx{1, 0, 0}, by{0, 1, 0}, bz{0, 0, 1};
        const Vec3 d{ o.x - cam.x, o.y - cam.y, o.z - cam.z };  // (20, -1, 15)

        // s=1.0 → M'(L) = B·L + (o − cam): today's matrix exactly.
        const Vec3 L{2.0f, 4.0f, 6.0f};
        Vec3 r1 = RevealTransform(L, o, cam, bx, by, bz, 1.0f);
        assert(r1.x == d.x + L.x && r1.y == d.y + L.y && r1.z == d.z + L.z);
        printf("PASS: s=1.0 equals today's matrix (identity basis)\n");

        // The PIVOT: the render-origin vertex (L = 0) maps to (o − cam) for
        // EVERY s — the translation is invariant in s.
        for (const float s : {1.0f, 0.5f, kRevealEpsilon}) {
            Vec3 rp = RevealTransform(Vec3{0, 0, 0}, o, cam, bx, by, bz, s);
            assert(rp.x == d.x && rp.y == d.y && rp.z == d.z);
        }
        printf("PASS: pivot fixed at the render origin for all s\n");

        // s=0.5 → a vertex offset by +X from the origin shrinks to half its
        // offset toward the origin (the cell scales in about its own center).
        Vec3 rox = RevealTransform(Vec3{1.0f, 0.0f, 0.0f}, o, cam, bx, by, bz, 0.5f);
        assert(rox.x == 0.5f + d.x && rox.y == d.y && rox.z == d.z);
        printf("PASS: s=0.5 scales extent about the origin (o+X → o+X/2)\n");

        // Non-identity unit basis (90° rotation): verify the full formula
        // M'(L) = s·B·L + (o − cam) with rows = (gx, gy, gz). Under the
        // tiny3d row layout (+X → −Y → +X rotation), B·L = (gx·L, gy·L, gz·L)
        // = (4, −2, 6) for L = (2, 4, 6).
        const Vec3 gx{0, 1, 0}, gy{-1, 0, 0}, gz{0, 0, 1};
        Vec3 gv = RevealTransform(L, o, cam, gx, gy, gz, 0.5f);
        const float ex = 0.5f * 4.0f + d.x;
        const float ey = 0.5f * -2.0f + d.y;
        const float ez = 0.5f * 6.0f + d.z;
        assert(gv.x == ex && gv.y == ey && gv.z == ez);
        printf("PASS: general formula holds for non-identity basis\n");
    }

    printf("ALL PASS\n");
    return 0;
}
