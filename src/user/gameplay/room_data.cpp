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

    return room;
}

const Room g_start_room = BuildStartRoom();

}  // namespace

const Room& GetForsakenCityStartRoom() {
    return g_start_room;
}

}  // namespace madeline_cube
