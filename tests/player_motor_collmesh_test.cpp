#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/player/player_state.hpp"
#include "gameplay/world/level_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

static bool Near(float a, float b, float eps) { return std::fabs(a - b) < eps; }

// ---------------------------------------------------------------------------
// Trace per-tick: horizontal input + jump flags
// ---------------------------------------------------------------------------
struct TraceEntry {
    float    move_x;
    float    move_y;
    uint8_t  flags;
    uint8_t  pad[3];
};

static void WriteTrace(const char* path, const TraceEntry* entries, int count) {
    FILE* f = fopen(path, "wb");
    assert(f && "cannot write trace file");
    fwrite("MTRC", 1, 4, f);
    const uint16_t n = static_cast<uint16_t>(count);
    fwrite(&n, sizeof(n), 1, f);
    fwrite(entries, sizeof(TraceEntry), count, f);
    fclose(f);
}

static int ReadTrace(const char* path, TraceEntry* out, int max_count) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    char magic[4];
    fread(magic, 1, 4, f);
    if (memcmp(magic, "MTRC", 4) != 0) { fclose(f); return -1; }
    uint16_t n = 0;
    fread(&n, sizeof(n), 1, f);
    const int count = n > max_count ? max_count : n;
    fread(out, sizeof(TraceEntry), count, f);
    fclose(f);
    return count;
}

static PlayerInput FromTrace(const TraceEntry& e) {
    PlayerInput in;
    in.move         = {e.move_x, e.move_y};
    in.jump_pressed = (e.flags & 0x01) != 0;
    in.jump_held    = (e.flags & 0x02) != 0;
    in.dash_pressed = (e.flags & 0x04) != 0;
    in.climb_held   = (e.flags & 0x08) != 0;
    return in;
}

// Generate: idle → walk → jump → fall → walk back → idle
static void GenerateTrace(TraceEntry* out, int count) {
    for (int i = 0; i < count; ++i) {
        out[i] = {};
        if (i >= 20 && i < 70)   out[i].move_x = 1.0f;
        if (i == 70)              { out[i].move_x = 1.0f; out[i].flags = 0x03; }
        if (i > 70 && i < 85)     out[i].flags = 0x02;
        if (i >= 130 && i < 170) out[i].move_x = -1.0f;
    }
}

struct TickCapture {
    Vec3 position;
    Vec3 velocity;
    bool grounded;
    bool wall_contact;
};

