// Exercise OG slope-speed behavior on an actual baked Forsaken City ramp.
// Finds a stable walkable slope corridor in CMSH, then runs ordinary
// PlayerController ticks uphill and downhill through the collision mesh.

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kSpawnSkin = 0.2f;
constexpr float kPathLength = 9.0f;
constexpr float kNormalAlignment = 0.96f;
constexpr int kTicks = 12;

Vec3 Add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 ScaleVector(const Vec3& value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float DotXZ(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.z * b.z;
}

float LengthXZ(const Vec3& value) {
    return std::sqrt(DotXZ(value, value));
}

bool IsStableRamp(const Room& room, const GroundHit& center,
                  const Vec3& downhill) {
    for (float distance = 1.5f; distance <= kPathLength; distance += 1.5f) {
        for (float sign : {-1.0f, 1.0f}) {
            const Vec3 sample = Add(center.point,
                ScaleVector(downhill, distance * sign));
            const GroundHit hit = QueryFloorSource(
                room, {sample.x, center.point.y + 32.0f, sample.z}, 64.0f);
            if (!hit.hit || Dot(hit.normal, center.normal) < kNormalAlignment)
                return false;
        }
    }
    return true;
}

struct Ramp {
    GroundHit floor{};
    Vec3 downhill{};
    float steepness = 0.0f;
};

std::vector<Ramp> FindRamps(const Room& room) {
    std::vector<Ramp> ramps;
    const CollMesh& mesh = *room.coll_mesh;
    for (uint32_t i = 0; i < mesh.header->triangle_count; ++i) {
        const CollTriangle& triangle = mesh.triangles[i];
        if ((triangle.material & MAT_SOLID) == 0) continue;
        const Vec3& a = mesh.world_verts[triangle.i0];
        const Vec3& b = mesh.world_verts[triangle.i1];
        const Vec3& c = mesh.world_verts[triangle.i2];
        const Vec3 centroid = {
            (a.x + b.x + c.x) / 3.0f,
            (a.y + b.y + c.y) / 3.0f,
            (a.z + b.z + c.z) / 3.0f,
        };
        const float top_y = std::max(centroid.y,
                                    std::max(a.y, std::max(b.y, c.y)));
        const GroundHit floor = QueryFloorSource(
            room, {centroid.x, top_y + 24.0f, centroid.z}, 48.0f);
        if (!floor.hit || floor.normal.y < 0.55f || floor.normal.y > 0.94f)
            continue;
        const float horizontal_normal = LengthXZ(floor.normal);
        if (horizontal_normal < 0.25f) continue;
        const Vec3 downhill = {floor.normal.x / horizontal_normal, 0.0f,
                               floor.normal.z / horizontal_normal};
        if (!IsStableRamp(room, floor, downhill)) continue;
        ramps.push_back({floor, downhill, 1.0f - floor.normal.y});
    }
    return ramps;
}

bool RunRampTrack(const Room& room, const MovementConfig& config,
                  PlayerController& controller, const GroundHit& floor,
                  const Vec3& direction, float& speed_out) {
    PlayerState state;
    state.position = {floor.point.x,
                      floor.point.y + config.player_half_height + kSpawnSkin,
                      floor.point.z};
    state.prev_position = state.position;
    state.grounded = true;
    state.contact.was_grounded = true;
    state.contact.ground_normal = floor.normal;
    state.contact.coyote_height = state.position.y;
    state.coyote_time_remaining = config.coyote_time;
    state.last_facing = direction;
    state.target_facing = direction;
    state.facing = direction;
    const Vec3 start = state.position;

    PlayerInput input;
    input.move = {0.0f, 1.0f};
    for (int tick = 0; tick < kTicks; ++tick) {
        controller.Step(state, input, direction, kDt, &room);
        if (!state.grounded ||
            Dot(state.contact.ground_normal, floor.normal) < kNormalAlignment)
            return false;
    }
    speed_out = LengthXZ(state.velocity);
    const float traveled = std::sqrt(
        (state.position.x - start.x) * (state.position.x - start.x) +
        (state.position.z - start.z) * (state.position.z - start.z));
    return std::isfinite(speed_out) && speed_out > 1.0f && traveled > 1.0f;
}

float RunFlatBaseline(const MovementConfig& config,
                      PlayerController& controller) {
    PlayerState state;
    state.grounded = true;
    state.contact.was_grounded = true;
    state.contact.ground_normal = {0.0f, 1.0f, 0.0f};
    state.target_facing = {1.0f, 0.0f, 0.0f};
    state.last_facing = state.target_facing;
    PlayerInput input;
    input.move = {0.0f, 1.0f};
    for (int tick = 0; tick < kTicks; ++tick)
        controller.Step(state, input, state.target_facing, kDt);
    return LengthXZ(state.velocity);
}

}  // namespace

