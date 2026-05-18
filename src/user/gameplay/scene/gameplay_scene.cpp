#include "gameplay/scene/gameplay_scene.hpp"

#include <cmath>
#include <cstdint>

#include <libdragon.h>
#include <rdpq.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "gameplay/runtime/timing.hpp"
#include "gameplay/player/camera_controller.hpp"
#include "gameplay/world/collectible.hpp"
#include "gameplay/debug_hud.hpp"
#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/render/level_renderer.hpp"
#include "gameplay/render/material_catalog.hpp"
#include "gameplay/render/model.hpp"
#include "gameplay/render/static_room_model.hpp"
#include "gameplay/render/texture.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/entity_dispatch.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/level_loader.hpp"
#include "gameplay/world/respawn_system.hpp"
#include "gameplay/world/room_data.hpp"
#include "gameplay/rom_telemetry.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

namespace {

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
    int16_t ax, int16_t ay, int16_t az,
    int16_t bx, int16_t by, int16_t bz,
    uint32_t color, uint16_t normal
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
    const uint16_t front  = PackNormal(0.0f, 0.0f, 1.0f);
    const uint16_t back   = PackNormal(0.0f, 0.0f, -1.0f);
    const uint16_t left   = PackNormal(-1.0f, 0.0f, 0.0f);
    const uint16_t right  = PackNormal(1.0f, 0.0f, 0.0f);
    const uint16_t top    = PackNormal(0.0f, 1.0f, 0.0f);
    const uint16_t bottom = PackNormal(0.0f, -1.0f, 0.0f);

    FillVertexPair(vertices[0],  -1, -1,  1,  1, -1,  1, color, front);
    FillVertexPair(vertices[1],   1,  1,  1, -1,  1,  1, color, front);
    FillVertexPair(vertices[2],   1, -1, -1, -1, -1, -1, color, back);
    FillVertexPair(vertices[3],  -1,  1, -1,  1,  1, -1, color, back);
    FillVertexPair(vertices[4],  -1, -1, -1, -1, -1,  1, color, left);
    FillVertexPair(vertices[5],  -1,  1,  1, -1,  1, -1, color, left);
    FillVertexPair(vertices[6],   1, -1,  1,  1, -1, -1, color, right);
    FillVertexPair(vertices[7],   1,  1, -1,  1,  1,  1, color, right);
    FillVertexPair(vertices[8],  -1,  1,  1,  1,  1,  1, color, top);
    FillVertexPair(vertices[9],   1,  1, -1, -1,  1, -1, color, top);
    FillVertexPair(vertices[10], -1, -1, -1,  1, -1, -1, color, bottom);
    FillVertexPair(vertices[11],  1, -1,  1, -1, -1,  1, color, bottom);
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
    joypad_inputs_t held_inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    constexpr float stick_max = 80.0f;
    float move_x = -held_inputs.stick_x / stick_max;
    float move_y = held_inputs.stick_y / stick_max;
    const float move_length = std::sqrt((move_x * move_x) + (move_y * move_y));
    if (move_length > 1.0f) {
        move_x /= move_length;
        move_y /= move_length;
    }

    return {
        .move = {move_x, move_y},
        .jump_pressed = pressed.a != 0,
        .jump_held = held.a != 0,
        .dash_pressed = pressed.b != 0,
        .climb_held = held.z != 0,
    };
}

CameraInput ReadCameraInput() {
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    CameraInput input;
    if (held.c_left) input.orbit -= 1.0f;
    if (held.c_right) input.orbit += 1.0f;
    if (held.c_down) input.zoom -= 1.0f;
    if (held.c_up) input.zoom += 1.0f;
    return input;
}

}  // namespace

