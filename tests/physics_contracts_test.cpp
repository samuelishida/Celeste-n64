#include <cassert>
#include <cmath>
#include <limits>

#include "../src/user/gameplay/physics_contracts.hpp"

using namespace madeline_cube;

int main() {
    // --- Coordinate adapter round-trip ---
    {
        Vec3 src = {1.0f, 2.0f, 3.0f};
        Vec3 port = SrcToPort(src);
        assert(port.x == 1.0f);
        assert(port.y == 3.0f);
        assert(port.z == -2.0f);

        Vec3 back = PortToSrc(port);
        assert(back.x == src.x);
        assert(back.y == src.y);
        assert(back.z == src.z);
    }

    // --- Coordinate adapter preserves origin ---
    {
        Vec3 zero = {0.0f, 0.0f, 0.0f};
        assert(SrcToPort(zero).x == 0.0f);
        assert(SrcToPort(zero).y == 0.0f);
        assert(SrcToPort(zero).z == 0.0f);
    }

    // --- MotorInput default construction ---
    {
        MotorInput input;
        assert(input.requested_velocity.x == 0.0f);
        assert(input.wants_ground_snap == true);
        assert(input.wants_coyote_refresh == true);
        assert(input.wants_dash_refill == true);
    }

    // --- MotorResult default construction ---
    {
        MotorResult result;
        assert(result.grounded == false);
        assert(result.landed_this_frame == false);
        assert(result.ground_face_id == -1);
        assert(result.wall_face_id == -1);
        assert(result.ground_normal.y == 1.0f);
    }

    // --- GroundHit default construction ---
    {
        GroundHit hit;
        assert(hit.hit == false);
        assert(hit.face_id == -1);
        assert(hit.owner_id == -1);
        assert(hit.normal.y == 1.0f);
    }

    // --- WallHit default construction ---
    {
        WallHit hit;
        assert(hit.hit == false);
        assert(hit.face_id == -1);
        assert(hit.owner_id == -1);
    }

    // --- CeilingHit default construction ---
    {
        CeilingHit hit;
        assert(hit.hit == false);
        assert(hit.face_id == -1);
        assert(hit.owner_id == -1);
        assert(hit.normal.y == -1.0f);
    }

    // --- IsNumericValid: normal numbers ---
    {
        assert(IsNumericValid(0.0f));
        assert(IsNumericValid(1.0f));
        assert(IsNumericValid(-3.14f));
        assert(IsNumericValid(1e30f));
        assert(IsNumericValid(1e-30f));
    }

    // --- IsNumericValid: rejects NaN ---
    {
        assert(!IsNumericValid(std::numeric_limits<float>::quiet_NaN()));
        assert(!IsNumericValid(std::numeric_limits<float>::signaling_NaN()));
    }

    // --- IsNumericValid: rejects infinity ---
    {
        assert(!IsNumericValid(std::numeric_limits<float>::infinity()));
        assert(!IsNumericValid(-std::numeric_limits<float>::infinity()));
    }

    // --- IsNumericValid: rejects subnormal ---
    {
        assert(!IsNumericValid(std::numeric_limits<float>::denorm_min()));
    }

    // --- IsNumericValid Vec3 ---
    {
        Vec3 valid = {1.0f, 2.0f, 3.0f};
        assert(IsNumericValid(valid));

        Vec3 invalid = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        assert(!IsNumericValid(invalid));
    }

    // --- NearlyEqual ---
    {
        assert(NearlyEqual(1.0f, 1.0f));
        assert(NearlyEqual(1.0f, 1.00005f));
        assert(!NearlyEqual(1.0f, 1.1f));
        assert(NearlyEqual(0.0f, 0.0f));
        assert(NearlyEqual(-1.0f, -1.0f));
    }

    // --- NearlyEqual with custom epsilon ---
    {
        assert(NearlyEqual(1.0f, 1.5f, 1.0f));
        assert(!NearlyEqual(1.0f, 1.5f, 0.1f));
    }

    return 0;
}
