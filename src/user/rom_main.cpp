#include <cmath>
#include <cstdint>

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/camera_controller.hpp"
#include "gameplay/collectible.hpp"
#include "gameplay/player_controller.hpp"
#include "gameplay/respawn_system.hpp"

namespace {

using namespace madeline_cube;

constexpr float kFixedDeltaSeconds = 1.0f / 60.0f;
constexpr float kIslandHalfExtent = 10.0f;
constexpr float kIslandTopY = 0.0f;
constexpr float kPlayerHalfHeight = 1.0f;

struct RenderObject {
    T3DMat4 matrix;
    T3DMat4FP* matrix_fp = nullptr;
};

uint16_t PackNormal(float x, float y, float z) {
    T3DVec3 normal = {{x, y, z}};
    t3d_vec3_norm(&normal);
    return t3d_vert_pack_normal(&normal);
}

void FillVertexPair(
    T3DVertPacked& pair,
    int16_t ax,
    int16_t ay,
    int16_t az,
    int16_t bx,
    int16_t by,
    int16_t bz,
    uint32_t color,
    uint16_t normal
) {
    pair = {
        .posA = {ax, ay, az},
        .normA = normal,
        .posB = {bx, by, bz},
        .normB = normal,
        .rgbaA = color,
        .rgbaB = color,
    };
}

void BuildCubeGeometry(T3DVertPacked* vertices, uint32_t color) {
    const uint16_t front = PackNormal(0.0f, 0.0f, 1.0f);
    const uint16_t back = PackNormal(0.0f, 0.0f, -1.0f);
    const uint16_t left = PackNormal(-1.0f, 0.0f, 0.0f);
    const uint16_t right = PackNormal(1.0f, 0.0f, 0.0f);
    const uint16_t top = PackNormal(0.0f, 1.0f, 0.0f);
    const uint16_t bottom = PackNormal(0.0f, -1.0f, 0.0f);

    FillVertexPair(vertices[0], -1, -1, 1, 1, -1, 1, color, front);
    FillVertexPair(vertices[1], 1, 1, 1, -1, 1, 1, color, front);

    FillVertexPair(vertices[2], 1, -1, -1, -1, -1, -1, color, back);
    FillVertexPair(vertices[3], -1, 1, -1, 1, 1, -1, color, back);

    FillVertexPair(vertices[4], -1, -1, -1, -1, -1, 1, color, left);
    FillVertexPair(vertices[5], -1, 1, 1, -1, 1, -1, color, left);

    FillVertexPair(vertices[6], 1, -1, 1, 1, -1, -1, color, right);
    FillVertexPair(vertices[7], 1, 1, -1, 1, 1, 1, color, right);

    FillVertexPair(vertices[8], -1, 1, 1, 1, 1, 1, color, top);
    FillVertexPair(vertices[9], 1, 1, -1, -1, 1, -1, color, top);

    FillVertexPair(vertices[10], -1, -1, -1, 1, -1, -1, color, bottom);
    FillVertexPair(vertices[11], 1, -1, 1, -1, -1, 1, color, bottom);
}

void SetTransform(RenderObject& object, const Vec3& position, const Vec3& scale) {
    const float rotation[3] = {0.0f, 0.0f, 0.0f};
    const float transform_scale[3] = {scale.x, scale.y, scale.z};
    const float transform_position[3] = {position.x, position.y, position.z};
    t3d_mat4_from_srt_euler(&object.matrix, transform_scale, rotation, transform_position);
    t3d_mat4_to_fixed(object.matrix_fp, &object.matrix);
}

void DrawCube(const T3DVertPacked* vertices, T3DMat4FP* matrix_fp) {
    t3d_matrix_push(matrix_fp);
    t3d_vert_load(vertices, 0, 24);
    t3d_matrix_pop(1);

    for (uint8_t face = 0; face < 6; ++face) {
        const uint8_t first = face * 4;
        t3d_tri_draw(first + 0, first + 1, first + 2);
        t3d_tri_draw(first + 2, first + 3, first + 0);
    }

    t3d_tri_sync();
}

PlayerInput ReadPlayerInput() {
    joypad_inputs_t held = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    constexpr float stick_max = 80.0f;
    // N64 stick X reads opposite to our world-space right direction.
    float move_x = -held.stick_x / stick_max;
    float move_y = held.stick_y / stick_max;
    const float move_length = std::sqrt((move_x * move_x) + (move_y * move_y));
    if (move_length > 1.0f) {
        move_x /= move_length;
        move_y /= move_length;
    }

    return {
        .move = {move_x, move_y},
        .jump_pressed = pressed.a != 0,
        .dash_pressed = pressed.b != 0,
    };
}

void ResolveIslandCollision(PlayerState& player) {
    const bool above_island_x = std::fabs(player.position.x) <= kIslandHalfExtent;
    const bool above_island_z = std::fabs(player.position.z) <= kIslandHalfExtent;
    const float player_floor_y = player.position.y - kPlayerHalfHeight;

    if (above_island_x && above_island_z && player_floor_y <= kIslandTopY && player.velocity.y <= 0.0f) {
        player.position.y = kIslandTopY + kPlayerHalfHeight;
        player.velocity.y = 0.0f;
        player.grounded = true;
        return;
    }

    player.grounded = false;
}

}  // namespace

