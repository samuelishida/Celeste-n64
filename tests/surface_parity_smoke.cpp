#include <cassert>
#include <cmath>

#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player/player_controller.hpp"
#include "../src/user/gameplay/player/player_movement.hpp"
#include "../src/user/gameplay/world/room_data.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

bool ApproxEq(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

// Host gravity constant matching MovementConfig::gravity.
constexpr float kGravity = 600.0f;
constexpr float kDt = 1.0f / 60.0f;

}  // namespace

int main() {
    // --- Legacy floor/jump regression: stored platform velocity adds to the
    //     jump frame. ---
    {
        Room room;
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-4.0f, 0.0f, -4.0f}, .max = {4.0f, 0.0f, 4.0f}},
            .solid = true,
            .normal = {0.0f, 1.0f, 0.0f},
        };
        const GroundHit floor = QueryFloorSource(room, {0.0f, 2.0f, 0.0f}, 4.0f);
        assert(floor.hit);
        assert(floor.normal.y == 1.0f);

        PlayerController controller;
        PlayerState jump;
        jump.grounded = true;
        jump.platform_carry.stored_velocity = {2.0f, 3.0f, 0.0f};
        jump.platform_carry.time_remaining = 0.1f;
        PlayerInput input;
        input.jump_pressed = true;
        input.jump_held = true;
        controller.Step(jump, input, {0.0f, 0.0f, 1.0f}, 1.0f / 60.0f);
        assert(jump.velocity.x >= 2.0f);
        assert(jump.velocity.y > 9.0f);
    }

    // --- Moving-platform rider: grounded surface ownership must be recorded,
    //     then platform displacement carries rider and stored velocity feeds
    //     the next jump. ---
    {
        Room room = BuildMovingPlatformFixtureRoom();
        PlayerController controller;
        PlayerState rider;
        rider.position = {0.0f, 31.25f, 0.0f};
        rider.grounded = true;
        rider.contact.was_grounded = true;
        const Vec3 camera_forward = {0.0f, 0.0f, 1.0f};
        const PlayerInput idle{};

        controller.Step(rider, idle, camera_forward, kDt, &room);
        assert(rider.contact.ground_surface_id == 200);

        room.moving_surfaces[1].position.x += 1.0f;
        AdvanceMovingSurfaces(room, kDt);
        controller.Step(rider, idle, camera_forward, kDt, &room);
        assert(ApproxEq(rider.position.x, 1.0f));
        assert(ApproxEq(rider.platform_carry.stored_velocity.x, 60.0f));
        assert(rider.platform_carry.time_remaining > 0.0f);

        AdvanceMovingSurfaces(room, kDt);
        PlayerInput jump;
        jump.jump_pressed = true;
        jump.jump_held = true;
        controller.Step(rider, jump, camera_forward, kDt, &room);
        assert(ApproxEq(rider.velocity.x, 60.0f));
        assert(rider.platform_carry.time_remaining == 0.0f);
    }

    // --- Slope normal: QueryFloorSource returns the actual slope plane normal,
    //     not the default (+Y). ---
    {
        const Room room = BuildSlopeFixtureRoom();
        const GroundHit hit = QueryFloorSource(room, {-2.5f, 8.0f, 0.0f}, 10.0f);
        assert(hit.hit);
        assert(ApproxEq(hit.normal.x, 0.70710678f));
        assert(ApproxEq(hit.normal.y, 0.70710678f));
        assert(ApproxEq(hit.normal.z, 0.0f));
        assert(ApproxEq(hit.point.y, 2.5f));
        assert(hit.face_id == 1);
    }

    // --- Slope landing: MoveService grounds the player on the slope with the
    //     slope normal recorded; downhill X velocity is preserved. ---
    {
        Room room = BuildSlopeFixtureRoom();
        MoveService mover;
        PlayerState p;
        p.position = {-2.5f, 14.0f, 0.0f};
        p.velocity = {2.0f, 0.0f, 0.0f};
        p.grounded = false;
        bool grounded = false;
        for (int i = 0; i < 240 && !grounded; ++i) {
            p.velocity.y -= kGravity * kDt;
            const Vec3 amount = {p.velocity.x * kDt, p.velocity.y * kDt, 0.0f};
            mover.SweepMove(p, room, amount, true);
            grounded = mover.GroundCheck(p, room, nullptr, nullptr);
            p.grounded = grounded;
            p.contact.was_grounded = grounded;
        }
        assert(grounded);
        assert(ApproxEq(p.contact.ground_normal.x, 0.70710678f, 0.05f));
        assert(ApproxEq(p.contact.ground_normal.y, 0.70710678f, 0.05f));
        assert(p.velocity.x > 0.0f);
    }

    // --- Ledge ground snap: was_grounded + snaps onto a lower floor via the
    //     cooldown-gated GroundSnap. ---
    {
        Room room = BuildLedgeFixtureRoom();
        MoveService mover;
        PlayerState p;
        // Feet 2 units above the lower floor (y=-10): feet at -8 -> center 2.
        p.position = {0.5f, 2.0f, 0.0f};
        p.grounded = false;
        p.contact.was_grounded = true;
        p.contact.ground_snap_cooldown_remaining = 0.0f;
        mover.GroundSnap(p, room, kDt);
        // Snapped feet onto the lower floor (y=-10) -> center y=0 exactly.
        assert(ApproxEq(mover.GroundCheck(p, room, nullptr, nullptr), true));
        assert(p.position.y == 0.0f);  // exact snap (review C5: no loose eps)
    }

    // --- Ledge no-snap when previously airborne: stays (was_grounded false). ---
    {
        Room room = BuildLedgeFixtureRoom();
        MoveService mover;
        PlayerState p;
        p.position = {0.5f, 12.0f, 0.0f};
        p.grounded = false;
        p.contact.was_grounded = false;
        p.contact.ground_snap_cooldown_remaining = 0.0f;
        const float before_y = p.position.y;
        mover.GroundSnap(p, room, kDt);
        assert(p.position.y == before_y);
        assert(!mover.GroundCheck(p, room, nullptr, nullptr));
    }

    return 0;
}
