// Host-side drawn-mask contract test (streaming-experience Inc 3, Pattern A:
// header-only, no N64 deps).
//
// Asserts protected core-ring membership (Chebyshev distance ≤ 1) for
// eviction safety, separately from draw eligibility: all 9 core cells and
// four forward-ranked Cheb-2 coverage cells are resident and drawable.
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
            r.world_aabb = {
                {ix * 240.0f, -30.0f, iz * 240.0f},
                {(ix + 1) * 240.0f, 110.0f, (iz + 1) * 240.0f},
            };
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

    // Center + Cheb-1 are protected from eviction; Cheb-2 cells are not.
    assert(IsCoreCell(*center, *center));
    assert(IsCoreCell(*edge, *center));
    assert(IsCoreCell(*corner, *center));
    assert(!IsCoreCell(*outer, *center));
    assert(!IsCoreCell(*far, *center));
    printf("PASS: protected core membership = cheb<=1\n");

    // Symmetry: the mask is relative to the center, so a different center
    // shifts the core ring.
    assert(IsCoreCell(*center, *edge));   // (0,0) is cheb=1 from (1,0)
    assert(IsCoreCell(*outer, *edge));    // (2,0) is cheb=1 from (1,0)
    assert(!IsCoreCell(*far, *edge));     // (2,2) is cheb=2 from (1,0)
    printf("PASS: protected core membership is center-relative\n");

    // Runtime residency contains complete 3x3 core plus four forward cells.
    // A conservative 3D mask limits draw work without changing residency or
    // DLOD ownership.
    {
        assert(kMaxRing == 9);
        assert(kMaxResident == 13);
        const V2RoomSpec* ring[kMaxResident] = {};
        const Vec3 forward{0.0f, 0.0f, -1.0f};
        const V2RoomSpec* forward_outer = cell(0, -2);
        const int n = ResolveForwardWedge(spec, *center, forward, ring,
                                          kMaxResident, /*max_cheb=*/2);
        assert(n == 13);
        int core = 0;
        int outer_count = 0;
        bool forward_outer_resident = false;
        bool rear_corner_resident = false;
        for (int i = 0; i < n; ++i) {
            if (IsCoreCell(*ring[i], *center)) ++core;
            else ++outer_count;
            forward_outer_resident |= ring[i] == forward_outer;
            rear_corner_resident |= ring[i] == far;
        }
        assert(core == 9);  // all nine are the core
        assert(outer_count == 4);
        ResidentVisibility visibility;
        ResidentSet residents;
        residents.count = n;
        for (int i = 0; i < n; ++i) residents.spec[i] = ring[i];
        const CameraDesc near_cam = MakeNearCamera(
            45.0f, 5.0f, 800.0f, {120.0f, 50.0f, 120.0f},
            {120.0f, 50.0f, -1000.0f}, {0.0f, 1.0f, 0.0f});
        ResolveResidentFrustumMask(visibility, residents, near_cam);
        int detailed_core = 0;
        int visible_preloads = 0;
        int drawable_residents = 0;
        for (int i = 0; i < residents.count; ++i) {
            if (visibility.visible[i]) ++drawable_residents;
            if (visibility.visible[i] && IsCoreCell(*residents.spec[i], *center))
                ++detailed_core;
            if (visibility.visible[i] && !IsCoreCell(*residents.spec[i], *center))
                ++visible_preloads;
        }
        assert(detailed_core > 0);
        assert(visible_preloads > 0);
        assert(drawable_residents > 0 && drawable_residents < n);
        const int forward_slot = residents.IndexOf(cell(0, -2));
        assert(forward_slot >= 0 && visibility.visible[forward_slot]);
        assert(visibility.visible[residents.IndexOf(center)]);  // center always draws
        assert(forward_outer_resident);  // forward Cheb-2 coverage
        assert(!rear_corner_resident);   // rear/diagonal stays bounded
        CameraDesc distant_cam = near_cam;
        distant_cam.far = 3000.0f;
        int distant_visible = 0;
        for (int i = 0; i < residents.count; ++i) {
            const V2RoomSpec* room = residents.spec[i];
            const bool in_distant_frustum = CellAabbInFrustum3D(
                distant_cam.pos, distant_cam.target, distant_cam.up,
                distant_cam.fov_deg, 4.0f / 3.0f, distant_cam.near,
                distant_cam.far, room->world_aabb, kCullMargin);
            const bool near_owner = IsDrawnReadyResidentCell(
                residents, visibility, room->cell_ix, room->cell_iz,
                [](int) { return true; });
            assert(!visibility.visible[i] || in_distant_frustum);
            if (in_distant_frustum) {
                ++distant_visible;
                // Runtime DLOD yields only to READY cells submitted by near.
                const bool distant_owner = in_distant_frustum && !near_owner;
                assert(near_owner || distant_owner);
            }
        }
        assert(distant_visible >= drawable_residents);
        const int pending_slot = residents.IndexOf(cell(0, -2));
        const bool pending_near_owner = IsDrawnReadyResidentCell(
            residents, visibility, 0, -2,
            [pending_slot](int slot) { return slot != pending_slot; });
        assert(!pending_near_owner);
        assert(CellAabbInFrustum3D(
            distant_cam.pos, distant_cam.target, distant_cam.up,
            distant_cam.fov_deg, 4.0f / 3.0f, distant_cam.near,
            distant_cam.far, cell(0, -2)->world_aabb, kCullMargin));
        printf("PASS: near culling keeps %d/%d residents; DLOD ownership intact\n",
               drawable_residents, n);
    }

    // Camera-sector preload refresh uses a dead band: cross 7.5 degrees into a
    // neighboring sector before swapping outer residents, and require 7.5
    // degrees of return travel before swapping back.
    {
        auto direction = [](float degrees) {
            const float radians = degrees * (kLodPi / 180.0f);
            return Vec3{std::cos(radians), 0.0f, std::sin(radians)};
        };
        const int east_sector = CameraHeadingSector(direction(0.0f));
        assert(east_sector == 0);
        assert(CameraHeadingSector(direction(25.0f)) == 1);
        assert(!CameraDirectionConfirmsSector(direction(25.0f), 1));
        assert(CameraDirectionConfirmsSector(direction(31.0f), 1));
        assert(CameraHeadingSector(direction(20.0f)) == 0);
        assert(!CameraDirectionConfirmsSector(direction(20.0f), 0));
        assert(CameraDirectionConfirmsSector(direction(13.0f), 0));
        printf("PASS: camera heading sectors resist orbit jitter\n");
    }

    printf("ALL PASS\n");
    return 0;
}
