#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/user/gameplay/world/room_data.hpp"
#include "../src/user/gameplay/world/world.hpp"

using namespace madeline_cube;

namespace {

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

Room MakeThinPlatformRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {0.0f, 0.0f, -8.0f}, .max = {2.0f, 0.0f, 8.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 10,
    };
    return room;
}

Room MakeThinWallRoom() {
    Room room;
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {4.0f, 0.0f, -2.0f}, .max = {4.5f, 4.0f, 2.0f}},
        .solid = true,
        .face_id = 20,
    };
    return room;
}

void PrintDiagnostics(const char* label, const CollisionQueryDiagnostics& diag) {
    std::printf("[%s] floor_sweep_hits=%d floor_raycast_hits=%d wall_candidate_hits=%d\n",
                label, diag.floor_sweep_hits, diag.floor_raycast_hits, diag.wall_candidate_hits);
}

bool RunSmokeFixtures() {
    bool ok = true;

    {
        const Room room = BuildLedgeFixtureRoom();
        CollisionQueryDiagnostics diag;
        const GroundHit floor = ProbeFloorDebug(room, {0.5f, 2.0f, 0.0f}, 1.0f, 20.0f, 1.0f, &diag);
        ok = ok && floor.hit;
        std::printf("[collision-query] ledge_support: hit=%d face=%d y=%.3f\n",
                    (int)floor.hit, floor.face_id, floor.point.y);
        PrintDiagnostics("collision-query", diag);
    }

    {
        Room room = MakeThinWallRoom();
        CollisionQueryDiagnostics diag;
        WallHit hits[kMaxWallHits];
        const int count = QueryWalls(room, {2.0f, 2.0f, 0.0f}, 3.0f, hits, kMaxWallHits, &diag);
        const WallHit nearest = QueryWallNearest(room, {2.0f, 2.0f, 0.0f}, 3.0f);
        ok = ok && count > 0 && nearest.hit;
        std::printf("[collision-query] thin_wall: hits=%d nearest_pushout=%.3f face=%d\n",
                    count, nearest.pushout, nearest.face_id);
        PrintDiagnostics("collision-query", diag);
    }

    {
        const Room room = BuildSlopeFixtureRoom();
        CollisionQueryDiagnostics diag;
        const GroundHit floor = ProbeFloorDebug(room, {-25.0f, 60.0f, 0.0f}, 1.0f, 80.0f, 1.0f, &diag);
        ok = ok && floor.hit && Near(floor.normal.x, 0.70710678f) && Near(floor.normal.y, 0.70710678f);
        std::printf("[collision-query] slope_support: hit=%d face=%d normal=(%.3f,%.3f,%.3f)\n",
                    (int)floor.hit, floor.face_id, floor.normal.x, floor.normal.y, floor.normal.z);
        PrintDiagnostics("collision-query", diag);
    }

    return ok;
}

int DiagnoseSupportFailure() {
    const Room room = MakeThinPlatformRoom();
    CollisionQueryDiagnostics diag;
    const GroundHit support = ProbeFloorDebug(room, {2.5f, 5.0f, 0.0f}, 1.0f, 10.0f, 0.75f, &diag);
    if (support.hit) {
        std::printf("[collision-query] support_probe hit face=%d y=%.3f\n", support.face_id, support.point.y);
        PrintDiagnostics("collision-query", diag);
        return 0;
    }
    const char* source = diag.floor_sweep_hits == 0 && diag.floor_raycast_hits == 0
        ? "support detection"
        : "response";
    std::printf("[collision-query] support_failure_probe missed -> %s\n", source);
    PrintDiagnostics("collision-query", diag);
    return 1;
}

int DiagnoseWallFailure() {
    Room room = MakeThinWallRoom();
    CollisionQueryDiagnostics diag;
    WallHit hits[kMaxWallHits];
    const int count = QueryWalls(room, {3.1f, 2.0f, 0.0f}, 0.3f, hits, kMaxWallHits, &diag);
    if (count == 0) {
        std::printf("[collision-query] wall_failure_probe missed -> wall candidate generation\n");
        PrintDiagnostics("collision-query", diag);
        return 1;
    }
    const WallHit nearest = QueryWallNearest(room, {3.1f, 2.0f, 0.0f}, 0.3f);
    if (!nearest.hit || nearest.pushout <= 0.0f) {
        std::printf("[collision-query] wall_failure_probe candidate found but response failed\n");
        PrintDiagnostics("collision-query", diag);
        return 1;
    }
    std::printf("[collision-query] wall_failure_probe hit face=%d pushout=%.3f\n", nearest.face_id, nearest.pushout);
    PrintDiagnostics("collision-query", diag);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke_only = argc <= 1 || std::strcmp(argv[1], "--smoke") == 0;
    if (smoke_only) {
        const bool ok = RunSmokeFixtures();
        std::printf("[collision-query] smoke=%s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    if (std::strcmp(argv[1], "--diagnose-support") == 0) {
        return DiagnoseSupportFailure();
    }

    if (std::strcmp(argv[1], "--diagnose-wall") == 0) {
        return DiagnoseWallFailure();
    }

    std::fprintf(stderr, "usage: %s [--smoke|--diagnose-support|--diagnose-wall]\n", argv[0]);
    return 2;
}
