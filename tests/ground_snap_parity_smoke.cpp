#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

#include "gameplay/player/player_movement.hpp"

using namespace madeline_cube;

namespace {

Room FloorRoom(float floor_y, float normal_y) {
    Room room{};
    Collider floor{};
    floor.type = ColliderType::Plane;
    floor.bounds.min = {-100.0f, floor_y - 100.0f, -100.0f};
    floor.bounds.max = {100.0f, floor_y + 100.0f, 100.0f};
    floor.solid = true;
    floor.normal = {std::sqrt(1.0f - normal_y * normal_y), normal_y, 0.0f};
    floor.plane_origin = {0.0f, floor_y, 0.0f};
    floor.has_plane_origin = true;
    floor.face_id = 17;
    room.colliders[0] = floor;
    room.collider_count = 1;
    return room;
}

bool SourceWouldSnap(bool previously_grounded, bool grounded, float cooldown,
                     float gap, float normal_y) {
    // OG Player.LateUpdate: prevOnGround && !onGround && tGroundSnapCooldown
    // <= 0, then a five-unit front-face ray and FloorNormalCheck.
    const bool front_face_ray_hit = normal_y > 0.0f && gap <= 5.0f;
    const bool floor_normal = normal_y >= 0.35f;
    return cooldown <= 0.0f && previously_grounded && !grounded &&
           front_face_ray_hit && floor_normal;
}

bool CheckCase(float gap, float normal_y, float cooldown,
               bool previously_grounded, bool currently_grounded) {
    MoveService mover;
    const Room room = FloorRoom(0.0f, normal_y);
    PlayerState state;
    state.position.y = mover.Config().half_height + gap;
    state.grounded = currently_grounded;
    state.contact.was_grounded = previously_grounded;
    state.contact.ground_snap_cooldown_remaining = cooldown;

    mover.GroundSnap(state, room, 1.0f / 60.0f);

    const bool snap_expected = SourceWouldSnap(
        previously_grounded, currently_grounded, cooldown, gap, normal_y);
    const bool grounded_expected = currently_grounded || snap_expected;
    const float feet_y = state.position.y - mover.Config().half_height;
    const float expected_feet_y = snap_expected ? 0.0f : gap;
    if (state.grounded != grounded_expected ||
        std::fabs(feet_y - expected_feet_y) > 1.0e-5f) {
        std::fprintf(stderr,
            "ground snap mismatch gap=%.9g normal_y=%.9g cooldown=%.9g "
            "prev=%u grounded=%u feet=%.9g expected=%u\n",
            gap, normal_y, cooldown, previously_grounded ? 1u : 0u,
            state.grounded ? 1u : 0u, feet_y, grounded_expected ? 1u : 0u);
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const float half_height = MoveService{}.Config().half_height;
    // Choose the next center-representable value above halfHeight+5; a ULP
    // added to the gap itself can disappear when converted back to center.
    const float beyond_five = std::nextafter(
        half_height + 5.0f, std::numeric_limits<float>::infinity()) - half_height;
    const float below_floor_threshold = std::nextafter(0.35f, 0.0f);
    const float positive_cooldown = std::nextafter(0.0f, 1.0f);

    assert(CheckCase(5.0f, 1.0f, 0.0f, true, false));
    assert(CheckCase(beyond_five, 1.0f, 0.0f, true, false));
    assert(CheckCase(2.0f, 0.35f, 0.0f, true, false));
    assert(CheckCase(2.0f, below_floor_threshold, 0.0f, true, false));
    assert(CheckCase(2.0f, 1.0f, positive_cooldown, true, false));
    assert(CheckCase(2.0f, 1.0f, 0.0f, false, false));
    assert(CheckCase(2.0f, 1.0f, 0.0f, true, true));

    std::puts("ground_snap_parity_smoke: PASS (5-unit boundary, normal threshold, cooldown, previous-ground gate)");
    return 0;
}
