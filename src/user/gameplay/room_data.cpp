#include "room_data.hpp"

namespace madeline_cube {

namespace {

Room BuildStartRoom() {
    Room room;
    room.player_start = {0.0f, 3.0f, 0.0f};
    room.checkpoint = {0.0f, 3.0f, 0.0f};
    room.kill_plane_y = -20.0f;

    // Main platform
    room.geometry[room.geometry_count++] = {
        .position = {0.0f, -1.0f, 0.0f},
        .scale = {10.0f, 1.0f, 10.0f},
        .color = 0x5AA06AFF,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-10.0f, 0.0f, -10.0f}, .max = {10.0f, 0.0f, 10.0f}},
        .solid = true,
    };

    // Left wall for wall-grab practice
    room.geometry[room.geometry_count++] = {
        .position = {-12.0f, 4.0f, 0.0f},
        .scale = {1.0f, 8.0f, 4.0f},
        .color = 0x888888FF,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {-13.0f, 0.0f, -2.0f}, .max = {-11.0f, 8.0f, 2.0f}},
        .solid = true,
    };

    // Right wall
    room.geometry[room.geometry_count++] = {
        .position = {12.0f, 4.0f, 0.0f},
        .scale = {1.0f, 8.0f, 4.0f},
        .color = 0x888888FF,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {11.0f, 0.0f, -2.0f}, .max = {13.0f, 8.0f, 2.0f}},
        .solid = true,
    };

    // Small raised platform
    room.geometry[room.geometry_count++] = {
        .position = {6.0f, 1.0f, 0.0f},
        .scale = {3.0f, 1.0f, 3.0f},
        .color = 0x5AA06AFF,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {3.0f, 2.0f, -3.0f}, .max = {9.0f, 2.0f, 3.0f}},
        .solid = true,
    };

    // Strawberry collectible
    room.spawns[room.spawn_count++] = {
        .position = {6.0f, 4.0f, 0.0f},
        .placeholder_id = 2,  // pickup_strawberry
    };

    // Refill gem
    room.spawns[room.spawn_count++] = {
        .position = {-6.0f, 2.0f, 0.0f},
        .placeholder_id = 3,  // pickup_refill
    };

    // Spring board
    room.spawns[room.spawn_count++] = {
        .position = {0.0f, 0.5f, 6.0f},
        .placeholder_id = 7,  // actor_spring
    };

    return room;
}

Room BuildPhysicsQueryGrayboxRoom() {
    Room room;

    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-8.0f, 0.0f, -8.0f}, .max = {8.0f, 0.0f, 8.0f}},
        .solid = true,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-8.0f, 6.0f, -8.0f}, .max = {8.0f, 6.0f, 8.0f}},
        .solid = true,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {-4.0f, 0.0f, -2.0f}, .max = {-3.0f, 6.0f, 2.0f}},
        .solid = true,
    };
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {3.0f, 0.0f, -2.0f}, .max = {4.0f, 6.0f, 2.0f}},
        .solid = true,
    };
    // Intentional duplicate wall: equal-distance ray hits must prefer collider 3.
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Box,
        .bounds = {.min = {3.0f, 0.0f, -2.0f}, .max = {4.0f, 6.0f, 2.0f}},
        .solid = true,
    };

    return room;
}

const Room g_start_room = BuildStartRoom();
const Room g_physics_query_graybox_room = BuildPhysicsQueryGrayboxRoom();

}  // namespace

const Room& GetForsakenCityStartRoom() {
    return g_start_room;
}

const Room& GetPhysicsQueryGrayboxRoom() {
    return g_physics_query_graybox_room;
}

Room BuildSlopeFixtureRoom() {
    Room room;
    constexpr float kSqrtHalf = 0.70710678f;  // sin/cos of 45 degrees

    // Flat floor at y=0 across x in [-15, 15].  Leaves room left of the slope
    // for the player to stand on level ground before stepping onto the ramp.
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-15.0f, 0.0f, -5.0f}, .max = {15.0f, 0.0f, 5.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 0,
    };

    // 45-degree slope spanning x in [-5, 0], rising from y=0 at x=0 up to
    // y=5 at x=-5.  Normal faces up and toward +X (downhill toward +X).
    // Plane origin must be an actual on-plane point: pick the high corner.
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-5.0f, 0.0f, -5.0f}, .max = {0.0f, 5.0f, 5.0f}},
        .solid = true,
        .normal = {kSqrtHalf, kSqrtHalf, 0.0f},
        .plane_origin = {-5.0f, 5.0f, 0.0f},
        .has_plane_origin = true,
        .face_id = 1,
    };

    return room;
}

Room BuildLedgeFixtureRoom() {
    Room room;

    // Upper floor: y=0 for x in [-10, 0].
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-10.0f, 0.0f, -5.0f}, .max = {0.0f, 0.0f, 5.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 0,
    };

    // Lower floor: y=-1 for x in [0, 10].
    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {0.0f, -1.0f, -5.0f}, .max = {10.0f, -1.0f, 5.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .face_id = 1,
    };

    return room;
}

Room BuildMovingPlatformFixtureRoom() {
    Room room;
    constexpr int kFloorId = 100;
    constexpr int kPlatformId = 200;

    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-16.0f, 0.0f, -16.0f}, .max = {16.0f, 0.0f, 16.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .velocity = {0.0f, 0.0f, 0.0f},
        .face_id = 0,
        .owner_id = kFloorId,
    };

    room.colliders[room.collider_count++] = {
        .type = ColliderType::Plane,
        .bounds = {.min = {-2.0f, 2.0f, -2.0f}, .max = {2.0f, 2.0f, 2.0f}},
        .solid = true,
        .normal = {0.0f, 1.0f, 0.0f},
        .velocity = {0.0f, 0.0f, 0.0f},
        .face_id = 1,
        .owner_id = kPlatformId,
    };

    MovingSurface& floor = room.moving_surfaces[room.moving_surface_count++];
    floor.id = kFloorId;
    floor.position = {0.0f, 0.0f, 0.0f};
    floor.last_position = floor.position;

    MovingSurface& platform = room.moving_surfaces[room.moving_surface_count++];
    platform.id = kPlatformId;
    platform.position = {0.0f, 2.0f, 0.0f};
    platform.last_position = platform.position;

    return room;
}

}  // namespace madeline_cube
