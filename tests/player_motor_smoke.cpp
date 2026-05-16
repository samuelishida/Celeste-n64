#include <cassert>
#include <cmath>
#include <limits>

#include "../src/user/gameplay/player/player_motor.hpp"
#include "../src/user/gameplay/world/room_data.hpp"

using namespace madeline_cube;

namespace {

bool ApproxEq(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

Room BuildBasicRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
        .solid = true,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {2.0f, 0.0f, -1.0f}, .max = {3.0f, 3.0f, 1.0f}},
        .solid = true,
    };
    return room;
}

MotorInput MovingInput(const Vec3& velocity) {
    MotorInput in;
    in.requested_velocity = velocity;
    in.wants_ground_snap = true;
    return in;
}

}  // namespace

int main() {
    PlayerMotor motor;
    const Room basic = BuildBasicRoom();

    // --- Slow fall to floor: lands exactly on floor, velocity zeroes. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 2.0f, 0.0f};
        MotorResult r = motor.Step(p, room, MovingInput({0.0f, -10.0f, 0.0f}), 0.1f);
        assert(r.grounded);
        assert(p.grounded);
        assert(ApproxEq(p.position.y, 1.0f));
        assert(ApproxEq(p.velocity.y, 0.0f));
        assert(r.landed_this_frame);
        assert(r.ground_face_id == 0);
    }

    // --- Fast fall: sweep substeps still capture floor without overshoot. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 2.0f, 0.0f};
        MotorResult r = motor.Step(p, room, MovingInput({0.0f, -30.0f, 0.0f}), 0.1f);
        assert(r.grounded);
        assert(ApproxEq(p.position.y, 1.0f));
        assert(ApproxEq(p.velocity.y, 0.0f));
    }

    // --- Lateral collision into box wall: pushed out, X velocity zeroed,
    //     wall_right flag set, ground retained on the floor plane. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 1.0f, 0.0f};
        p.grounded = true;
        MotorResult r = motor.Step(p, room, MovingInput({30.0f, 0.0f, 0.0f}), 0.1f);
        assert(r.wall_contact);
        assert(p.wall_right);
        assert(p.position.x <= 1.5f + 0.0005f);
        assert(ApproxEq(p.velocity.x, 0.0f));
        assert(p.grounded);
    }

    // --- Ground snap: was grounded, short drop within snap distance. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 1.2f, 0.0f};
        p.grounded = true;
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        MotorResult r = motor.RefreshContacts(p, room, in);
        assert(r.grounded);
        assert(ApproxEq(p.position.y, 1.0f));
    }

    // --- No snap when previously airborne: should fall freely. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 1.2f, 0.0f};
        p.grounded = false;
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = true;
        MotorResult r = motor.RefreshContacts(p, room, in);
        assert(!r.grounded);
        assert(ApproxEq(p.position.y, 1.2f));
    }

    // --- NaN/subnormal flushed in position and velocity. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {std::numeric_limits<float>::denorm_min(), 2.0f, 0.0f};
        motor.Step(p, room, MovingInput({0.0f, -10.0f, 0.0f}), 0.1f);
        assert(std::fpclassify(p.position.x) != FP_SUBNORMAL);

        PlayerState q;
        q.position = {0.0f, 2.0f, 0.0f};
        motor.Step(q, room, MovingInput({std::numeric_limits<float>::quiet_NaN(), -10.0f, 0.0f}), 0.1f);
        assert(IsNumericValid(q.position));
        assert(IsNumericValid(q.velocity));
    }

    // --- Ceiling: upward velocity into a low ceiling clips and zeroes vy. ---
    {
        Room room;
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-5.0f, 0.0f, -5.0f}, .max = {5.0f, 0.0f, 5.0f}},
            .solid = true,
            .normal = {0.0f, 1.0f, 0.0f},
        };
        room.colliders[room.collider_count++] = {
            .type = ColliderType::Plane,
            .bounds = {.min = {-5.0f, 4.0f, -5.0f}, .max = {5.0f, 4.0f, 5.0f}},
            .solid = true,
            .normal = {0.0f, -1.0f, 0.0f},
        };
        PlayerState p;
        p.position = {0.0f, 3.0f, 0.0f};  // head at 4.0
        MotorResult r = motor.Step(p, room, MovingInput({0.0f, 20.0f, 0.0f}), 0.05f);
        assert(ApproxEq(p.velocity.y, 0.0f));
        assert(p.position.y <= 3.0f + 0.0005f);
        (void)r;
    }

    // --- Moving platform owner velocity gets stored as platform carry. ---
    {
        Room room = BuildMovingPlatformFixtureRoom();
        MovingSurface* platform = FindMovingSurfaceMutable(room, 200);
        assert(platform != nullptr);
        platform->position.x += 0.5f;
        AdvanceMovingSurfaces(room, 1.0f / 60.0f);

        PlayerState p;
        p.position = {0.5f, 3.0f, 0.0f};
        MotorResult r = motor.Step(p, room, MovingInput({0.0f, -2.0f, 0.0f}), 1.0f / 60.0f);
        assert(r.grounded);
        assert(ApproxEq(p.position.y, 3.0f));
        assert(p.platform_carry.time_remaining > 0.0f);
        assert(ApproxEq(p.platform_carry.stored_velocity.x, 0.5f * 60.0f));
    }

    // --- Spawn invariant: spawning above floor produces a clean landing
    //     without any below-floor recovery branch firing. ---
    {
        Room room = basic;
        PlayerState p;
        p.position = {0.0f, 5.0f, 0.0f};
        MotorInput in;
        in.requested_velocity = {0.0f, 0.0f, 0.0f};
        in.wants_ground_snap = false;  // not previously grounded
        MotorResult r = motor.RefreshContacts(p, room, in);
        assert(!r.grounded);
        assert(ApproxEq(p.position.y, 5.0f));

        // Then falling toward floor lands cleanly.
        for (int i = 0; i < 30; ++i) {
            r = motor.Step(p, room, MovingInput({0.0f, -10.0f, 0.0f}), 1.0f / 60.0f);
            if (r.grounded) break;
        }
        assert(r.grounded);
        assert(ApproxEq(p.position.y, 1.0f));
        assert(p.position.y > 0.0f);  // never below floor
    }

    return 0;
}