int main(int argc, char** argv) {
    const char* pack_path = argc > 1 ? argv[1]
        : "build/bake-fc-1200/staging/forsyken-city.mappack";
    const char* mesh_path = argc > 2 ? argv[2]
        : "build/bake-fc-1200/staging/forsyken-city.colmesh";
    MapSpecV2 map{};
    if (!LoadMapPackV2(pack_path, map)) {
        std::printf("FAIL: cannot load %s\n", pack_path);
        return 1;
    }
    CollMesh* mesh = LoadCollMesh(mesh_path);
    if (!mesh) {
        std::printf("FAIL: cannot load %s\n", mesh_path);
        return 1;
    }

    Room room{};
    room.coll_mesh = mesh;
    const std::vector<Ramp> ramps = FindRamps(room);
    if (ramps.empty()) {
        std::printf("FAIL: no stable walkable ramp with %.1f-unit runway\n",
                    kPathLength);
        FreeCollMesh(mesh);
        return 1;
    }
    const V2SpawnSpec* start = map.FindStartSpawn();
    if (!start) {
        std::printf("FAIL: map has no Start spawn\n");
        FreeCollMesh(mesh);
        return 1;
    }
    const Ramp* nearest = nullptr;
    float nearest_distance = 1.0e9f;
    for (const Ramp& ramp : ramps) {
        const float dx = ramp.floor.point.x - start->position.x;
        const float dz = ramp.floor.point.z - start->position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < nearest_distance) {
            nearest = &ramp;
            nearest_distance = distance;
        }
    }
    if (nearest) {
        std::printf("[real-map-slope-nearest] face=%d distance=%.2f "
                    "point=(%.2f,%.2f,%.2f) normal=(%.3f,%.3f,%.3f)\n",
                    nearest->floor.face_id, nearest_distance,
                    nearest->floor.point.x, nearest->floor.point.y,
                    nearest->floor.point.z, nearest->floor.normal.x,
                    nearest->floor.normal.y, nearest->floor.normal.z);
    }

    MovementConfig config;
    PlayerController controller(config);
    const float flat_speed = RunFlatBaseline(config, controller);
    Ramp selected{};
    float downhill_speed = 0.0f;
    float uphill_speed = 0.0f;
    bool downhill_ok = false;
    bool uphill_ok = false;
    size_t candidates_tested = 0;
    for (const Ramp& candidate : ramps) {
        ++candidates_tested;
        float candidate_downhill = 0.0f;
        float candidate_uphill = 0.0f;
        const Vec3 uphill = ScaleVector(candidate.downhill, -1.0f);
        const bool candidate_downhill_ok = RunRampTrack(
            room, config, controller, candidate.floor, candidate.downhill,
            candidate_downhill);
        const bool candidate_uphill_ok = RunRampTrack(
            room, config, controller, candidate.floor, uphill,
            candidate_uphill);
        if (candidate_downhill_ok && candidate_uphill_ok &&
            candidate_downhill > flat_speed + 5.0f &&
            candidate_uphill < flat_speed - 5.0f) {
            selected = candidate;
            downhill_speed = candidate_downhill;
            uphill_speed = candidate_uphill;
            downhill_ok = true;
            uphill_ok = true;
            break;
        }
    }
    const bool pass = downhill_ok && uphill_ok && flat_speed > 55.0f;
    std::printf("[real-map-slope] result=%s face=%d normal=(%.3f,%.3f,%.3f) "
                "steepness=%.3f downhill=%.2f flat=%.2f uphill=%.2f "
                "point=(%.2f,%.2f,%.2f) candidates=%zu/%zu "
                "tracks=%u/%u\n",
                pass ? "PASS" : "FAIL", selected.floor.face_id,
                selected.floor.normal.x, selected.floor.normal.y,
                selected.floor.normal.z, selected.steepness,
                downhill_speed, flat_speed, uphill_speed,
                selected.floor.point.x, selected.floor.point.y,
                selected.floor.point.z,
                candidates_tested, ramps.size(),
                downhill_ok ? 1u : 0u, uphill_ok ? 1u : 0u);
    FreeCollMesh(mesh);
    return pass ? 0 : 1;
}