struct GameplayScene::Impl {
    T3DViewport viewport = t3d_viewport_create();
    T3DVec3 light_direction = {{0.2f, 0.8f, 0.6f}};
    uint8_t ambient_light[4] = {70, 70, 70, 0xFF};
    uint8_t directional_light[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    T3DVertPacked* cube_vertices = nullptr;

    RenderObject player_render;
    RenderObject room_geometry[Room::kMaxGeometry];
    int room_geometry_count = 0;
    RenderObject collectible_render;

    StaticModel strawberry_model;
    StaticModel madeline_model;
    StaticModel room_fixture_model;
    StaticRoomModel static_room_model;
    SpriteTexture rock1_texture;

    LevelGeometry level_geometry;
    MaterialCatalog material_catalog;
    LevelRenderer level_renderer;

    ActorWorld actor_world;
    StrawberryActor strawberry_actor;
    RefillActor refill_actor;
    SpringActor spring_actor;

    MovementConfig movement_config;
    PlayerController player_controller{movement_config};
    PlayerMotor player_motor;
    RespawnSystem respawn_system{movement_config};
    CameraController camera_controller;

    Room room;
    Vec3 checkpoint = {0.0f, 3.0f, 0.0f};
    PlayerState player;
    CollectibleState collectible;
    CameraState camera;

    FixedStepAccumulator fixed_step;
    float render_alpha = 1.0f;

    DebugHUD debug_hud;
    RomTelemetry telemetry;
    bool baked_level_loaded_ = false;
    bool room_fixture_visible_ = false;
};

void GameplayScene::Init() {
    impl_ = new Impl();

    t3d_vec3_norm(&impl_->light_direction);

    impl_->cube_vertices = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * 12));
    BuildCubeGeometry(impl_->cube_vertices, 0x888888FF);

    impl_->player_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    impl_->collectible_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));

    impl_->debug_hud.Init();
    impl_->strawberry_model.Load("rom:/mdl/strawberry.t3dm");
    impl_->madeline_model.Load("rom:/mdl/madeline.t3dm");
    impl_->room_fixture_model.Load("rom:/mdl/room_fixture.t3dm");
    impl_->room_fixture_model.UpdateMatrix({0.0f, 4.0f, -12.0f}, 6.0f, 0.0f);
    impl_->static_room_model.Load("rom:/lvl/first-room.t3dm");

    impl_->baked_level_loaded_ =
        LoadLevel("rom:/lvl/first-room.lvl", impl_->room, impl_->level_geometry) &&
        impl_->level_renderer.Init(impl_->level_geometry);
    if (impl_->baked_level_loaded_) {
        impl_->material_catalog.Load("first-room");
        DispatchLevelEntities(impl_->room, impl_->actor_world,
                              impl_->strawberry_actor,
                              impl_->refill_actor,
                              impl_->spring_actor);
    } else {
        impl_->room = GetForsakenCityStartRoom();
    }

    impl_->checkpoint = impl_->room.checkpoint;
    impl_->player.position = impl_->room.player_start;
    impl_->player.prev_position = impl_->player.position;
    impl_->player.grounded = false;
    impl_->camera_controller.Reset(impl_->camera, impl_->player.position);
    impl_->telemetry.RecordSpawn();

    // Graybox room geometry render objects (only when not using baked level)
    if (!impl_->baked_level_loaded_) {
        impl_->room_geometry_count = impl_->room.geometry_count;
        for (int i = 0; i < impl_->room_geometry_count; ++i) {
            impl_->room_geometry[i].matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
            SetTransform(impl_->room_geometry[i], impl_->room.geometry[i].position, impl_->room.geometry[i].scale);
        }
    }

    // Setup collectible from first spawn (graybox path only)
    if (!impl_->baked_level_loaded_ && impl_->room.spawn_count > 0) {
        impl_->collectible.position = impl_->room.spawns[0].position;
        impl_->collectible.pickup_radius = 1.5f;
    }
}

void GameplayScene::Shutdown() {
    if (impl_ == nullptr) return;

    impl_->debug_hud.Shutdown();
    impl_->strawberry_model.Free();
    impl_->madeline_model.Free();
    impl_->room_fixture_model.Free();
    impl_->static_room_model.Free();
    impl_->material_catalog.Unload();
    impl_->level_renderer.Free();

    if (impl_->room.coll_mesh) {
        physics::FreeCollMesh(impl_->room.coll_mesh);
        impl_->room.coll_mesh = nullptr;
    }

    free_uncached(impl_->cube_vertices);
    free_uncached(impl_->player_render.matrix_fp);
    free_uncached(impl_->collectible_render.matrix_fp);
    for (int i = 0; i < impl_->room_geometry_count; ++i) {
        free_uncached(impl_->room_geometry[i].matrix_fp);
    }

    delete impl_;
    impl_ = nullptr;
}