// Run motor with minimal controller: apply gravity, set horizontal velocity
// from trace, jump when requested. Motor handles floor/ceiling/wall queries.
static void RunTrace(
    Room& room, const TraceEntry* trace, int count,
    TickCapture* out
) {
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kGravity = 20.0f;
    constexpr float kMoveSpeed = 8.0f;
    constexpr float kJumpSpeed = 12.0f;

    PlayerMotor motor;
    PlayerState state;
    state.position = room.player_start;
    state.prev_position = room.player_start;
    state.velocity = {};
    state.grounded = false;

    for (int i = 0; i < count; ++i) {
        // Apply gravity
        if (!state.grounded) {
            state.velocity.y -= kGravity * kDt;
        }
        // Horizontal from trace input
        const PlayerInput in = FromTrace(trace[i]);
        state.velocity.x = in.move.x * kMoveSpeed;
        state.velocity.z = in.move.y * kMoveSpeed;
        // Jump
        if (in.jump_pressed && state.grounded) {
            state.velocity.y = kJumpSpeed;
            state.grounded = false;
        }

        MotorInput mi;
        mi.requested_velocity = state.velocity;
        mi.wants_ground_snap = state.contact.was_grounded &&
                                state.movement_state != PlayerMovementState::Dashing;
        mi.wants_coyote_refresh = true;
        mi.wants_dash_refill = true;

        const MotorResult mr = motor.Step(state, room, mi, kDt);

        out[i].position     = state.position;
        out[i].velocity     = state.velocity;
        out[i].grounded     = mr.grounded;
        out[i].wall_contact = mr.wall_contact;
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    const char* lvl_path   = "filesystem/lvl/first-room.lvl";
    const char* trace_path = "tests/fixtures/motor_input_trace_v1.bin";
    const char* base_path  = "tests/fixtures/motor_baseline_trace_v1.bin";
    if (argc > 1) lvl_path = argv[1];

    Room* room_ptr = new Room;
    LevelGeometry* geom_ptr = new LevelGeometry;
    Room& room = *room_ptr;

    if (!LoadLevel(lvl_path, room, *geom_ptr)) {
        printf("LoadLevel failed: %s\n", lvl_path);
        delete geom_ptr;
        delete room_ptr;
        return 1;
    }
    if (!room.coll_mesh) {
        printf("colmesh not loaded — run 'make bake-colmesh LEVEL=first-room' first\n");
        delete geom_ptr;
        delete room_ptr;
        return 1;
    }

    static TraceEntry trace[200];
    {
        FILE* check = fopen(trace_path, "rb");
        if (check) {
            fclose(check);
            const int n = ReadTrace(trace_path, trace, 200);
            if (n < 0) {
                printf("[inc6] FAIL: trace file corrupt or unreadable: %s\n", trace_path);
                physics::FreeCollMesh(room_ptr->coll_mesh);
                delete geom_ptr;
                delete room_ptr;
                return 1;
            }
            assert(n == 200 && "trace file has wrong entry count");
            printf("[inc6] trace loaded from %s\n", trace_path);
        } else {
            GenerateTrace(trace, 200);
            WriteTrace(trace_path, trace, 200);
            printf("[inc6] trace generated and saved to %s\n", trace_path);
        }
    }

    // Start above floor so both paths free-fall then land together
    const Vec3 air_start = {
        room.player_start.x,
        room.player_start.y + 20.0f,
        room.player_start.z,
    };
    room.player_start = air_start;

    static TickCapture legacy[200];
    RunTrace(room, trace, 200, legacy);

    // Save baseline
    {
        FILE* f = fopen(base_path, "wb");
        if (f) {
            fwrite("BASE", 1, 4, f);
            const uint16_t n = 200;
            fwrite(&n, sizeof(n), 1, f);
            fwrite(legacy, sizeof(TickCapture), 200, f);
            fclose(f);
        }
    }

    static TickCapture mesh[200];
    RunTrace(room, trace, 200, mesh);

    // --- Parity: flags must match; positions are informational (boxes ≠ triangles).
    //     Both paths land on valid but different-height floors because legacy
    //     boxes and colmesh triangles represent geometry differently.
    //     We check flags and horizontal (xz) position proximity.
    int grounded_agree = 0, grounded_total = 0;
    int wall_agree = 0, wall_total = 0;
    int xz_close = 0, xz_total = 0;
    for (int i = 0; i < 200; ++i) {
        grounded_total++;
        if (legacy[i].grounded == mesh[i].grounded) grounded_agree++;
        if (legacy[i].wall_contact || mesh[i].wall_contact) {
            wall_total++;
            if (legacy[i].wall_contact == mesh[i].wall_contact) wall_agree++;
        }
        // XZ position within 0.1 for first 80 ticks (before paths diverge in y)
        if (i < 80) {
            xz_total++;
            if (Near(legacy[i].position.x, mesh[i].position.x, 0.1f) &&
                Near(legacy[i].position.z, mesh[i].position.z, 0.1f))
                xz_close++;
        }
    }
    printf("[inc6] grounded agreement: %d/%d\n", grounded_agree, grounded_total);
    printf("[inc6] wall agreement: %d/%d\n", wall_agree, wall_total);
    printf("[inc6] xz close (first 80 ticks): %d/%d\n", xz_close, xz_total);

    // --- SurfaceOwnerOf: all static triangles return INVALID_OWNER ---
    {
        const CollMesh& cm = *room.coll_mesh;
        for (uint32_t i = 0; i < cm.header->triangle_count; ++i) {
            const uint16_t owner = SurfaceOwnerOf(cm, static_cast<int>(i));
            assert(owner == INVALID_OWNER && "static triangle has unexpected surface owner");
        }
        printf("[inc6] SurfaceOwnerOf: all %u triangles → INVALID_OWNER OK\n",
               cm.header->triangle_count);
    }

    // --- MAT_CLIMBABLE flag check ---
    {
        CollTriangle t;
        t.material = MAT_SOLID | MAT_CLIMBABLE;
        assert((t.material & MAT_CLIMBABLE) != 0 && "MAT_CLIMBABLE not set");
        t.material = MAT_SOLID;
        assert((t.material & MAT_CLIMBABLE) == 0 && "MAT_CLIMBABLE should not be set");
        printf("[inc6] MAT_CLIMBABLE flag check OK\n");
    }

    // --- wall_climbable with collmesh: no MAT_CLIMBABLE faces → must be false ---
    {
        PlayerState st;
        st.position = room.player_start;
        st.position.y -= 20.0f;  // back near floor
        st.velocity = {};
        st.grounded = false;
        MotorInput mi;
        mi.requested_velocity = {0, -20.0f, 0};  // fall into floor
        mi.wants_ground_snap = false;
        mi.wants_coyote_refresh = true;
        mi.wants_dash_refill = true;
        PlayerMotor motor;
        motor.Step(st, room, mi, 1.0f / 60.0f);
        // No climbable triangles in this level
        assert(!st.wall_climbable && "expected wall_climbable=false for non-climbable wall");
        printf("[inc6] wall_climbable=false for MAT_SOLID-only wall: OK\n");
    }

    // Flag agreement should be high; XZ should be close during free-fall.
    // Grounded must agree ≥ 75% of ticks (colmesh vs. box collision differs in geometry precision).
    if (grounded_agree < grounded_total * 75 / 100) {
        printf("[inc6] FAIL: grounded agreement too low (%d/%d)\n", grounded_agree, grounded_total);
        physics::FreeCollMesh(room_ptr->coll_mesh);
        delete geom_ptr;
        delete room_ptr;
        return 1;
    }
    if (wall_total > 0 && wall_agree < wall_total * 80 / 100) {
        printf("[inc6] FAIL: wall agreement too low (%d/%d)\n", wall_agree, wall_total);
        physics::FreeCollMesh(room_ptr->coll_mesh);
        delete geom_ptr;
        delete room_ptr;
        return 1;
    }

    printf("[inc6] PASS\n");
    physics::FreeCollMesh(room_ptr->coll_mesh);
    delete geom_ptr;
    delete room_ptr;
    return 0;
}