int main() {
    debug_init_isviewer();
    debug_init_usblog();
    joypad_init();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    t3d_init((T3DInitParams){});

    T3DViewport viewport = t3d_viewport_create();
    T3DVec3 light_direction = {{0.2f, 0.8f, 0.6f}};
    t3d_vec3_norm(&light_direction);
    uint8_t ambient_light[4] = {70, 70, 70, 0xFF};
    uint8_t directional_light[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    T3DVertPacked* player_vertices = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * 12));
    T3DVertPacked* island_vertices = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * 12));
    T3DVertPacked* collectible_vertices = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * 12));
    BuildCubeGeometry(player_vertices, 0xFF6A8AFF);
    BuildCubeGeometry(island_vertices, 0x5AA06AFF);
    BuildCubeGeometry(collectible_vertices, 0xFF284CFF);

    RenderObject player_render;
    RenderObject island_render;
    RenderObject collectible_render;
    player_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    island_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    collectible_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));

    MovementConfig movement_config;
    PlayerController player_controller(movement_config);
    RespawnSystem respawn_system(movement_config);
    CameraController camera_controller;

    const Vec3 checkpoint = {0.0f, 3.0f, 0.0f};
    PlayerState player;
    player.position = checkpoint;
    player.grounded = false;

    CollectibleState collectible;
    collectible.position = {6.0f, 2.0f, 0.0f};
    collectible.pickup_radius = 1.5f;

    CameraState camera;
    const T3DVec3 camera_up = {{0.0f, 1.0f, 0.0f}};

    SetTransform(island_render, {0.0f, -1.0f, 0.0f}, {10.0f, 1.0f, 10.0f});

    for (;;) {
        joypad_poll();
        const PlayerInput input = ReadPlayerInput();

        player_controller.Step(player, input, kFixedDeltaSeconds);
        ResolveIslandCollision(player);
        respawn_system.Step(player, checkpoint);
        TryCollect(collectible, player.position);
        camera_controller.Step(camera, player.position, kFixedDeltaSeconds);

        SetTransform(player_render, player.position, {1.0f, 1.0f, 1.0f});
        SetTransform(
            collectible_render,
            collectible.position,
            collectible.collected ? Vec3{0.0f, 0.0f, 0.0f} : Vec3{0.75f, 0.75f, 0.75f}
        );

        const T3DVec3 camera_position = {{camera.position.x, camera.position.y, camera.position.z}};
        const T3DVec3 camera_target = {{camera.target.x, camera.target.y, camera.target.z}};

        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 4.0f, 120.0f);
        t3d_viewport_look_at(&viewport, &camera_position, &camera_target, &camera_up);

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        t3d_viewport_attach(&viewport);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_screen_clear_color(RGBA32(88, 163, 221, 0xFF));
        t3d_screen_clear_depth();
        t3d_light_set_ambient(ambient_light);
        t3d_light_set_directional(0, directional_light, &light_direction);
        t3d_light_set_count(1);
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));

        DrawCube(island_vertices, island_render.matrix_fp);
        DrawCube(player_vertices, player_render.matrix_fp);
        if (!collectible.collected) {
            DrawCube(collectible_vertices, collectible_render.matrix_fp);
        }

        rdpq_detach_show();
    }

    t3d_destroy();
    return 0;
}