void GameplayScene::Update(float delta_seconds) {
    if (impl_ == nullptr) return;

    joypad_poll();
    const joypad_buttons_t pressed_buttons = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed_buttons.start) {
        impl_->room_fixture_visible_ = !impl_->room_fixture_visible_;
        debugf("[fixture] room fixture %s\n", impl_->room_fixture_visible_ ? "ON" : "OFF");
    }
    // Input sampled once per render frame; same values replayed across all substeps.
    const PlayerInput input = ReadPlayerInput();
    const CameraInput camera_input = ReadCameraInput();
    const Vec3 camera_forward = {
        impl_->camera.target.x - impl_->camera.position.x,
        impl_->camera.target.y - impl_->camera.position.y,
        impl_->camera.target.z - impl_->camera.position.z,
    };

    // Fixed-step physics loop.
    const int n_ticks = impl_->fixed_step.BeginFrame(delta_seconds);
    impl_->player.prev_position = impl_->player.position;

    MotorResult motor_result = {};
    bool was_grounded_pre_motor = impl_->player.contact.was_grounded;
    bool did_respawn = false;

    for (int tick = 0; tick < n_ticks; ++tick) {
        const PlayerController::StepContext player_step = impl_->player_controller.TimerInputPhase(
            impl_->player, input, camera_forward, FixedStepAccumulator::kTickDt);
        impl_->player_controller.StatePhase(impl_->player, input, player_step, FixedStepAccumulator::kTickDt);

        MotorInput motor_input;
        motor_input.requested_velocity = impl_->player.velocity;
        motor_input.wants_ground_snap = impl_->player.contact.was_grounded &&
                                        impl_->player.movement_state != PlayerMovementState::Dashing;
        motor_input.wants_coyote_refresh = true;
        motor_input.wants_dash_refill = impl_->player.dash_reset_cooldown_remaining <= 0.0f;
        AdvanceMovingSurfaces(impl_->room, FixedStepAccumulator::kTickDt);
        was_grounded_pre_motor = impl_->player.contact.was_grounded;
        motor_result = impl_->player_motor.Step(impl_->player, impl_->room, motor_input, FixedStepAccumulator::kTickDt);
        impl_->player_controller.LateContactPhase(impl_->player);

        if (impl_->respawn_system.Step(impl_->player, impl_->checkpoint, impl_->room, impl_->player_motor)) {
            did_respawn = true;
            impl_->player.prev_position = impl_->player.position;
        }
    }

    impl_->render_alpha = impl_->fixed_step.Alpha();

    impl_->telemetry.BeginFrame();
    impl_->telemetry.RecordPlayerState(impl_->player);
    // Inc 7: surface/carry sample counters fed from motor result + room state.
    impl_->telemetry.RecordSurfaceSample(
        static_cast<uint32_t>(impl_->room.moving_surface_count),
        motor_result.grounded,
        motor_result.ground_normal.y,
        motor_result.grounded && was_grounded_pre_motor &&
            impl_->player.contact.ground_snap_cooldown_remaining <= 0.0f);
    if (did_respawn) {
        impl_->telemetry.RecordRespawn();
        impl_->camera_controller.Reset(impl_->camera, impl_->player.position);
    }

    // Camera reads the post-motor (and post-respawn) player state.
    impl_->camera_controller.Step(
        impl_->camera,
        impl_->player.position,
        impl_->player.wall_grabbing,
        camera_input,
        delta_seconds,
        &impl_->room
    );

    // Actors run after the player + camera so they can read the resolved
    // player state for pickup checks and other gameplay reactions.
    if (impl_->baked_level_loaded_) {
        impl_->actor_world.Update(delta_seconds);
    } else {
        TryCollect(impl_->collectible, impl_->player.position);
    }

    {
        const Vec3 interp = impl_->player.InterpolatedPosition(impl_->render_alpha);
        SetTransform(impl_->player_render, interp, {1.0f, 1.0f, 1.0f});
    }
    if (!impl_->baked_level_loaded_) {
        SetTransform(
            impl_->collectible_render,
            impl_->collectible.position,
            impl_->collectible.collected ? Vec3{0.0f, 0.0f, 0.0f} : Vec3{0.75f, 0.75f, 0.75f}
        );
    }

    DebugCounters counters;
    counters.active_scene_id = 0;
    counters.actor_count = 1 + impl_->room_geometry_count + impl_->actor_world.Count();
    impl_->debug_hud.Update(counters);

    // Print telemetry every 60 frames (~1 second) to avoid serial spam
    if (impl_->telemetry.frame_index % 60 == 0) {
        impl_->telemetry.PrintLine();
    }
}

