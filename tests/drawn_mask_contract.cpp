// Host-side drawn-mask contract test (streaming-experience Inc 3, Pattern A:
// header-only, no N64 deps).
//
// Asserts the DRAWN set is exactly the 3×3 centered on the active cell
// (Chebyshev distance ≤ 1), independent of the 5×5 RESIDENT pool. Preloaded
// outer (cheb=2) cells are resident but never drawn — drawing them would move
// the visible boundary outward and re-import the screen-edge pop the
// `kEnableNearCulling` gate exists to avoid. Covers the 4-case corner:
// center / edge (cheb=1) / outer (cheb=2) / evicted (not resident).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/drawn_mask_contract.cpp
#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

int main() {
    // A 5×5 grid centered at (0,0): ix,iz ∈ [-2,2].
    MapSpecV2 spec;
    spec.room_count = 0;
    for (int iz = -2; iz <= 2; ++iz) {
        for (int ix = -2; ix <= 2; ++ix) {
            V2RoomSpec& r = spec.rooms[spec.room_count];
            std::snprintf(r.id, V2RoomSpec::kIdLen, "cell_%d_%d", ix, iz);
            r.cell_ix = ix;
            r.cell_iz = iz;
            ++spec.room_count;
        }
    }
    auto cell = [&](int ix, int iz) -> const V2RoomSpec* {
        for (int i = 0; i < spec.room_count; ++i)
            if (spec.rooms[i].cell_ix == ix && spec.rooms[i].cell_iz == iz)
                return &spec.rooms[i];
        return nullptr;
    };
    const V2RoomSpec* center = cell(0, 0);
    const V2RoomSpec* edge = cell(1, 0);    // cheb=1
    const V2RoomSpec* outer = cell(2, 0);   // cheb=2
    const V2RoomSpec* corner = cell(1, 1);  // cheb=1
    const V2RoomSpec* far = cell(2, 2);      // cheb=2
    assert(center && edge && outer && corner && far);

    // Center is always drawn.
    assert(IsDrawnCell(*center, *center));
    // Edge (cheb=1) is drawn.
    assert(IsDrawnCell(*edge, *center));
    assert(IsDrawnCell(*corner, *center));
    // Outer (cheb=2) is resident but NOT drawn.
    assert(!IsDrawnCell(*outer, *center));
    assert(!IsDrawnCell(*far, *center));
    printf("PASS: drawn mask = cheb<=1 (center/edge drawn, outer not)\n");

    // Symmetry: the mask is relative to the center, so a different center
    // shifts the drawn 3×3.
    assert(IsDrawnCell(*center, *edge));   // (0,0) is cheb=1 from (1,0)
    assert(IsDrawnCell(*outer, *edge));    // (2,0) is cheb=1 from (1,0)
    assert(!IsDrawnCell(*far, *edge));     // (2,2) is cheb=2 from (1,0)
    printf("PASS: drawn mask is center-relative\n");

    // The 5×5 resident ring resolves 25 cells; the drawn subset is exactly the
    // 9 cheb<=1 cells.
    {
        const V2RoomSpec* ring[kMaxResident] = {};
        const int n = ResolveForwardWedge(spec, *center, Vec3{0, 0, 0}, ring,
                                          kMaxResident, /*max_cheb=*/2);
        assert(n == 25);
        int drawn = 0;
        for (int i = 0; i < n; ++i)
            if (IsDrawnCell(*ring[i], *center)) ++drawn;
        assert(drawn == 9);  // exactly the 3×3
        printf("PASS: 5×5 resident ring draws exactly 9 (the 3×3)\n");
    }

    printf("ALL PASS\n");
    return 0;
}
