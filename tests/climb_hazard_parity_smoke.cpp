#include <cassert>
#include <cmath>

#include "gameplay/player/player_controller.hpp"
#include "gameplay/world/hazard_query.hpp"

using namespace madeline_cube;

namespace {

constexpr float kDt = 1.0f / 60.0f;

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

PlayerState Climber() {
    PlayerState state;
    state.position = {10.0f, 30.0f, 20.0f};
    state.movement_state = PlayerMovementState::Climbing;
    state.climb.wall_normal = {0.0f, 0.0f, -1.0f};
    state.target_facing = {0.0f, 0.0f, 1.0f};
    state.last_facing = state.target_facing;
    return state;
}

PlayerController::StepContext TickClimb(PlayerState& state,
                                        PlayerInput input,
                                        const MapSpecV2* hazards) {
    input.climb_held = true;
    PlayerController controller;
    PlayerController::StepContext context{};
    context.move_input = {0.0f, 0.0f, 1.0f};
    context.has_move_input = true;
    context.camera_forward = {0.0f, 0.0f, 1.0f};
    context.camera_target_forward = context.camera_forward;
    context.hazard_map = hazards;
    controller.StatePhase(state, input, context, kDt, nullptr);
    return context;
}

void SetSpike(MapSpecV2& map, const Vec3& point) {
    map.hazard_count = 1;
    map.hazards[0].kind = kHazardSpikeBlock;
    map.hazards[0].source_id = 77;
    map.hazards[0].bounds = {
        {point.x - 0.1f, point.y - 0.1f, point.z - 0.1f},
        {point.x + 0.1f, point.y + 0.1f, point.z + 0.1f},
    };
    map.hazards[0].spike_direction = {0.0f, 0.0f, 1.0f};
}

}  // namespace

int main() {
    // OG Player.cs:1500-1503 probes body position + up4 + forward5. A spike
    // there cancels only upward motion; side input remains active. This probe
    // must ignore SpikeBlock's lethal-velocity dot gate (climb velocity is 0).
    {
        PlayerState state = Climber();
        const Vec3 probe = {10.0f, 34.0f, 25.0f};
        MapSpecV2 map;
        SetSpike(map, probe);
        assert(!HazardKillsAtPoint(map.hazards[0], probe, {}));

        PlayerInput input;
        input.move = {0.5f, 1.0f};
        const auto context = TickClimb(state, input, &map);
        assert(context.movement.has_climb_delta);
        assert(Near(context.movement.climb_delta.x, 40.0f * 0.5f * kDt));
        assert(Near(context.movement.climb_delta.y, 0.0f));
    }

    // OG Player.cs:1505-1508 probes SolidWaistTestPos + side + forward5.
    // A spike there removes only the lateral component, preserving climb-up.
    {
        PlayerState state = Climber();
        // For wall normal -Z, wallRight is -X; input.x=.5 maps to side +.5X.
        const Vec3 probe = {10.5f, 21.75f, 25.0f};
        MapSpecV2 map;
        SetSpike(map, probe);

        PlayerInput input;
        input.move = {0.5f, 1.0f};
        const auto context = TickClimb(state, input, &map);
        assert(context.movement.has_climb_delta);
        assert(Near(context.movement.climb_delta.x, 0.0f));
        assert(Near(context.movement.climb_delta.y, 40.0f * kDt));
    }

    // A DeathBlock at the up probe does not substitute for OG's
    // SpikeBlock-only climb gate; full diagonal movement survives.
    {
        PlayerState state = Climber();
        MapSpecV2 map;
        map.hazard_count = 1;
        map.hazards[0].kind = kHazardDeathBlock;
        map.hazards[0].bounds = {{9.0f, 33.0f, 24.0f}, {11.0f, 35.0f, 26.0f}};

        PlayerInput input;
        input.move = {0.5f, 1.0f};
        const auto context = TickClimb(state, input, &map);
        assert(Near(context.movement.climb_delta.x, 40.0f * 0.5f * kDt));
        assert(Near(context.movement.climb_delta.y, 40.0f * kDt));
    }
}