void GameplayScene::Render() {
    if (impl_ == nullptr) return;

    const T3DVec3 camera_up = {{0.0f, 1.0f, 0.0f}};
    const T3DVec3 camera_position = {{
        impl_->camera.position.x,
        impl_->camera.position.y,
        impl_->camera.position.z
    }};
    const T3DVec3 camera_target = {{
        impl_->camera.target.x,
        impl_->camera.target.y,
        impl_->camera.target.z
    }};

    t3d_viewport_set_projection(&impl_->viewport, T3D_DEG_TO_RAD(70.0f), 4.0f, 120.0f);
    t3d_viewport_look_at(&impl_->viewport, &camera_position, &camera_target, &camera_up);

    rdpq_attach(display_get(), display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(&impl_->viewport);
    t3d_screen_clear_color(RGBA32(88, 163, 221, 0xFF));
    t3d_screen_clear_depth();
    t3d_light_set_ambient(impl_->ambient_light);
    t3d_light_set_directional(0, impl_->directional_light, &impl_->light_direction);
    t3d_light_set_count(1);
    if (impl_->room_fixture_visible_) {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        impl_->room_fixture_model.Draw();
    } else if (impl_->static_room_model.IsLoaded()) {
        impl_->static_room_model.Draw();
    } else if (impl_->baked_level_loaded_) {
        // Baked level: textured geometry + actor models
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
            T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED));
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,SHADE,0),(TEX0,0,SHADE,0)));
        impl_->level_renderer.Draw(impl_->material_catalog);
        constexpr float kStrawberryScale = 0.005f;
        if (StrawberryActor* sa = impl_->actor_world.Get<StrawberryActor>()) {
            impl_->strawberry_model.UpdateMatrix(sa->position, kStrawberryScale, 0.0f);
            impl_->strawberry_model.Draw();
        }
    } else {
        // Graybox: cube geometry
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        for (int i = 0; i < impl_->room_geometry_count; ++i) {
            DrawCube(impl_->cube_vertices, impl_->room_geometry[i].matrix_fp);
        }
        if (!impl_->collectible.collected) {
            constexpr float kStrawberryScale = 0.005f;
            impl_->strawberry_model.UpdateMatrix(impl_->collectible.position, kStrawberryScale, 0.0f);
            impl_->strawberry_model.Draw();
        }
    }

    if (impl_->madeline_model.IsLoaded()) {
        constexpr float kMadelineScale = 0.02f;
        constexpr float kMadelineYOffset = -1.0f;
        const Vec3 facing = impl_->player.facing;
        const float yaw = std::atan2(facing.x, facing.z);
        Vec3 draw_pos = impl_->player.InterpolatedPosition(impl_->render_alpha);
        draw_pos.y += kMadelineYOffset;
        impl_->madeline_model.UpdateMatrix(draw_pos, kMadelineScale, yaw);
        impl_->madeline_model.Draw();
    } else {
        DrawCube(impl_->cube_vertices, impl_->player_render.matrix_fp);
    }

    // Switch to 2D for overlays
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    impl_->debug_hud.Render();

    rdpq_detach_show();
}

}  // namespace madeline_cube
