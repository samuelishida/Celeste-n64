#include <cassert>
#include <cmath>

#include "../src/user/gameplay/physics_contracts.hpp"
#include "../src/user/gameplay/player_controller.hpp"
#include "../src/user/gameplay/player_motor.hpp"
#include "../src/user/gameplay/room_data.hpp"
#include "../src/user/gameplay/world.hpp"

using namespace madeline_cube;

namespace {

bool ApproxEq(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
    // --- Legacy floor/jump regression: keep the original surface_parity_smoke
    //     expectation that stored platform velocity adds to the jump frame. ---
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

    // --- Slope normal: QueryFloorSource returns the actual slope plane normal,
    //     not the default (+Y).  Required so the controller (Inc 5) can react
    //     to slope angle via state.contact.ground_normal. ---
    {
        const Room room = BuildSlopeFixtureRoom();
        // Ray straight down from above the middle of the slope (x=-2.5).
        const GroundHit hit = QueryFloorSource(room, {-2.5f, 8.0f, 0.0f}, 10.0f);
        assert(hit.hit);
        // Slope normal should be (sqrt(.5), sqrt(.5), 0).
        assert(ApproxEq(hit.normal.x, 0.70710678f));
        assert(ApproxEq(hit.normal.y, 0.70710678f));
        assert(ApproxEq(hit.normal.z, 0.0f));
        // Slope rises from y=0 at x=0 to y=5 at x=-5, so at x=-2.5 -> y=2.5.
        assert(ApproxEq(hit.point.y, 2.5f));
        assert(hit.face_id == 1);
    }

    // --- Slope acceleration / motor preserves ground_normal so the
    //     controller can read it.  A player landing on the slope ends up
    //     grounded with the slope normal recorded; downhill X velocity is
    //     preserved through RemoveIntoNormal (tangential component survives).
    {
        Room room = BuildSlopeFixtureRoom();
        PlayerMotor motor;
        PlayerState p;
        // Player descending onto the slope at x=-2.5, y above the slope face.
        p.position = {-2.5f, 4.0f, 0.0f};
        MotorInput in;
        in.requested_velocity = {2.0f, -10.0f, 0.0f};  // moving downhill (+X) and falling.
        in.wants_ground_snap = false;
        const MotorResult r = motor.Step(p, room, in, 0.1f);
        assert(r.grounded);
        assert(p.grounded);
        // Motor must preserve the slope normal in MotorResult/contact.
        assert(ApproxEq(r.ground_normal.x, 0.70710678f));
        assert(ApproxEq(r.ground_normal.y, 0.70710678f));
        assert(ApproxEq(p.contact.ground_normal.x, 0.70710678f));
        // Downhill X velocity is preserved (positive +X).  Y velocity zeroed.
        assert(p.velocity.x > 0.0f);
        assert(ApproxEq(p.velocity.y, 0.0f));
    }

    // --- Ledge ground snap over short drop: was_grounded, walks off edge
    //     onto the lower floor 1 unit below; motor snaps to it. ---
    {
        Room room = BuildLedgeFixtureRoom();
        PlayerMotor motor;
        PlayerState p;
        // Stand just past the ledge: x=0.5 (over the lower floor), y still at
        // upper-floor height + half-height (= 1.0).  was_grounded triggers snap.
        p.position = {0.5f, 1.0f, 0.0f};
        p.grounded = true;
        p.contact.was_grounded = true;
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        const MotorResult r = motor.RefreshContacts(p, room, in);
        assert(r.grounded);
        // Lower floor at y=-1 + half_height(1.0) = 0.0.
        assert(ApproxEq(p.position.y, 0.0f));
        assert(r.ground_face_id == 1);
    }

    // --- Ledge no-snap when previously airborne: should NOT snap, falls. ---
    {
        Room room = BuildLedgeFixtureRoom();
        PlayerMotor motor;
        PlayerState p;
        p.position = {0.5f, 1.0f, 0.0f};
        p.grounded = false;
        p.contact.was_grounded = false;
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        const MotorResult r = motor.RefreshContacts(p, room, in);
        assert(!r.grounded);
        // Position unchanged (no snap, no motion this frame).
        assert(ApproxEq(p.position.y, 1.0f));
    }

    // --- Rider displacement: a moving platform displacement carries the
    //     grounded player.  The motor itself does not add displacement; the
    //     collider moves under the player via AdvanceMovingSurfaces and the
    //     motor's ground capture pins the player on top.
    {
        Room room = BuildMovingPlatformFixtureRoom();
        PlayerMotor motor;

        // Place the player exactly on top of the platform (platform at y=2,
        // half-height=1 -> player.y=3) over the platform center.
        PlayerState p;
        p.position = {0.0f, 3.0f, 0.0f};
        p.grounded = true;
        p.contact.was_grounded = true;

        // Move the platform +X by 0.5 over one tick.
        MovingSurface* platform = FindMovingSurfaceMutable(room, 200);
        assert(platform != nullptr);
        platform->position.x += 0.5f;
        AdvanceMovingSurfaces(room, 1.0f / 60.0f);

        // After AdvanceMovingSurfaces the collider has moved +X by 0.5.  The
        // player ground-snaps to the moved collider, so post-motor X is the
        // platform's new X.  (The motor does not add lateral displacement
        // itself -- it captures the moved floor's owner_velocity.)
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        const MotorResult r = motor.Step(p, room, in, 1.0f / 60.0f);
        assert(r.grounded);
        assert(ApproxEq(p.position.y, 3.0f));
        // Stored platform velocity should be the platform's rider velocity.
        assert(ApproxEq(p.platform_carry.stored_velocity.x, 0.5f * 60.0f));
        assert(p.platform_carry.time_remaining > 0.0f);
    }

    // --- Stored platform velocity preserved across a tick where the player
    //     is stationary above the platform but the platform itself moves
    //     under them.  The carry record should remain populated. ---
    {
        Room room = BuildMovingPlatformFixtureRoom();
        PlayerMotor motor;
        PlayerState p;
        p.position = {0.0f, 3.0f, 0.0f};
        p.grounded = true;
        p.contact.was_grounded = true;

        MovingSurface* platform = FindMovingSurfaceMutable(room, 200);
        assert(platform != nullptr);

        // First tick: platform moves +X, player picks up stored carry.
        platform->position.x += 0.3f;
        AdvanceMovingSurfaces(room, 1.0f / 60.0f);
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        motor.Step(p, room, in, 1.0f / 60.0f);
        const float carry_after_first = p.platform_carry.stored_velocity.x;
        assert(carry_after_first > 0.0f);

        // Second tick: platform still moves +X by same amount -> carry must
        // remain populated (replaced or preserved, never cleared while grounded).
        platform->position.x += 0.3f;
        AdvanceMovingSurfaces(room, 1.0f / 60.0f);
        motor.Step(p, room, in, 1.0f / 60.0f);
        assert(p.platform_carry.stored_velocity.x > 0.0f);
        assert(p.platform_carry.time_remaining > 0.0f);
    }

    return 0;
}
