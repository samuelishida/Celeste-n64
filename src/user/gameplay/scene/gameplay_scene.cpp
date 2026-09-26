#include "gameplay/scene/gameplay_scene.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <libdragon.h>
#include <rdpq.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "gameplay/runtime/timing.hpp"
#include "gameplay/runtime/math.hpp"
#include "gameplay/debug_flags.hpp"
#include "gameplay/player/camera_controller.hpp"
#include "gameplay/world/collectible.hpp"
#include "gameplay/debug_hud.hpp"
#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_movement.hpp"
#include "gameplay/input/input_system.hpp"
#include "gameplay/input/input_trace_policy.hpp"
#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"
#include "gameplay/render/lod_math.hpp"
#include "gameplay/render/material_catalog.hpp"
#include "gameplay/render/model.hpp"
#include "gameplay/render/player_model_clearance.hpp"
#include "gameplay/render/texture.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/entity_dispatch.hpp"
#include "gameplay/actor/cassette_actor.hpp"
#include "gameplay/actor/strawberry_actor.hpp"
#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/actor/spring_actor.hpp"
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/level_loader.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/map_runtime.hpp"
#include "gameplay/world/respawn_system.hpp"
#include "gameplay/world/room_data.hpp"
#include "gameplay/rom_telemetry.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

namespace {

constexpr const char* kMadelineModelPath = "rom:/mdl/player.t3dm";
constexpr const char* kCassetteModelPath = "rom:/mdl/tape_1.t3dm";
constexpr float kFadeReloadSeconds = 0.5f;

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

// Self-contained RDP state for actor t3dm draws (Inc 5): the baked world pass
// leaves PRIM×SHADE + the last room material's prim color in the RDP. Without
// explicit state the strawberry/cassette/madeline models inherit that prim and
// tint-shift depending on which cell drew last (camera-angle-dependent). MUST
// NOT be applied inside StaticModel::Draw — room_fixture_model relies on its
// red PRIM×SHADE diagnostic at the call site.
void SetActorShadedState() {
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
}

void DrawCube(const T3DVertPacked* vertices, T3DMat4FP* matrix_fp) {
    // Self-contained RDP state (Inc 5): the baked world pass leaves PRIM×SHADE
    // + the last room material's prim color in the RDP. Without explicit state
    // here the cube would inherit that prim and shift gray/tinted depending on
    // which cell drew last (camera-angle-dependent). Force lit vertex-shade.
    SetActorShadedState();
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

// Build the PlayerInput snapshot from the frame's InputSystem state.
// Called once per render frame and replayed across all fixed-step ticks so
// the physics sees one consistent input snapshot (§34: capture raw once).
//
// Inc 1: reads the device-owned virtual states (deadzone + press buffer +
// consume-once) and wires the device pointers so the controller's
// ConsumeJumpPress/ConsumeDashPress ALSO consume the device edge (OG
// VirtualButton parity) — otherwise the buffered press re-fires next frame.
PlayerInput ReadPlayerInput(InputSystem& input_system) {
    PlayerInput result;
    result.move = input_system.MoveValue();
    result.jump_pressed = input_system.Jump().pressed;
    result.jump_held = input_system.Jump().down;
    result.dash_pressed = input_system.Dash().pressed;
    result.climb_held = input_system.Climb().down;
    result.jump_device = const_cast<VirtualButtonState*>(&input_system.Jump());
    result.dash_device = const_cast<VirtualButtonState*>(&input_system.Dash());
    return result;
}

// Build the camera input from the InputSystem's Camera action. Camera.X orbits
// (rotation), Camera.Y zooms (distance) per spec §24-25.
CameraInput ReadCameraInput(const InputSystem& input_system) {
    const Vec2 cam = input_system.camera.Value();
    CameraInput input;
    input.orbit = cam.x;
    input.zoom = cam.y;
    return input;
}

}  // namespace

struct GameplayScene::Impl {
    // Buffered (3 slots = swap-chain count) so the RSP's async DMA of the
    // projection/camera matrices never reads a torn matrix mid-frame. The
    // distant→near projection switch rewrites these 3×/frame (see
    // open_world_renderer.cpp); unbuffered, the RSP could read a half-written
    // matrix → mid-screen split under some camera angles. Freed in Shutdown.
    T3DViewport viewport = t3d_viewport_create_buffered(3);
    T3DVec3 light_direction = {{0.2f, 0.8f, 0.6f}};
    uint8_t ambient_light[4] = {90, 85, 80, 0xFF};
    uint8_t directional_light[4] = {0xFF, 0xF8, 0xEE, 0xFF};

    T3DVertPacked* cube_vertices = nullptr;

    RenderObject player_render;
    RenderObject room_geometry[Room::kMaxGeometry];
    int room_geometry_count = 0;
    RenderObject collectible_render;

    StaticModel strawberry_model;
    StaticModel cassette_model;
    StaticModel madeline_model;
    StaticModel room_fixture_model;
    SpriteTexture rock1_texture;

    LevelGeometry level_geometry;
    LvlRoomRenderer room_renderer;

    ActorWorld actor_world;
    CassetteActor cassette_actor;
    StrawberryActor strawberry_actor;
    RefillActor refill_actor;
    SpringActor spring_actor;

    MovementConfig movement_config;
    PlayerController player_controller{movement_config};
    // og-mover-overhaul Inc 2 + review S1: PlayerMotor is DELETED, and there
    // is exactly ONE MoveService — the controller's own (Mover() accessor).
    // Gameplay contact, spawn snap, and respawn all share this instance; a
    // second scene-local mover risked divergent-config drift.
    RespawnSystem respawn_system{movement_config};
    CameraController camera_controller;
    InputSystem input_system;

    Room room;
    Vec3 checkpoint = {0.0f, 30.0f, 0.0f};
    PlayerState player;
    CollectibleState collectible;
    CameraState camera;

    FixedStepAccumulator fixed_step;
    float render_alpha = 1.0f;
    float last_delta = 1.0f / 30.0f;  // streaming-experience Inc 4: last frame
                                      // delta (for the reveal animation age)

    DebugHUD debug_hud;
    RomTelemetry telemetry;
    bool baked_level_loaded_ = false;
    bool room_fixture_visible_ = false;
    bool cassette_reload_active_ = false;
    float cassette_reload_timer_ = 0.0f;

    // Multi-room map-pack state. When use_map_pack_ is true, the active room
    // is map_runtime_.Active() and the single-room room/room_renderer/actor_world
    // below are not used for the active chunk. The legacy single-room path is
    // preserved as a fallback when no map-pack is loaded.
    MapRuntime map_runtime_;
    bool use_map_pack_ = false;
    const char* mappack_path_ = nullptr;  // set via SetMapPack before Init
    // Two-pass render orchestrator (Inc 2+). In map-pack mode this drives the
    // arch.md §21 frame order; near pass uses the TileStreamer (Inc 3).
    OpenWorldRenderer open_world_;

    // Debug auto-walk bookkeeping: fixed-distance walk then stop.
    Vec3 autowalk_start_ = {0.0f, 0.0f, 0.0f};
    bool autowalk_recording_ = false;
    uint32_t physics_autoplay_tick_ = 0;
    uint32_t input_record_frame_ = 0;
    uint32_t input_record_tick_ = 0;
    int input_record_last_stick_x_bucket_ = 0;
    int input_record_last_stick_y_bucket_ = 0;
    int input_record_last_analog_l_bucket_ = 0;
    int input_record_last_analog_r_bucket_ = 0;
    int input_record_last_ground_nx_bucket_ = 0;
    int input_record_last_ground_ny_bucket_ = 0;
    int input_record_last_ground_nz_bucket_ = 0;
    int input_record_last_climb_nx_bucket_ = 0;
    int input_record_last_climb_ny_bucket_ = 0;
    int input_record_last_climb_nz_bucket_ = 0;
    int input_record_last_ground_surface_id_ = -1;
    int input_record_last_grip_face_id_ = -1;
    uint16_t input_record_last_buttons_ = 0;
    uint8_t input_record_last_state_ = 0;
    bool input_record_last_grounded_ = false;
    bool input_record_last_was_grounded_ = false;
    bool input_record_last_wall_ = false;
    bool input_record_last_ceiling_ = false;
    bool input_record_last_snap_active_ = false;
    bool input_record_has_previous_ = false;
    bool physics_autoplay_target_found_ = false;
    bool physics_autoplay_climb_started_ = false;
    bool physics_autoplay_climb_entry_recorded_ = false;
    bool physics_autoplay_climb_entry_grounded_ = false;
    bool physics_autoplay_climb_first_step_sampled_ = false;
    bool physics_autoplay_entry_attempted_ = false;
    bool physics_autoplay_walljump_attempted_ = false;
    bool physics_autoplay_walljumped_ = false;
    float physics_autoplay_walljump_speed_xy_ = 0.0f;
    float physics_autoplay_walljump_speed_y_ = 0.0f;
    float physics_autoplay_walljump_outward_speed_ = 0.0f;
    bool physics_autoplay_mantled_ = false;
    bool physics_autoplay_corner_seen_ = false;
    bool physics_autoplay_corner_arrived_ = false;
    Vec3 physics_autoplay_corner_target_normal_{};
    uint32_t physics_autoplay_corner_events_ = 0;
    uint32_t physics_autoplay_corner_stable_ticks_ = 0;
    float physics_autoplay_corner_max_step_ = 0.0f;
    bool physics_autoplay_result_emitted_ = false;
    uint32_t physics_autoplay_approach_ticks_ = 0;
    uint32_t physics_autoplay_climb_ticks_ = 0;
    uint32_t physics_autoplay_clearance_samples_ = 0;
    uint32_t physics_autoplay_clearance_violations_ = 0;
    float physics_autoplay_min_model_gap_ = 1.0e9f;
    Vec3 physics_autoplay_run_direction_{};
    Vec3 physics_autoplay_dash_start_{};
    bool physics_autoplay_dash_launch_recorded_ = false;
    bool physics_autoplay_dash_airborne_ = false;
    float physics_autoplay_dash_launch_speed_ = 0.0f;
    float physics_autoplay_dash_launch_vy_ = 0.0f;
    float physics_autoplay_run_peak_speed_ = 0.0f;
    float physics_autoplay_brake_min_speed_ = 1.0e9f;
    float physics_autoplay_reverse_speed_ = 0.0f;
    float physics_autoplay_jump_start_y_ = 0.0f;
    float physics_autoplay_jump_peak_y_ = -1.0e9f;
    float physics_autoplay_dash_distance_ = 0.0f;
    float physics_autoplay_air_steer_start_axis_speed_ = 0.0f;
    float physics_autoplay_air_steer_end_axis_speed_ = 0.0f;
    float physics_autoplay_air_release_start_speed_ = 0.0f;
    float physics_autoplay_air_release_min_speed_ = 1.0e9f;
    bool physics_autoplay_air_window_grounded_ = false;
    float physics_autoplay_target_start_gap_ = 0.0f;
    float physics_autoplay_entry_gap_ = 1.0e9f;
    float physics_autoplay_climb_entry_y_ = 0.0f;
    float physics_autoplay_climb_first_step_dy_ = 0.0f;
    float physics_autoplay_climb_first_step_snap_cooldown_ = 0.0f;
    Vec3 physics_autoplay_climb_target_point_{};
    Vec3 physics_autoplay_climb_target_normal_{};
    Vec3 physics_autoplay_approach_direction_{};
    // Material catalog for the textured near pass (Inc 5). Loaded from the
    // map-pack's `.manifest`; owned by this Impl.
    MaterialCatalog material_catalog_;
    // Cached union of all room AABBs (Inc 4 / D4). Computed once at map-pack
    // init (alongside the fog config) and reused every frame instead of
    // re-running `UnionRoomsAABB` over all 45 rooms. Static for the map
    // lifetime. `world_bounds_valid_` guards the null/empty case.
    AABB world_bounds = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    bool world_bounds_valid_ = false;

    // Resolve the active room for query/update/render. Routes to the MapRuntime
    // active room when a map-pack is in use, else the legacy single room.
    Room& ActiveRoom() {
        return use_map_pack_ ? map_runtime_.Active()->room : room;
    }
    const Room& ActiveRoom() const {
        return use_map_pack_ ? map_runtime_.Active()->room : room;
    }

    const char* lvl_path  = "rom:/lvl/first-room.lvl";
    const char* level_name = "first-room";

    void ResetPlayerToRoomStart();
    void ReloadBakedLevel();
    // Initialize cassette actor from room data (extracted helper for DRY).
    void InitCassetteForRoom(const Room& room);
    // Boot a multi-room map-pack: load the manifest, ensure + activate the
    // start room, dispatch its entities, and reset the player (boot IS a
    // spawn, so ResetPlayerToRoomStart is correct here). Returns true on
    // success; on failure the caller falls back to the legacy single-room
    // ReloadBakedLevel path.
    bool BootMapPack(const char* mappack_path);
    // Chunk-transition load: swap the active room WITHOUT resetting the
    // player (preserves world pos/velocity). Used by the per-tick boundary
    // check. Returns true if the new room is now active.
    bool TransitionToRoom(const char* room_id);

    // The camera's normalized XZ forward direction (target - position),
    // used to shape the resident wedge. Zero vector when the camera faces
    // its target exactly (degenerate → the streamer falls back to the ring).
    Vec3 CameraForwardDir() const {
        const Vec3 cam_dir = {
            camera.target.x - camera.position.x,
            0.0f,
            camera.target.z - camera.position.z,
        };
        const float d2 = cam_dir.x * cam_dir.x + cam_dir.z * cam_dir.z;
        if (d2 <= 1e-4f) return {0.0f, 0.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(d2);
        return {cam_dir.x * inv, 0.0f, cam_dir.z * inv};
    }
};

void GameplayScene::Impl::ResetPlayerToRoomStart() {
    constexpr float kSpawnSkin = 0.2f;
    const MoveServiceConfig& mover_config = player_controller.Mover().Config();
    const float spawn_lift = mover_config.half_height + kSpawnSkin;
    const auto snap_spawn_center = [&](const Vec3& authored_point) {
        Vec3 center = authored_point;
        center.y += spawn_lift;

        // OG spawn snap: a single ray down from the feet (ray + ground snap
        // distance 5). PlayerMotor's 9-point ProbeFloorDebug is gone with the
        // motor; the OG ray ground model owns spawn placement now.
        Vec3 pushout{};
        Vec3 normal{};
        PlayerState probe_state;
        probe_state.position = center;
        if (player_controller.Mover().GroundCheck(probe_state, room, &pushout, &normal)) {
            center.y += pushout.y + kSpawnSkin;
        }
        return center;
    };

    const Vec3 authored_start = room.player_start;
    checkpoint = snap_spawn_center(room.checkpoint);
    player = {};
    player.position = snap_spawn_center(room.player_start);
    player.prev_position = player.position;

    // Resolve the spawn grounded contact through the same single mover as
    // gameplay (OG ground snap), replacing PlayerMotor::RefreshContacts.
    Vec3 spawn_normal{};
    player.grounded = player_controller.Mover().GroundCheck(player, room, nullptr, &spawn_normal);
    player.contact.was_grounded = player.grounded;
    if (player.grounded) {
        player.contact.coyote_height = player.position.y;
        player.contact.ground_normal = spawn_normal;  // S6: spawn-on-slope normal
    }
    player.prev_position = player.position;

    debugf("[spawn] authored=(%.2f,%.2f,%.2f) grounded=(%.2f,%.2f,%.2f) grounded=%d\n",
           static_cast<double>(authored_start.x),
           static_cast<double>(authored_start.y),
           static_cast<double>(authored_start.z),
           static_cast<double>(player.position.x),
           static_cast<double>(player.position.y),
           static_cast<double>(player.position.z),
           player.grounded ? 1 : 0);

    camera_controller.Reset(camera, player.position);
    telemetry.RecordSpawn();
    fixed_step.accumulator = 0.0f;
    render_alpha = 1.0f;
}

void GameplayScene::Impl::ReloadBakedLevel() {
    room_renderer.Free();
    if (room.coll_mesh) {
        physics::FreeCollMesh(room.coll_mesh);
        room.coll_mesh = nullptr;
    }

    level_geometry = {};
    room = Room{};
    actor_world = ActorWorld{};

    // Load level data (entities, collision, atmosphere)
    if (!LoadLevel(lvl_path, room, level_geometry)) {
        baked_level_loaded_ = false;
        room = GetForsakenCityStartRoom();
        debugf("[reload] LoadLevel FAILED — fallback to first-room\n");
        goto skip_entity_dispatch;
    }

    // Load room geometry directly from .lvl (bypasses .glb/.t3dm pipeline)
    {
        const bool model_ok = room_renderer.Load(lvl_path);
        baked_level_loaded_ = model_ok;
        if (!model_ok) {
            debugf("[reload] LvlRoomRenderer FAILED — fallback to first-room\n");
            room = GetForsakenCityStartRoom();
            goto skip_entity_dispatch;
        }
    }

    DispatchLevelEntities(room, actor_world,
                          strawberry_actor,
                          refill_actor,
                          spring_actor);
    actor_world.ResolvePending();

skip_entity_dispatch:
    if (room.has_cassette) {
        cassette_actor.InitAt(room.cassette);
    } else {
        cassette_actor = {};
    }

    debugf("[reload] before spawn: coll_mesh=%p\n", (void*)room.coll_mesh);
    ResetPlayerToRoomStart();
    cassette_reload_active_ = false;
    cassette_reload_timer_ = 0.0f;
}

// Initialize cassette actor from room data (extracted helper for DRY).
void GameplayScene::Impl::InitCassetteForRoom(const Room& room) {
    if (room.has_cassette) {
        cassette_actor.InitAt(room.cassette);
        // Wire cassette target: if the room has a target set, use it.
        if (room.cassette_target[0] != '\0') {
            cassette_actor.target_level_path = room.cassette_target;
        }
    } else {
        cassette_actor = {};
    }
}

bool GameplayScene::Impl::BootMapPack(const char* mappack_path) {
    if (!mappack_path || mappack_path[0] == '\0') {
        debugf("[mappack] BootMapPack ENTRY: path=null — invalid\n");
        return false;
    }
    debugf("[mappack] BootMapPack ENTRY: path=%s\n", mappack_path);

    // MapRuntime owns the ONE global collision mesh + the active visual room.
    // It loads the v2 manifest (which carries the manifest Start spawn) and the
    // global CMSH. On failure, no gameplay state is committed — the caller
    // falls back to the legacy single-room path.
    if (!map_runtime_.Init(mappack_path)) {
        debugf("[mappack] MapRuntime::Init FAILED: %s — falling back to single-room\n", mappack_path);
        return false;
    }
    use_map_pack_ = true;

    // Load the material catalog for the textured near pass (Inc 5). The
    // manifest path is rom:/lvl/<pack>/<pack>.manifest (the bake emits it
    // alongside the .mappack). A missing manifest is non-fatal — the near
    // pass falls back to flat-color.
    material_catalog_.Unload();
    {
        // Derive the pack name from the mappack path: "rom:/lvl/<pack>/<pack>.mappack".
        const char* slash = std::strrchr(mappack_path, '/');
        const char* fname = slash ? slash + 1 : mappack_path;
        char pack_name[64] = {};
        std::strncpy(pack_name, fname, sizeof(pack_name) - 1);
        char* dot = std::strrchr(pack_name, '.');
        if (dot) *dot = '\0';
        // MaterialCatalog::Load expects "rom:/lvl/<name>.manifest"; the pack
        // manifest lives at rom:/lvl/<pack>/<pack>.manifest, so pass the
        // "<pack>/<pack>" form.
        char manifest_key[128] = {};
        std::snprintf(manifest_key, sizeof(manifest_key), "%s/%s", pack_name, pack_name);
        if (material_catalog_.Load(manifest_key)) {
            open_world_.SetMaterialCatalog(&material_catalog_);
        } else {
            debugf("[mappack] material catalog load FAILED for %s — flat-color near pass\n",
                   manifest_key);
            open_world_.SetMaterialCatalog(nullptr);
        }
    }

    // Cache the world bounds for any future CPU-side math. With the z-split
    // distant pass removed, the near pass uses a fixed far plane of 800 and
    // does not need a per-map far clip.
    {
        const MapSpecV2& spec = map_runtime_.Spec();
        world_bounds = UnionRoomsAABB(spec.rooms, spec.room_count);
        world_bounds_valid_ = spec.room_count > 0;
    }

    const ActiveRoomView* active = map_runtime_.Active();
    if (!active) {
        debugf("[mappack] MapRuntime active room null after Init\n");
        use_map_pack_ = false;
        map_runtime_.Reset();
        return false;
    }

    baked_level_loaded_ = true;
    // Dispatch the start room's entities into its own ActorWorld.
    DispatchLevelEntities(active->room, actor_world,
                          strawberry_actor, refill_actor, spring_actor);
    actor_world.ResolvePending();
    InitCassetteForRoom(active->room);

    // Boot IS a spawn: reset the player to the start room's spawn.
    // ResetPlayerToRoomStart reads the legacy `room` field, so mirror the
    // active chunk's spawn + collision into `room` before snapping. The
    // active room exposes a compatibility pointer to the global mesh.
    room.player_start = active->room.player_start;
    room.checkpoint = active->room.checkpoint;
    room.coll_mesh = active->room.coll_mesh;

    // Boot from the manifest start_spawn (the 'Start'-named PlayerSpawn), not
    // from the .lvl's last PlayerSpawn, so the boot position is robust across
    // bake re-runs regardless of .lvl entity order. Guarded so a future
    // non-start boot (continue-from-save, debug room) cannot teleport the
    // player to Start.
    const V2SpawnSpec* start = map_runtime_.FindStartSpawn();
    if (start) {
        room.player_start = start->position;
        room.checkpoint = start->position;
    }

    // Debug (baseline capture): boot at the map-center cell's authored spawn
    // instead of the corner Start spawn. Ares cannot take controller input, so
    // we need the player to start where the "still takes" symptom is worst.
    // The center cell's authored spawn is on solid ground, so the existing
    // floor-snap in ResetPlayerToRoomStart handles Y (room.coll_mesh is the
    // global mesh at this point). The first Update's SetActiveByPosition then
    // transitions the active room + re-centers the render ring to the center
    // cell. Gated by kDebugTeleportToMapCenter (revert to false after capture).
    if (kDebugTeleportToMapCenter) {
        const MapSpecV2& spec = map_runtime_.Spec();
        if (spec.room_count > 0) {
            // render-distance A/B walker: prefer the dense "house" cell so the
            // perf capture exercises the worst-case geometry instead of the
            // sparse geometric-center cell. The house is cell_n01_n02 (the
            // biggest building, ~920 faces); the authored "After Tutorial"
            // anchor there is on solid ground. Fall back to the geometric
            // center cell if the house cell isn't in the manifest.
            int best = -1;
            for (int i = 0; i < spec.room_count; ++i) {
                if (std::strncmp(spec.rooms[i].id, "cell_n01_n02",
                                 V2RoomSpec::kIdLen) == 0) { best = i; break; }
            }
            if (best < 0) {
                int min_ix = spec.rooms[0].cell_ix, max_ix = spec.rooms[0].cell_ix;
                int min_iz = spec.rooms[0].cell_iz, max_iz = spec.rooms[0].cell_iz;
                for (int i = 1; i < spec.room_count; ++i) {
                    if (spec.rooms[i].cell_ix < min_ix) min_ix = spec.rooms[i].cell_ix;
                    if (spec.rooms[i].cell_ix > max_ix) max_ix = spec.rooms[i].cell_ix;
                    if (spec.rooms[i].cell_iz < min_iz) min_iz = spec.rooms[i].cell_iz;
                    if (spec.rooms[i].cell_iz > max_iz) max_iz = spec.rooms[i].cell_iz;
                }
                const float center_ix = 0.5f * (float)(min_ix + max_ix);
                const float center_iz = 0.5f * (float)(min_iz + max_iz);
                best = 0;
                float best_d2 = 1e30f;
                for (int i = 0; i < spec.room_count; ++i) {
                    const float dx = (float)spec.rooms[i].cell_ix - center_ix;
                    const float dz = (float)spec.rooms[i].cell_iz - center_iz;
                    const float d2 = dx * dx + dz * dz;
                    if (d2 < best_d2) { best_d2 = d2; best = i; }
                }
            }
            const V2RoomSpec& center_room = spec.rooms[best];
            // Prefer a PlayerSpawn (Start/Anchor) on solid ground; fall back to
            // the first authored spawn in the cell.
            const V2SpawnSpec* spawn = nullptr;
            for (int s = 0; s < center_room.spawn_count; ++s) {
                if (center_room.spawns[s].kind != kSpawnActor) {
                    spawn = &center_room.spawns[s];
                    break;
                }
            }
            if (!spawn && center_room.spawn_count > 0) spawn = &center_room.spawns[0];
            if (spawn) {
                room.player_start = spawn->position;
                room.checkpoint = spawn->position;
                debugf("[mappack] debug teleport -> %s (%d,%d) spawn=(%.2f,%.2f,%.2f)\n",
                       center_room.id, center_room.cell_ix, center_room.cell_iz,
                       (double)spawn->position.x, (double)spawn->position.y,
                       (double)spawn->position.z);
            }
        }
    }

    ResetPlayerToRoomStart();

    // Orient the player + camera toward the map center at boot. The default
    // facing is +Z (player_state.hpp / camera_controller.cpp cold-start
    // forward), but the start spawn is at the +Z edge of Forsaken City and the
    // map extends in -Z. With the default facing, the camera looks AWAY from
    // the map → the forward-wedge resident load (`ResolveForwardWedge`) keeps
    // only the start cell, so the map content in front is not resident until
    // the player turns around. Facing the map center fixes the boot-facing
    // without changing any render logic.
    if (world_bounds_valid_) {
        const Vec3 map_center = {
            (world_bounds.min.x + world_bounds.max.x) * 0.5f,
            0.0f,  // Y is irrelevant for XZ facing
            (world_bounds.min.z + world_bounds.max.z) * 0.5f,
        };
        Vec3 to_center = {
            map_center.x - player.position.x,
            0.0f,
            map_center.z - player.position.z,
        };
        const float len2 = to_center.x * to_center.x + to_center.z * to_center.z;
        if (len2 > 1e-4f) {
            const float inv_len = 1.0f / std::sqrt(len2);
            to_center.x *= inv_len;
            to_center.z *= inv_len;
            player.facing = to_center;
            player.target_facing = to_center;
            player.last_facing = to_center;
            // Override the cold-start camera forward (+Z) so the camera looks
            // into the map. ResetPlayerToRoomStart already called
            // camera_controller.Reset (which set forward to +Z on cold start).
            // OrientForward recomputes target/position from the new forward
            // using the controller's own DesiredLookAt/DesiredPosition, so the
            // next Step() sees a consistent state (no distance/height jump).
            camera.origin = player.position;

            // Temporary reproduction: nudge spawn left and rotate camera by a
            // small fixed angle to match the reported trigger condition.
            if (kDebugSpawnOffsetLeft != 0.0f) {
                // "left" is perpendicular to to_center in world XZ.
                // to_center is the map-center direction; left = (-to_center.z, 0, to_center.x).
                player.position.x += -to_center.z * kDebugSpawnOffsetLeft;
                player.position.z +=  to_center.x * kDebugSpawnOffsetLeft;
                player.prev_position = player.position;
                camera.origin = player.position;
            }
            Vec3 facing = to_center;
            if (kDebugCameraRotateDeg != 0.0f) {
                const float rad = kDebugCameraRotateDeg * (3.14159265358979f / 180.0f);
                const float c = std::cos(rad);
                const float s = std::sin(rad);
                facing = {
                    to_center.x * c - to_center.z * s,
                    0.0f,
                    to_center.x * s + to_center.z * c,
                };
                player.facing = facing;
                player.target_facing = facing;
                player.last_facing = facing;
            }
            camera_controller.OrientForward(camera, facing);
        }
    }

    // Load the render-only neighbor ring for the start cell. The resident set
    // is the full 3×3 Chebyshev-1 ring; the camera direction only reorders
    // cells so the most forward ones are placed first.
    const V2RoomSpec* start_spec = map_runtime_.ActiveSpec();
    if (start_spec) {
        open_world_.SetCenter(map_runtime_.Spec(), *start_spec, CameraForwardDir(), nullptr);
        // render-distance Inc 4: load the whole-map horizon tier once at boot.
        // 45 × ~1.4 KB decimated `.dlod` cells — cheap to keep resident for
        // the map lifetime. Gated by kEnableDistantTier (default OFF — the
        // merged approach regressed perf).
        if (kEnableDistantTier) {
            open_world_.LoadDistantTier(map_runtime_.Spec(), nullptr);
        }
    }

    debugf("[mappack] booted %s: %d rooms, start=%s\n",
           mappack_path, map_runtime_.Spec().room_count,
           map_runtime_.Spec().start_room_id);
    return true;
}

bool GameplayScene::Impl::TransitionToRoom(const char* room_id) {
    if (!room_id || room_id[0] == '\0') {
        debugf("[mappack] transition FAILED: room_id null or empty\n");
        return false;
    }
    // Chunk transition: load + activate WITHOUT resetting the player.
    // Player world position/velocity are preserved (all chunks share world
    // coords). Per the missing-player-start-init common-mistake, we do NOT
    // call ResetPlayerToRoomStart here — that is reserved for death-respawn.
    if (!map_runtime_.CommitActive(room_id)) {
        debugf("[mappack] transition to %s FAILED — staying in current room\n", room_id);
        return false;
    }
    const ActiveRoomView* active = map_runtime_.Active();
    if (!active) {
        debugf("[mappack] Active() returned null after CommitActive(%s)\n", room_id);
        return false;
    }
    // Refresh the render-only neighbor ring to the new cell's 3×3 neighbors.
    // The camera direction only reorders cells so forward cells are first.
    const V2RoomSpec* active_spec = map_runtime_.ActiveSpec();
    if (active_spec) {
        open_world_.SetCenter(map_runtime_.Spec(), *active_spec, CameraForwardDir(), nullptr);
    }
    // Re-dispatch the new active room's entities and re-init cassette.
    actor_world = ActorWorld{};
    DispatchLevelEntities(active->room, actor_world,
                          strawberry_actor, refill_actor, spring_actor);
    actor_world.ResolvePending();
    InitCassetteForRoom(active->room);
    // Mirror the active room's collision into the legacy `room` reference so
    // the existing motor/camera queries (which read `room`) keep working until
    // a later refactor routes them through ActiveRoom(). The active room
    // exposes a compatibility pointer to the global mesh.
    room.coll_mesh = active->room.coll_mesh;
    room.has_cassette = active->room.has_cassette;
    room.cassette = active->room.cassette;
    // Mirror cassette target to legacy room reference.
    room.cassette_target[0] = '\0';
    if (active->room.cassette_target[0] != '\0') {
        std::strncpy(room.cassette_target, active->room.cassette_target, 31);
        room.cassette_target[31] = '\0';
    }
    debugf("[mappack] transitioned to %s\n", room_id);
    return true;
}

void GameplayScene::SetLevel(const char* lvl_path, const char* level_name) {
    lvl_path_   = lvl_path;
    level_name_ = level_name;
    if (impl_) {
        impl_->lvl_path   = lvl_path;
        impl_->level_name = level_name;
    }
}

void GameplayScene::SetMapPack(const char* mappack_path) {
    // Stored; consumed by Init() to boot the multi-room path.
    if (impl_) {
        impl_->mappack_path_ = mappack_path;
    } else {
        // Stash on the outer object for Init to read.
        mappack_path_pending_ = mappack_path;
    }
}

const RenderCounters& GameplayScene::GetRenderCounters() const {
    // The two-pass orchestrator owns the per-frame counters; forward them so
    // the reporting profiler (rom_main.cpp) can print them (Inc 1 / D6).
    // If the scene isn't initialized yet, return a zeroed static fallback
    // (never dereference a null impl_).
    static const RenderCounters kEmpty;
    return impl_ ? impl_->open_world_.Counters() : kEmpty;
}

const n64::FrameProfiler& GameplayScene::Profiler() const {
    // Forward the renderer's per-phase profiler so rom_main can print
    // phase_average_ms(). If the scene isn't initialized, return a zeroed
    // static fallback (never dereference a null impl_).
    static const n64::FrameProfiler kEmpty(60);
    return impl_ ? impl_->open_world_.Profiler() : kEmpty;
}

int GameplayScene::PendingLoads() const {
    // streaming-experience Inc 5: pending-load backlog depth (queued +
    // in-flight) for the [counters] report line. 0 before init.
    return impl_ ? impl_->open_world_.PendingLoads() : 0;
}

int GameplayScene::ResidentLoads() const {
    // Resident pool depth (the 5x5 preload ring that has been set up). 0
    // before init.
    return impl_ ? impl_->open_world_.ResidentLoads() : 0;
}

size_t GameplayScene::FrameArenaUsed() const {
    // memory-opt Inc 1: [memory] bucket — frame-arena bytes in use.
    return impl_ ? impl_->open_world_.Arena().Used() : 0;
}

size_t GameplayScene::StreamingStagingBytes() const {
    // memory-opt Inc 1: [memory] bucket — staging pool bytes.
    return impl_ ? impl_->open_world_.StreamingStagingBytes() : 0;
}

size_t GameplayScene::StreamingResidentBytes() const {
    // memory-opt Inc 1: [memory] bucket — resident renderer object bytes.
    return impl_ ? impl_->open_world_.StreamingResidentBytes() : 0;
}

size_t GameplayScene::PoolHighWater() const {
    // memory-opt Inc 4: [memory] bucket — renderer-pool high-water.
    return impl_ ? impl_->open_world_.PoolHighWater() : 0;
}

size_t GameplayScene::FrameArenaPeak() const {
    // memory-opt Inc 4: [memory] bucket — frame-arena peak since boot.
    return impl_ ? impl_->open_world_.FrameArenaPeak() : 0;
}

size_t GameplayScene::LiveCollMeshBytes() const {
    // memory-opt Inc 1: [memory] bucket — active room's collision mesh bytes.
    // Pre-init-safe: resolve the active room without dereferencing an
    // uninitialized map runtime (the report only fires post-boot, but the
    // accessor is hardened anyway).
    if (!impl_) return 0;
    const physics::CollMesh* mesh = nullptr;
    if (impl_->map_runtime_.HasMap()) {
        if (impl_->map_runtime_.Active()) {
            mesh = impl_->map_runtime_.Active()->room.coll_mesh;
        }
    } else {
        mesh = impl_->room.coll_mesh;
    }
    return mesh ? mesh->UsedBytes() : 0;
}

size_t GameplayScene::MapSpecBytes() const {
    // memory-opt Inc 1: [memory] bucket — map-pack manifest struct footprint.
    if (!impl_) return 0;
    const MapSpecV2& spec = impl_->map_runtime_.Spec();
    return impl_->map_runtime_.HasMap() ? sizeof(spec) : 0;
}

void GameplayScene::Init() {
    impl_ = new Impl();
    impl_->lvl_path   = lvl_path_;
    impl_->level_name = level_name_;

    t3d_vec3_norm(&impl_->light_direction);

    impl_->cube_vertices = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked) * 12));
    BuildCubeGeometry(impl_->cube_vertices, 0x888888FF);

    impl_->player_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    impl_->collectible_render.matrix_fp = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));

    impl_->debug_hud.Init();
    if (!impl_->strawberry_model.Load("rom:/mdl/strawberry.t3dm"))
        debugf("[init] WARNING: strawberry model missing\n");
    if (!impl_->cassette_model.Load(kCassetteModelPath))
        debugf("[init] WARNING: cassette model missing\n");
    if (!impl_->madeline_model.Load(kMadelineModelPath))
        debugf("[init] FATAL: madeline model missing at %s\n", kMadelineModelPath);
    if (!impl_->room_fixture_model.Load("rom:/mdl/room_fixture.t3dm"))
        debugf("[init] WARNING: room fixture model missing\n");
    impl_->room_fixture_model.UpdateMatrix({0.0f, 40.0f, -120.0f}, 60.0f, 0.0f);
    impl_->room_fixture_model.UpdateMatrix({0.0f, 40.0f, -120.0f}, 60.0f, 0.0f);

    // Boot path: if a map-pack was set (SetMapPack), boot the multi-room
    // world; else fall back to the legacy single-room .lvl path.
    debugf("[init] ENTRY: mappack_path_pending_=%s lvl_path_=%s\n",
           mappack_path_pending_ ? mappack_path_pending_ : "(null)",
           lvl_path_ ? lvl_path_ : "(null)");
    if (mappack_path_pending_ != nullptr) {
        impl_->mappack_path_ = mappack_path_pending_;
        debugf("[init] Calling BootMapPack(%s)\n", mappack_path_pending_);
        if (!impl_->BootMapPack(mappack_path_pending_)) {
            debugf("[init] BootMapPack FAILED — falling back to single-room\n");
            // Boot failed — fall back to single-room.
            impl_->ReloadBakedLevel();
        } else {
            debugf("[init] BootMapPack SUCCEEDED — use_map_pack_ should be true\n");
        }
    } else {
        debugf("[init] No mappack pending — using single-room path\n");
        impl_->ReloadBakedLevel();
    }

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
        impl_->collectible.pickup_radius = 15.0f;
    }
}

void GameplayScene::Shutdown() {
    if (impl_ == nullptr) return;

    impl_->debug_hud.Shutdown();
    impl_->strawberry_model.Free();
    impl_->cassette_model.Free();
    impl_->madeline_model.Free();
    impl_->room_fixture_model.Free();
    impl_->room_renderer.Free();

    // Free the multi-room map-pack runtime (frees the global collision mesh +
    // active room renderer). Safe to call when not using a map-pack (no-op).
    impl_->map_runtime_.Reset();
    impl_->material_catalog_.Unload();

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

    // Buffered viewports allocate an uncached _matFP ring that `delete impl_`
    // will not free (Impl has no destructor). Safe no-op when _matFP is null.
    t3d_viewport_destroy(&impl_->viewport);

    delete impl_;
    impl_ = nullptr;
}

void GameplayScene::Update(float delta_seconds) {
    if (impl_ == nullptr) return;
    impl_->last_delta = delta_seconds;  // streaming-experience Inc 4: for the reveal age

    // Open the renderer's per-frame profiler + reset arena/counters at the TOP
    // of Update (Inc 1 / instrumentation, D1). This MUST precede SetCenter/
    // transitions (which emit kPhaseStreaming) so the streaming ticks are not
    // wiped by BeginFrame's reset. EndFrame closes the span at the tail of
    // Render, spanning Update + Render.
    impl_->open_world_.BeginFrame();

    // Sample the controller once per render frame; same snapshot replayed
    // across all fixed-step substeps (§34: capture raw input once).
    joypad_poll();
    impl_->input_system.Poll(delta_seconds);

    // Start (N64) toggles the room fixture overlay (debug aid). The spec maps
    // Start -> Pause/menu; the fixture toggle is kept as a temporary stand-in
    // until a full pause menu exists.
    if (impl_->input_system.Pause().pressed) {
        impl_->room_fixture_visible_ = !impl_->room_fixture_visible_;
        debugf("[fixture] room fixture %s\n", impl_->room_fixture_visible_ ? "ON" : "OFF");
    }
    PlayerInput input = ReadPlayerInput(impl_->input_system);
    const CameraInput camera_input = ReadCameraInput(impl_->input_system);

    // Temporary debug: auto-walk sideways so Ares can reproduce the
    // side-cell loading glitch without controller input. Gated by kDebugAutoWalk.
    // The walk is limited to 100 world units so the scene stabilizes for inspection.
    if (kDebugAutoWalk) {
        if (!impl_->autowalk_recording_) {
            impl_->autowalk_start_ = impl_->player.position;
            impl_->autowalk_recording_ = true;
        }
        const Vec3 delta = {
            impl_->player.position.x - impl_->autowalk_start_.x,
            impl_->player.position.y - impl_->autowalk_start_.y,
            impl_->player.position.z - impl_->autowalk_start_.z,
        };
        const float dist = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        if (dist < kDebugAutoWalkDistance) {
            input.move = {1.0f, 0.0f};  // camera-relative right/left (sideways)
        }
    }
    const Vec3 camera_forward = {
        impl_->camera.target.x - impl_->camera.position.x,
        impl_->camera.target.y - impl_->camera.position.y,
        impl_->camera.target.z - impl_->camera.position.z,
    };

    // Reset collision query counters at the start of each frame so the
    // telemetry line reports only this frame's work.
    impl_->map_runtime_.GlobalCollision().ResetCounters();
    impl_->room.query_counters = &impl_->map_runtime_.GlobalCollision().Counters();

    // Fixed-step physics loop.
    const int n_ticks = impl_->fixed_step.BeginFrame(delta_seconds);
    const uint32_t input_record_frame = impl_->input_record_frame_++;
    // Inc 1 / D5: per-frame serial debugf is gated behind kVerboseFrameLogging
    // (default false) — USB output blocks the RSP and is a hidden per-frame
    // cost. Boot/init logs and the profiler report stay ungated.
    if (kVerboseFrameLogging) {
        debugf("[update] frame start: coll_mesh=%p n_ticks=%d\n",
               (void*)impl_->room.coll_mesh, n_ticks);
    }
    impl_->player.prev_position = impl_->player.position;

    // Multi-room: check if the player crossed a chunk boundary this frame and
    // transition to the new active room (preserves player pos/velocity).
    if (impl_->use_map_pack_) {
        const char* new_room_id = nullptr;
        if (impl_->map_runtime_.SetActiveByPosition(impl_->player.position, &new_room_id)) {
            if (new_room_id && new_room_id[0] != '\0') {
                impl_->TransitionToRoom(new_room_id);
            }
        }
    }

    MotorResult motor_result = {};
    // Last tick's pre-move grounded (review S5): refreshed every tick inside
    // the loop so multi-tick frames attribute the snap signal to the tick
    // that actually made the transition (old per-tick motor snapshot parity).
    bool was_grounded_pre_last_tick = impl_->player.contact.was_grounded;
    bool did_respawn = false;

    for (int tick = 0; tick < n_ticks; ++tick) {
        PlayerInput tick_input = input;
        const char* physics_phase = "live";
        uint32_t physics_tick = 0;
        if (kDebugPhysicsAutoplay) {
            physics_tick = impl_->physics_autoplay_tick_++;
            tick_input = {};
            const auto WorldToCameraMove = [&](const Vec3& direction) {
                const Vec3 forward = NormalizeXZ(camera_forward, direction);
                const Vec3 right = {forward.z, 0.0f, -forward.x};
                return Vec2{
                    direction.x * right.x + direction.z * right.z,
                    direction.x * forward.x + direction.z * forward.z,
                };
            };
            if (physics_tick < 60) {
                physics_phase = "idle";
            } else if (physics_tick < 84) {
                physics_phase = "run";
                tick_input.move = {0.0f, 1.0f};
            } else if (physics_tick < 100) {
                physics_phase = "brake";
            } else if (physics_tick < 124) {
                physics_phase = "reverse";
                tick_input.move = {0.0f, -1.0f};
            } else if (physics_tick < 150) {
                physics_phase = "brake";
            } else if (physics_tick == 150) {
                physics_phase = "jump-press";
                tick_input.move = {0.0f, 1.0f};
                tick_input.jump_pressed = true;
                tick_input.jump_held = true;
                impl_->physics_autoplay_jump_start_y_ = impl_->player.position.y;
                impl_->physics_autoplay_jump_peak_y_ = impl_->player.position.y;
            } else if (physics_tick < 170) {
                physics_phase = "jump-hold";
                tick_input.move = {0.0f, 1.0f};
                tick_input.jump_held = true;
            } else if (physics_tick < 176) {
                physics_phase = "air-steer";
                tick_input.move = {1.0f, 0.0f};
            } else if (physics_tick < 180) {
                physics_phase = "air-release";
            } else if (physics_tick == 180) {
                physics_phase = "dash-press";
                tick_input.move = {1.0f, 0.0f};
                tick_input.dash_pressed = true;
                impl_->physics_autoplay_dash_start_ = impl_->player.position;
            } else if (physics_tick < 200) {
                physics_phase = "dash-steer";
                tick_input.move = {1.0f, 0.0f};
            } else if (physics_tick < 300) {
                physics_phase = "recover";
            } else if (physics_tick == 300) {
                physics_phase = "climb-entry";
                const Vec3 waist = {
                    impl_->player.position.x,
                    impl_->player.position.y - impl_->movement_config.player_half_height +
                        impl_->movement_config.climb_waist_offset,
                    impl_->player.position.z,
                };
                WallHit wall{};
                Vec3 wall_direction{};
                float wall_search_distance = 0.0f;
                float wall_supported_height = 0.0f;
                int wall_height_band = 0;
                int short_wall_rejections = 0;
                float maximum_supported_height = 0.0f;
                float maximum_supported_by_band[5] = {};
                if (kDebugPhysicsAutoplayCorner) {
                    // Match the no-teleport host route: approach the real
                    // vertical face at (x=10,z=38.4), then traverse toward its
                    // shared edge. Query the baked collision mesh at the
                    // intended waist; never assign player.position here.
                    constexpr float kCornerApproachX = 10.0f;
                    constexpr float kCornerFaceZ = 38.4f;
                    const Vec3 probe = {
                        kCornerApproachX, waist.y,
                        kCornerFaceZ + impl_->movement_config.climb_check_dist,
                    };
                    wall = QueryClimbWall(
                        impl_->room, probe,
                        impl_->movement_config.climb_check_dist,
                        {0.0f, 0.0f, 1.0f});
                    if (wall.hit && wall.plane_normal.z > 0.8f) {
                        wall_direction = NormalizeXZ(
                            {probe.x - waist.x, 0.0f, probe.z - waist.z});
                        const float dx = probe.x - waist.x;
                        const float dz = probe.z - waist.z;
                        wall_search_distance = std::sqrt(dx * dx + dz * dz);
                        for (float height = 3.0f; height <= 48.0f;
                             height += 3.0f) {
                            const WallHit above = QueryClimbWall(
                                impl_->room,
                                {wall.point.x, wall.point.y + height,
                                 wall.point.z},
                                impl_->movement_config.climb_check_dist,
                                {0.0f, 0.0f, 1.0f});
                            const float alignment = DotXZ(
                                above.plane_normal, wall.plane_normal);
                            if (!above.hit || alignment < 0.8f) break;
                            wall_supported_height = height;
                        }
                        wall_height_band = 0;
                    } else {
                        wall = {};
                    }
                } else {
                    constexpr float kTau = 6.28318530717958647692f;
                    constexpr int kDirectionSamples = 16;
                    constexpr float kHeightOffsets[] = {0.0f, 4.0f, -4.0f, 8.0f, -8.0f};
                    for (float distance = 2.0f; distance <= 120.0f && !wall.hit;
                         distance += 4.0f) {
                        for (int band = 0; band < 5 && !wall.hit; ++band) {
                            for (int sample = 0; sample < kDirectionSamples && !wall.hit;
                                 ++sample) {
                                const float angle = kTau * static_cast<float>(sample) /
                                                    static_cast<float>(kDirectionSamples);
                                const Vec3 direction = {std::cos(angle), 0.0f, std::sin(angle)};
                                const Vec3 probe = {
                                    waist.x + direction.x * distance,
                                    waist.y + kHeightOffsets[band],
                                    waist.z + direction.z * distance,
                                };
                                const WallHit candidate = QueryClimbWall(
                                    impl_->room, probe,
                                    impl_->movement_config.climb_check_dist,
                                    {-direction.x, 0.0f, -direction.z});
                                if (candidate.hit &&
                                    DotXZ(candidate.plane_normal, direction) < -0.5f) {
                                    float supported_height = 0.0f;
                                    for (float height = 3.0f; height <= 48.0f;
                                         height += 3.0f) {
                                        const WallHit above = QueryClimbWall(
                                            impl_->room,
                                            {candidate.point.x,
                                             candidate.point.y + height,
                                             candidate.point.z},
                                            impl_->movement_config.climb_check_dist,
                                            {-direction.x, 0.0f, -direction.z});
                                        const float normal_alignment =
                                            above.plane_normal.x * candidate.plane_normal.x +
                                            above.plane_normal.z * candidate.plane_normal.z;
                                        if (!above.hit || normal_alignment < 0.8f) {
                                            break;
                                        }
                                        supported_height = height;
                                    }
                                    maximum_supported_height = std::max(
                                        maximum_supported_height, supported_height);
                                    maximum_supported_by_band[band] = std::max(
                                        maximum_supported_by_band[band], supported_height);
                                    const bool supports_climb_height = supported_height >= 9.0f;
                                    if (!supports_climb_height) {
                                        ++short_wall_rejections;
                                        continue;
                                    }
                                    wall = candidate;
                                    wall_direction = direction;
                                    wall_search_distance = distance;
                                    wall_supported_height = supported_height;
                                    wall_height_band = band;
                                }
                            }
                        }
                    }
                }
                if (wall.hit) {
                    impl_->physics_autoplay_climb_target_point_ = wall.point;
                    impl_->physics_autoplay_climb_target_normal_ = wall.plane_normal;
                    impl_->physics_autoplay_approach_direction_ =
                        kDebugPhysicsAutoplayCorner
                            ? wall_direction
                            : NormalizeXZ({-wall.plane_normal.x, 0.0f,
                                           -wall.plane_normal.z}, wall_direction);
                    impl_->physics_autoplay_target_start_gap_ = physics::Dot(
                        wall.plane_normal, physics::Sub(waist, wall.point));
                    impl_->physics_autoplay_target_found_ = true;
                    debugf("[phys-test] climb-target hit=1 height=%.0f max_height=%.0f face=%d range=%.1f band=%d "
                           "start_gap=%.1f approach=(%.2f,%.2f) point=(%.1f,%.1f,%.1f) n=(%.2f,%.2f,%.2f)\n",
                           static_cast<double>(wall_supported_height),
                           static_cast<double>(maximum_supported_height),
                           wall.face_id,
                           static_cast<double>(wall_search_distance), wall_height_band,
                           static_cast<double>(impl_->physics_autoplay_target_start_gap_),
                           static_cast<double>(impl_->physics_autoplay_approach_direction_.x),
                           static_cast<double>(impl_->physics_autoplay_approach_direction_.z),
                           static_cast<double>(wall.point.x),
                           static_cast<double>(wall.point.y),
                           static_cast<double>(wall.point.z),
                           static_cast<double>(wall.plane_normal.x),
                           static_cast<double>(wall.plane_normal.y),
                           static_cast<double>(wall.plane_normal.z));
                } else {
                    debugf("[phys-test] climb-target hit=0 max_height=%.0f bands=(%.0f,%.0f,%.0f,%.0f,%.0f) short=%d "
                           "pos=(%.1f,%.1f,%.1f) waist=(%.1f,%.1f,%.1f)\n",
                           static_cast<double>(maximum_supported_height),
                           static_cast<double>(maximum_supported_by_band[0]),
                           static_cast<double>(maximum_supported_by_band[1]),
                           static_cast<double>(maximum_supported_by_band[2]),
                           static_cast<double>(maximum_supported_by_band[3]),
                           static_cast<double>(maximum_supported_by_band[4]),
                           short_wall_rejections,
                           static_cast<double>(impl_->player.position.x),
                           static_cast<double>(impl_->player.position.y),
                           static_cast<double>(impl_->player.position.z),
                           static_cast<double>(waist.x),
                           static_cast<double>(waist.y),
                           static_cast<double>(waist.z));
                }
                if (wall.hit) {
                    physics_phase = "climb-approach";
                    tick_input.move = WorldToCameraMove(
                        impl_->physics_autoplay_approach_direction_);
                }
            } else if (physics_tick <= 480) {
                if (!impl_->physics_autoplay_target_found_) {
                    physics_phase = "climb-no-target";
                } else if (impl_->player.movement_state == PlayerMovementState::Climbing) {
                    if (kDebugPhysicsAutoplayCorner) {
                        tick_input.climb_held = true;
                        if (impl_->physics_autoplay_corner_seen_) {
                            physics_phase = impl_->player.climb.corner_ease > 0.0f
                                ? "corner-ease" : "corner-stable";
                            tick_input.move = impl_->player.climb.corner_ease > 0.0f
                                ? WorldToCameraMove({-1.0f, 0.0f, 0.0f})
                                : Vec2{};
                        } else if (impl_->physics_autoplay_climb_ticks_ <= 1) {
                            physics_phase = "corner-first-step";
                            tick_input.move = {0.0f, 1.0f};
                        } else {
                            physics_phase = "corner-traverse";
                            tick_input.move = WorldToCameraMove(
                                {-1.0f, 0.0f, 0.0f});
                        }
                    } else if (!kDebugPhysicsAutoplayMantle &&
                        !impl_->physics_autoplay_walljump_attempted_ &&
                        impl_->physics_autoplay_climb_ticks_ >= 10) {
                        // Exercise climb-walljump on the real-map route before
                        // the selected wall's short upper lip can trigger a
                        // mantle. Ten completed climb ticks still satisfy the
                        // sustained-climb and clearance gates.
                        physics_phase = "wall-jump";
                        tick_input.jump_pressed = true;
                        tick_input.climb_held = true;
                        impl_->physics_autoplay_walljump_attempted_ = true;
                    } else {
                        physics_phase = "climb-up";
                        tick_input.move = {0.0f, 1.0f};
                        tick_input.climb_held = true;
                    }
                } else if (!impl_->physics_autoplay_climb_started_) {
                    const Vec3 waist = {
                        impl_->player.position.x,
                        impl_->player.position.y - impl_->movement_config.player_half_height +
                            impl_->movement_config.climb_waist_offset,
                        impl_->player.position.z,
                    };
                    const float target_gap = physics::Dot(
                        impl_->physics_autoplay_climb_target_normal_,
                        physics::Sub(waist, impl_->physics_autoplay_climb_target_point_));
                    tick_input.move = WorldToCameraMove(
                        impl_->physics_autoplay_approach_direction_);
                    if (target_gap <= impl_->movement_config.climb_check_dist) {
                        physics_phase = "climb-entry";
                        if (kDebugPhysicsAutoplayCorner) tick_input.move = {};
                        tick_input.climb_held = true;
                        ++impl_->physics_autoplay_approach_ticks_;
                        if (!impl_->physics_autoplay_entry_attempted_) {
                            impl_->physics_autoplay_entry_attempted_ = true;
                            impl_->physics_autoplay_entry_gap_ = target_gap;
                            debugf("[phys-test] climb-entry gap=%.2f ticks=%u pos=(%.1f,%.1f,%.1f)\n",
                                   static_cast<double>(target_gap),
                                   static_cast<unsigned int>(impl_->physics_autoplay_approach_ticks_),
                                   static_cast<double>(impl_->player.position.x),
                                   static_cast<double>(impl_->player.position.y),
                                   static_cast<double>(impl_->player.position.z));
                        }
                    } else {
                        physics_phase = "climb-approach";
                        ++impl_->physics_autoplay_approach_ticks_;
                    }
                } else {
                    physics_phase = "climb-exit";
                }
            } else {
                physics_phase = "done";
            }

            const Vec3 camera_right = NormalizeXZ(
                {camera_forward.z, 0.0f, -camera_forward.x},
                {1.0f, 0.0f, 0.0f});
            if (physics_tick == 170) {
                impl_->physics_autoplay_air_steer_start_axis_speed_ = DotXZ(
                    impl_->player.velocity, camera_right);
                impl_->physics_autoplay_air_window_grounded_ =
                    impl_->player.grounded;
            } else if (physics_tick == 176) {
                impl_->physics_autoplay_air_steer_end_axis_speed_ = DotXZ(
                    impl_->player.velocity, camera_right);
                impl_->physics_autoplay_air_release_start_speed_ = LengthXZ(
                    impl_->player.velocity);
            }
        }
        // og-mover-overhaul Inc 2: PlayerMotor is deleted. The tick pipeline is
        // now OG's shape: moving-surface carry -> TimerInput/State (velocity
        // edits only) -> MovePhase (SweepMove + Popout + ground check + snap).
        impl_->player.prev_position = impl_->player.position;
        AdvanceMovingSurfaces(impl_->room, FixedStepAccumulator::kTickDt);
        impl_->player_controller.MovingSurfacePhase(impl_->player, impl_->room);
        // Per-tick snapshot (review S5): the old code re-captured before the
        // motor step each tick, so the telemetry arg means "grounded at the
        // start of THIS tick", not at frame start.
        const bool was_grounded_pre_tick = impl_->player.contact.was_grounded;
        was_grounded_pre_last_tick = was_grounded_pre_tick;
        const Vec3 physics_tick_start_position = impl_->player.position;
        // Controller: timers/input + state handler edit velocity only.
        PlayerController::StepContext player_step = impl_->player_controller.TimerInputPhase(
            impl_->player, tick_input, camera_forward, FixedStepAccumulator::kTickDt);
        const bool was_climbing_pre =
            impl_->player.movement_state == PlayerMovementState::Climbing;
        const bool was_dashing_pre =
            impl_->player.movement_state == PlayerMovementState::Dashing;
        impl_->player_controller.StatePhase(impl_->player, tick_input, player_step, FixedStepAccumulator::kTickDt, &impl_->room);
        const float ground_snap_after_state =
            impl_->player.contact.ground_snap_cooldown_remaining;
        if (kDebugPhysicsAutoplay && physics_tick == 180 && tick_input.dash_pressed) {
            const PlayerState& p = impl_->player;
            impl_->physics_autoplay_dash_launch_recorded_ =
                p.movement_state == PlayerMovementState::Dashing && !was_dashing_pre;
            impl_->physics_autoplay_dash_airborne_ =
                impl_->physics_autoplay_dash_launch_recorded_ && !p.grounded;
            impl_->physics_autoplay_dash_launch_vy_ = p.velocity.y;
            impl_->physics_autoplay_dash_launch_speed_ = std::sqrt(
                p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y +
                p.velocity.z * p.velocity.z);
        }


        // MovePhase applies velocity*dt through the single mover and resolves
        // contact. `grounded` reflects the post-move ground state.
        impl_->player_controller.MovePhase(impl_->player, FixedStepAccumulator::kTickDt, &impl_->room);

        if (kDebugPhysicsAutoplayCorner) {
            if (player_step.corner_triggered) {
                ++impl_->physics_autoplay_corner_events_;
                impl_->physics_autoplay_corner_seen_ = true;
                impl_->physics_autoplay_corner_target_normal_ =
                    impl_->player.climb.corner_normal_to;
                debugf("[phys-test] corner event=%u from=(%.2f,%.2f,%.2f) "
                       "to=(%.2f,%.2f,%.2f) ease=%.3f pos=(%.1f,%.1f,%.1f)\n",
                       static_cast<unsigned int>(impl_->physics_autoplay_corner_events_),
                       static_cast<double>(impl_->player.climb.corner_normal_from.x),
                       static_cast<double>(impl_->player.climb.corner_normal_from.y),
                       static_cast<double>(impl_->player.climb.corner_normal_from.z),
                       static_cast<double>(impl_->player.climb.corner_normal_to.x),
                       static_cast<double>(impl_->player.climb.corner_normal_to.y),
                       static_cast<double>(impl_->player.climb.corner_normal_to.z),
                       static_cast<double>(impl_->player.climb.corner_ease),
                       static_cast<double>(impl_->player.position.x),
                       static_cast<double>(impl_->player.position.y),
                       static_cast<double>(impl_->player.position.z));
            }
            if (impl_->physics_autoplay_corner_seen_) {
                const Vec3 delta = physics::Sub(
                    impl_->player.position, physics_tick_start_position);
                const float step = std::sqrt(
                    delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                impl_->physics_autoplay_corner_max_step_ = std::max(
                    impl_->physics_autoplay_corner_max_step_, step);
                const bool on_target_wall =
                    impl_->player.movement_state == PlayerMovementState::Climbing &&
                    impl_->player.climb.corner_ease <= 0.001f &&
                    DotXZ(impl_->player.climb.wall_normal,
                          impl_->physics_autoplay_corner_target_normal_) > 0.8f;
                impl_->physics_autoplay_corner_arrived_ |= on_target_wall;
                if (on_target_wall) {
                    ++impl_->physics_autoplay_corner_stable_ticks_;
                } else {
                    impl_->physics_autoplay_corner_stable_ticks_ = 0;
                }
            }
        }

        if (kDebugPhysicsAutoplay && physics_tick >= 170 && physics_tick < 180) {
            if (impl_->player.grounded) {
                impl_->physics_autoplay_air_window_grounded_ = true;
            }
            if (physics_tick >= 176) {
                impl_->physics_autoplay_air_release_min_speed_ = std::min(
                    impl_->physics_autoplay_air_release_min_speed_,
                    LengthXZ(impl_->player.velocity));
            }
        }

        // The aggregate 10-tick climb gate hid a canceled first step. Capture
        // grounded entry and the first sustained climb tick separately so
        // the Ares self-check fails on the same regression as the host test.
        if (kDebugPhysicsAutoplay && !was_climbing_pre &&
            impl_->player.movement_state == PlayerMovementState::Climbing &&
            !impl_->physics_autoplay_climb_entry_recorded_) {
            impl_->physics_autoplay_climb_entry_recorded_ = true;
            impl_->physics_autoplay_climb_entry_grounded_ = was_grounded_pre_tick;
            impl_->physics_autoplay_climb_entry_y_ = impl_->player.position.y;
        } else if (kDebugPhysicsAutoplay && was_climbing_pre &&
                   impl_->player.movement_state == PlayerMovementState::Climbing &&
                   impl_->physics_autoplay_climb_entry_recorded_ &&
                   !impl_->physics_autoplay_climb_first_step_sampled_) {
            impl_->physics_autoplay_climb_first_step_sampled_ = true;
            impl_->physics_autoplay_climb_first_step_dy_ =
                impl_->player.position.y - impl_->physics_autoplay_climb_entry_y_;
            impl_->physics_autoplay_climb_first_step_snap_cooldown_ =
                ground_snap_after_state;
        }

        if (kDebugPhysicsAutoplay && impl_->physics_autoplay_walljump_attempted_ &&
            was_climbing_pre &&
            impl_->player.movement_state != PlayerMovementState::Climbing &&
            impl_->player.velocity.y > 0.0f) {
            impl_->physics_autoplay_walljumped_ = true;
            impl_->physics_autoplay_walljump_speed_xy_ = LengthXZ(impl_->player.velocity);
            impl_->physics_autoplay_walljump_speed_y_ = impl_->player.velocity.y;
            impl_->physics_autoplay_walljump_outward_speed_ = DotXZ(
                impl_->player.velocity,
                NormalizeXZ(impl_->physics_autoplay_climb_target_normal_));
        }

        if (kDebugPhysicsAutoplay) {
            const PlayerState& p = impl_->player;
            const float speed_xz = std::sqrt(p.velocity.x * p.velocity.x +
                                             p.velocity.z * p.velocity.z);
            if (physics_tick >= 60 && physics_tick < 84) {
                impl_->physics_autoplay_run_peak_speed_ = std::max(
                    impl_->physics_autoplay_run_peak_speed_, speed_xz);
                if (speed_xz > 1.0f) {
                    impl_->physics_autoplay_run_direction_ = {
                        p.velocity.x / speed_xz, 0.0f, p.velocity.z / speed_xz};
                }
            } else if (physics_tick >= 90 && physics_tick < 100) {
                impl_->physics_autoplay_brake_min_speed_ = std::min(
                    impl_->physics_autoplay_brake_min_speed_, speed_xz);
            } else if (physics_tick >= 108 && physics_tick < 124) {
                const float along_run = p.velocity.x * impl_->physics_autoplay_run_direction_.x +
                                        p.velocity.z * impl_->physics_autoplay_run_direction_.z;
                impl_->physics_autoplay_reverse_speed_ = std::max(
                    impl_->physics_autoplay_reverse_speed_, -along_run);
            }
            if (physics_tick >= 150 && physics_tick < 180) {
                impl_->physics_autoplay_jump_peak_y_ = std::max(
                    impl_->physics_autoplay_jump_peak_y_, p.position.y);
            }
            if (physics_tick >= 180 && physics_tick < 200) {
                const float dx = p.position.x - impl_->physics_autoplay_dash_start_.x;
                const float dy = p.position.y - impl_->physics_autoplay_dash_start_.y;
                const float dz = p.position.z - impl_->physics_autoplay_dash_start_.z;
                impl_->physics_autoplay_dash_distance_ = std::max(
                    impl_->physics_autoplay_dash_distance_,
                    std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }

        if (kDebugPhysicsAutoplay &&
            impl_->player.movement_state == PlayerMovementState::Climbing) {
            impl_->physics_autoplay_climb_started_ = true;
            ++impl_->physics_autoplay_climb_ticks_;

            const Vec3 feet = {
                impl_->player.position.x,
                impl_->player.position.y - impl_->movement_config.player_half_height,
                impl_->player.position.z,
            };
            const float required_gap = kMadelineModelWallGap;
            const auto record_model_gap = [&](float model_gap) {
                ++impl_->physics_autoplay_clearance_samples_;
                impl_->physics_autoplay_min_model_gap_ = std::min(
                    impl_->physics_autoplay_min_model_gap_, model_gap);
                if (model_gap < required_gap - 0.01f) {
                    ++impl_->physics_autoplay_clearance_violations_;
                }
            };
            if (impl_->player.climb.corner_ease > 0.0f) {
                const Vec3 normal_a = impl_->player.climb.corner_normal_from;
                const Vec3 normal_b = impl_->player.climb.corner_normal_to;
                const float anchor_y = impl_->movement_config.player_half_height -
                                       impl_->movement_config.climb_waist_offset;
                const Vec3 plane_a = {
                    impl_->player.climb.corner_from.x -
                        normal_a.x * impl_->movement_config.climb_pushout_dist,
                    impl_->player.climb.corner_from.y -
                        normal_a.y * impl_->movement_config.climb_pushout_dist - anchor_y,
                    impl_->player.climb.corner_from.z -
                        normal_a.z * impl_->movement_config.climb_pushout_dist,
                };
                const Vec3 plane_b = {
                    impl_->player.climb.corner_to.x -
                        normal_b.x * impl_->movement_config.climb_pushout_dist,
                    impl_->player.climb.corner_to.y -
                        normal_b.y * impl_->movement_config.climb_pushout_dist - anchor_y,
                    impl_->player.climb.corner_to.z -
                        normal_b.z * impl_->movement_config.climb_pushout_dist,
                };
                const float gap_a = physics::Dot(
                    normal_a, physics::Sub(feet, plane_a));
                const float gap_b = physics::Dot(
                    normal_b, physics::Sub(feet, plane_b));
                const Vec3 render_offset = PlayerModelCornerHorizontalOffset(
                    normal_a, gap_a, normal_b, gap_b, kMadelineModelRadius,
                    kMadelineModelHeight, required_gap);
                record_model_gap(PlayerModelMinimumPlaneGapAfterTranslation(
                    normal_a, gap_a, kMadelineModelRadius,
                    kMadelineModelHeight, render_offset));
                record_model_gap(PlayerModelMinimumPlaneGapAfterTranslation(
                    normal_b, gap_b, kMadelineModelRadius,
                    kMadelineModelHeight, render_offset));
            } else {
                const Vec3 normal = impl_->player.climb.wall_normal;
                const Vec3 waist = {
                    feet.x,
                    feet.y + impl_->movement_config.climb_waist_offset,
                    feet.z,
                };
                const WallHit wall = QueryClimbWall(
                    impl_->ActiveRoom(), waist,
                    impl_->movement_config.climb_check_dist, normal);
                Vec3 clearance_normal = normal;
                float feet_wall_gap = impl_->movement_config.climb_pushout_dist -
                    impl_->movement_config.climb_waist_offset * normal.y;
                if (wall.hit) {
                    clearance_normal = wall.plane_normal;
                    feet_wall_gap = physics::Dot(
                        clearance_normal, physics::Sub(feet, wall.point));
                }
                const float render_offset = PlayerModelHorizontalOffset(
                    clearance_normal, feet_wall_gap, kMadelineModelRadius,
                    kMadelineModelHeight, required_gap);
                record_model_gap(PlayerModelMinimumPlaneGap(
                    clearance_normal, feet_wall_gap, kMadelineModelRadius,
                    kMadelineModelHeight, render_offset));
            }
        }

        if (kDebugPhysicsAutoplay && player_step.mantle_triggered) {
            impl_->physics_autoplay_mantled_ = true;
        }

        if (kDebugPhysicsAutoplay && !impl_->physics_autoplay_result_emitted_ &&
            (impl_->physics_autoplay_walljumped_ || impl_->physics_autoplay_mantled_ ||
             (kDebugPhysicsAutoplayCorner &&
              impl_->physics_autoplay_corner_stable_ticks_ >= 10) ||
             physics_tick >= 480)) {
            const PlayerState& p = impl_->player;
            impl_->physics_autoplay_result_emitted_ = true;
            const float required_gap = kMadelineModelWallGap;
            const bool climb_ok = impl_->physics_autoplay_climb_started_ &&
                                  impl_->physics_autoplay_climb_ticks_ >= 10;
            const bool grounded_climb_entry_ok =
                impl_->physics_autoplay_climb_entry_recorded_ &&
                impl_->physics_autoplay_climb_entry_grounded_;
            const bool climb_first_step_ok =
                impl_->physics_autoplay_climb_first_step_sampled_ &&
                impl_->physics_autoplay_climb_first_step_dy_ >= 0.5f &&
                impl_->physics_autoplay_climb_first_step_snap_cooldown_ > 0.0f;
            const bool approach_ok = impl_->physics_autoplay_entry_attempted_ &&
                impl_->physics_autoplay_approach_ticks_ > 0 &&
                impl_->physics_autoplay_entry_gap_ <=
                    impl_->movement_config.climb_check_dist + 0.01f &&
                impl_->physics_autoplay_target_start_gap_ -
                    impl_->physics_autoplay_entry_gap_ >= 4.0f;
            const bool clearance_ok = impl_->physics_autoplay_clearance_samples_ >= 10 &&
                impl_->physics_autoplay_clearance_violations_ == 0 &&
                impl_->physics_autoplay_min_model_gap_ >= required_gap - 0.01f;
            const bool walljump_ok = impl_->physics_autoplay_walljumped_ &&
                std::fabs(impl_->physics_autoplay_walljump_speed_xy_ -
                          impl_->movement_config.wall_jump_speed_x) <= 1.0f &&
                std::fabs(impl_->physics_autoplay_walljump_speed_y_ -
                          impl_->movement_config.wall_jump_speed_y) <= 1.0f &&
                impl_->physics_autoplay_walljump_outward_speed_ >=
                    impl_->movement_config.wall_jump_speed_x - 1.0f;
            const bool mantle_ok = impl_->physics_autoplay_mantled_;
            const bool corner_ok = impl_->physics_autoplay_corner_seen_ &&
                impl_->physics_autoplay_corner_arrived_ &&
                impl_->physics_autoplay_corner_events_ == 1 &&
                impl_->physics_autoplay_corner_stable_ticks_ >= 10 &&
                impl_->physics_autoplay_corner_max_step_ <= 6.0f &&
                p.movement_state == PlayerMovementState::Climbing;
            const float jump_rise = impl_->physics_autoplay_jump_peak_y_ -
                                    impl_->physics_autoplay_jump_start_y_;
            const float air_steer_delta =
                impl_->physics_autoplay_air_steer_end_axis_speed_ -
                impl_->physics_autoplay_air_steer_start_axis_speed_;
            const float air_release_speed_drop =
                impl_->physics_autoplay_air_release_start_speed_ -
                impl_->physics_autoplay_air_release_min_speed_;
            const bool air_control_ok =
                !impl_->physics_autoplay_air_window_grounded_ &&
                air_steer_delta >= 8.0f && air_release_speed_drop >= 4.0f;
            const bool movement_ok = impl_->physics_autoplay_run_peak_speed_ >= 60.0f &&
                impl_->physics_autoplay_brake_min_speed_ <= 1.0f &&
                impl_->physics_autoplay_reverse_speed_ >= 50.0f &&
                jump_rise >= 15.0f && impl_->physics_autoplay_dash_distance_ >= 20.0f;
            const float expected_air_dash_vy =
                impl_->movement_config.dash_speed * 0.4f / std::sqrt(1.16f);
            const bool dash_launch_ok = impl_->physics_autoplay_dash_launch_recorded_ &&
                impl_->physics_autoplay_dash_airborne_ &&
                std::fabs(impl_->physics_autoplay_dash_launch_vy_ -
                          expected_air_dash_vy) <= 1.0f &&
                std::fabs(impl_->physics_autoplay_dash_launch_speed_ -
                          impl_->movement_config.dash_speed) <= 1.0f;
            const bool selected_exit_ok = kDebugPhysicsAutoplayCorner
                ? corner_ok
                : (kDebugPhysicsAutoplayMantle ? mantle_ok : walljump_ok);
            const bool passed = movement_ok && dash_launch_ok && air_control_ok &&
                                impl_->physics_autoplay_target_found_ &&
                                approach_ok && grounded_climb_entry_ok &&
                                climb_first_step_ok && climb_ok &&
                                selected_exit_ok && clearance_ok;
            debugf("[phys-test] result=%s profile=%s movement=%u air_control=%u "
                   "air_steer_delta=%.1f air_release_drop=%.1f air_grounded=%u "
                   "run_peak=%.1f brake_min=%.1f reverse_speed=%.1f "
                   "jump_rise=%.1f dash_distance=%.1f dash_launch=%u "
                   "dash_airborne=%u dash_launch_y=%.1f dash_launch_speed=%.1f "
                   "target=%u approach=%u approach_ticks=%u start_gap=%.1f entry_gap=%.1f "
                   "climb=%u climb_ticks=%u climb_entry_grounded=%u "
                   "climb_first_step_dy=%.2f climb_first_step_snap=%.3f "
                   "climb_first_step_ok=%u "
                   "walljump=%u walljump_ok=%u walljump_xy=%.1f walljump_y=%.1f "
                   "walljump_out=%.1f mantle=%u mantle_ok=%u "
                   "corner_seen=%u corner_arrived=%u corner_events=%u "
                   "corner_stable_ticks=%u corner_max_step=%.2f "
                   "clearance_samples=%u clearance_violations=%u "
                   "model_gap=%.2f required_gap=%.2f end=(%.1f,%.1f,%.1f) v_y=%.1f\n",
                   passed ? "PASS" : "FAIL",
                   kDebugPhysicsAutoplayCorner ? "corner" :
                       (kDebugPhysicsAutoplayMantle ? "mantle" : "walljump"),
                   movement_ok ? 1u : 0u,
                   air_control_ok ? 1u : 0u,
                   static_cast<double>(air_steer_delta),
                   static_cast<double>(air_release_speed_drop),
                   impl_->physics_autoplay_air_window_grounded_ ? 1u : 0u,
                   static_cast<double>(impl_->physics_autoplay_run_peak_speed_),
                   static_cast<double>(impl_->physics_autoplay_brake_min_speed_),
                   static_cast<double>(impl_->physics_autoplay_reverse_speed_),
                   static_cast<double>(jump_rise),
                   static_cast<double>(impl_->physics_autoplay_dash_distance_),
                   dash_launch_ok ? 1u : 0u,
                   impl_->physics_autoplay_dash_airborne_ ? 1u : 0u,
                   static_cast<double>(impl_->physics_autoplay_dash_launch_vy_),
                   static_cast<double>(impl_->physics_autoplay_dash_launch_speed_),
                   impl_->physics_autoplay_target_found_ ? 1u : 0u,
                   approach_ok ? 1u : 0u,
                   static_cast<unsigned int>(impl_->physics_autoplay_approach_ticks_),
                   static_cast<double>(impl_->physics_autoplay_target_start_gap_),
                   static_cast<double>(impl_->physics_autoplay_entry_gap_),
                   impl_->physics_autoplay_climb_started_ ? 1u : 0u,
                   static_cast<unsigned int>(impl_->physics_autoplay_climb_ticks_),
                   impl_->physics_autoplay_climb_entry_grounded_ ? 1u : 0u,
                   static_cast<double>(impl_->physics_autoplay_climb_first_step_dy_),
                   static_cast<double>(impl_->physics_autoplay_climb_first_step_snap_cooldown_),
                   climb_first_step_ok ? 1u : 0u,
                   impl_->physics_autoplay_walljumped_ ? 1u : 0u,
                   walljump_ok ? 1u : 0u,
                   static_cast<double>(impl_->physics_autoplay_walljump_speed_xy_),
                   static_cast<double>(impl_->physics_autoplay_walljump_speed_y_),
                   static_cast<double>(impl_->physics_autoplay_walljump_outward_speed_),
                   impl_->physics_autoplay_mantled_ ? 1u : 0u,
                   mantle_ok ? 1u : 0u,
                   impl_->physics_autoplay_corner_seen_ ? 1u : 0u,
                   impl_->physics_autoplay_corner_arrived_ ? 1u : 0u,
                   static_cast<unsigned int>(impl_->physics_autoplay_corner_events_),
                   static_cast<unsigned int>(impl_->physics_autoplay_corner_stable_ticks_),
                   static_cast<double>(impl_->physics_autoplay_corner_max_step_),
                   static_cast<unsigned int>(impl_->physics_autoplay_clearance_samples_),
                   static_cast<unsigned int>(impl_->physics_autoplay_clearance_violations_),
                   static_cast<double>(impl_->physics_autoplay_min_model_gap_),
                   static_cast<double>(required_gap),
                   static_cast<double>(p.position.x),
                   static_cast<double>(p.position.y),
                   static_cast<double>(p.position.z),
                   static_cast<double>(p.velocity.y));
        }

        if (kDebugInputRecording) {
            const InputSystem::RawSnapshot& raw = impl_->input_system.LastRaw();
            const PlayerState& p = impl_->player;
            const uint16_t raw_buttons =
                (raw.z ? 1u << 0 : 0u) | (raw.l ? 1u << 1 : 0u) |
                (raw.r ? 1u << 2 : 0u) | (raw.jump_down ? 1u << 3 : 0u) |
                (raw.dash_down ? 1u << 4 : 0u) | (raw.climb_down ? 1u << 5 : 0u) |
                (raw.pause_down ? 1u << 6 : 0u);
            const int stick_x_bucket = InputTraceStickBucket(raw.stick_x);
            const int stick_y_bucket = InputTraceStickBucket(raw.stick_y);
            const int analog_l_bucket = InputTraceStickBucket(raw.analog_l);
            const int analog_r_bucket = InputTraceStickBucket(raw.analog_r);
            const Vec3& ground_normal = p.contact.ground_normal;
            const int ground_nx_bucket = InputTraceNormalBucket(ground_normal.x);
            const int ground_ny_bucket = InputTraceNormalBucket(ground_normal.y);
            const int ground_nz_bucket = InputTraceNormalBucket(ground_normal.z);
            const Vec3& climb_normal = p.climb.wall_normal;
            const int climb_nx_bucket = InputTraceNormalBucket(climb_normal.x);
            const int climb_ny_bucket = InputTraceNormalBucket(climb_normal.y);
            const int climb_nz_bucket = InputTraceNormalBucket(climb_normal.z);
            const bool snap_active = p.contact.ground_snap_cooldown_remaining > 0.0f;
            const uint8_t state = static_cast<uint8_t>(p.movement_state);
            const bool input_changed = !impl_->input_record_has_previous_ ||
                stick_x_bucket != impl_->input_record_last_stick_x_bucket_ ||
                stick_y_bucket != impl_->input_record_last_stick_y_bucket_ ||
                analog_l_bucket != impl_->input_record_last_analog_l_bucket_ ||
                analog_r_bucket != impl_->input_record_last_analog_r_bucket_ ||
                raw_buttons != impl_->input_record_last_buttons_;
            const bool state_changed = !impl_->input_record_has_previous_ ||
                state != impl_->input_record_last_state_;
            const bool contact_changed = !impl_->input_record_has_previous_ ||
                p.grounded != impl_->input_record_last_grounded_ ||
                p.contact.was_grounded != impl_->input_record_last_was_grounded_ ||
                p.contact.ground_surface_id != impl_->input_record_last_ground_surface_id_ ||
                (p.grounded &&
                 (ground_nx_bucket != impl_->input_record_last_ground_nx_bucket_ ||
                  ground_ny_bucket != impl_->input_record_last_ground_ny_bucket_ ||
                  ground_nz_bucket != impl_->input_record_last_ground_nz_bucket_)) ||
                p.wall_contact != impl_->input_record_last_wall_ ||
                p.ceiling_contact != impl_->input_record_last_ceiling_ ||
                p.climb.grip_face_id != impl_->input_record_last_grip_face_id_ ||
                (state == static_cast<uint8_t>(PlayerMovementState::Climbing) &&
                 (climb_nx_bucket != impl_->input_record_last_climb_nx_bucket_ ||
                  climb_ny_bucket != impl_->input_record_last_climb_ny_bucket_ ||
                  climb_nz_bucket != impl_->input_record_last_climb_nz_bucket_)) ||
                snap_active != impl_->input_record_last_snap_active_;
            const uint32_t seq = impl_->input_record_tick_++;
            const bool timing_anomaly = tick == 0 &&
                (delta_seconds >= kInputTraceSlowFrameSeconds ||
                 impl_->fixed_step.PendingTicks() > 0 ||
                 impl_->fixed_step.last_discarded_seconds > 0.0f);
            if (ShouldEmitInputTrace(seq, input_changed, state_changed,
                                     contact_changed, timing_anomaly)) {
                debugf("[input-trace] frame=%u sub=%d/%d seq=%u "
                       "sched=(frame_ms=%.1f,steps=%d,pending=%d,dropped_ms=%.1f) "
                       "pad=(style%u,current%04x,held%04x) "
                       "stick=(%d,%d) move=(%.2f,%.2f) "
                       "cam=(%.2f,%.2f) cf=(%.2f,%.2f) face=(%.2f,%.2f) target=(%.2f,%.2f) "
                       "raw=(z%u,l%u,r%u,al%d,ar%d,j%u,d%u,c%u,p%u) act=(j%u,jh%u,d%u,c%u) "
                       "edge=(j%u,d%u) used=(j%u,d%u) "
                       "pos=(%.1f,%.1f,%.1f) v=(%.1f,%.1f,%.1f) "
                       "g=%u wall=%u ceil=%u st=%u "
                       "support=(was%u,surface%d,n=(%.2f,%.2f,%.2f),snap=%.2f) "
                       "walln=(%.2f,%.2f,%.2f) climbn=(%.2f,%.2f,%.2f,face%d) "
                       "timers=(d%.2f,c%.2f,j%.2f,a%u)\n",
                       static_cast<unsigned int>(input_record_frame), tick + 1, n_ticks,
                       static_cast<unsigned int>(seq),
                       static_cast<double>(delta_seconds * 1000.0f), n_ticks,
                       impl_->fixed_step.PendingTicks(),
                       static_cast<double>(impl_->fixed_step.last_discarded_seconds * 1000.0f),
                       static_cast<unsigned int>(raw.style),
                       static_cast<unsigned int>(raw.buttons_current),
                       static_cast<unsigned int>(raw.buttons_held),
                       static_cast<int>(raw.stick_x), static_cast<int>(raw.stick_y),
                       static_cast<double>(tick_input.move.x),
                       static_cast<double>(tick_input.move.y),
                       static_cast<double>(camera_input.orbit),
                       static_cast<double>(camera_input.zoom),
                       static_cast<double>(camera_forward.x),
                       static_cast<double>(camera_forward.z),
                       static_cast<double>(p.facing.x),
                       static_cast<double>(p.facing.z),
                       static_cast<double>(p.target_facing.x),
                       static_cast<double>(p.target_facing.z),
                       static_cast<unsigned int>(raw.z),
                       static_cast<unsigned int>(raw.l),
                       static_cast<unsigned int>(raw.r),
                       static_cast<int>(raw.analog_l),
                       static_cast<int>(raw.analog_r),
                       raw.jump_down ? 1u : 0u,
                       raw.dash_down ? 1u : 0u,
                       raw.climb_down ? 1u : 0u,
                       raw.pause_down ? 1u : 0u,
                       tick_input.jump_pressed ? 1u : 0u,
                       tick_input.jump_held ? 1u : 0u,
                       tick_input.dash_pressed ? 1u : 0u,
                       tick_input.climb_held ? 1u : 0u,
                       tick_input.JumpPressAvailable() ? 1u : 0u,
                       tick_input.DashPressAvailable() ? 1u : 0u,
                       tick_input.jump_press_consumed ? 1u : 0u,
                       tick_input.dash_press_consumed ? 1u : 0u,
                       static_cast<double>(p.position.x),
                       static_cast<double>(p.position.y),
                       static_cast<double>(p.position.z),
                       static_cast<double>(p.velocity.x),
                       static_cast<double>(p.velocity.y),
                       static_cast<double>(p.velocity.z),
                       p.grounded ? 1u : 0u, p.wall_contact ? 1u : 0u,
                       p.ceiling_contact ? 1u : 0u,
                       static_cast<unsigned int>(p.movement_state),
                       p.contact.was_grounded ? 1u : 0u,
                       p.contact.ground_surface_id,
                       static_cast<double>(ground_normal.x),
                       static_cast<double>(ground_normal.y),
                       static_cast<double>(ground_normal.z),
                       static_cast<double>(p.contact.ground_snap_cooldown_remaining),
                       static_cast<double>(p.wall_normal.x),
                       static_cast<double>(p.wall_normal.y),
                       static_cast<double>(p.wall_normal.z),
                       static_cast<double>(climb_normal.x),
                       static_cast<double>(climb_normal.y),
                       static_cast<double>(climb_normal.z),
                       static_cast<int>(p.climb.grip_face_id),
                       static_cast<double>(p.dash_time_remaining),
                       static_cast<double>(p.coyote_time_remaining),
                       static_cast<double>(p.hold_jump_time_remaining),
                       p.air_dash_available ? 1u : 0u);
            }
            impl_->input_record_last_stick_x_bucket_ = stick_x_bucket;
            impl_->input_record_last_stick_y_bucket_ = stick_y_bucket;
            impl_->input_record_last_analog_l_bucket_ = analog_l_bucket;
            impl_->input_record_last_analog_r_bucket_ = analog_r_bucket;
            impl_->input_record_last_ground_nx_bucket_ = ground_nx_bucket;
            impl_->input_record_last_ground_ny_bucket_ = ground_ny_bucket;
            impl_->input_record_last_ground_nz_bucket_ = ground_nz_bucket;
            impl_->input_record_last_climb_nx_bucket_ = climb_nx_bucket;
            impl_->input_record_last_climb_ny_bucket_ = climb_ny_bucket;
            impl_->input_record_last_climb_nz_bucket_ = climb_nz_bucket;
            impl_->input_record_last_ground_surface_id_ = p.contact.ground_surface_id;
            impl_->input_record_last_grip_face_id_ = p.climb.grip_face_id;
            impl_->input_record_last_buttons_ = raw_buttons;
            impl_->input_record_last_state_ = state;
            impl_->input_record_last_grounded_ = p.grounded;
            impl_->input_record_last_was_grounded_ = p.contact.was_grounded;
            impl_->input_record_last_wall_ = p.wall_contact;
            impl_->input_record_last_ceiling_ = p.ceiling_contact;
            impl_->input_record_last_snap_active_ = snap_active;
            impl_->input_record_has_previous_ = true;
        }

        if (kDebugPhysicsAutoplay && physics_tick % 6 == 0) {
            const PlayerState& p = impl_->player;
            debugf("[phys] t=%u phase=%s in=(%.2f,%.2f) j=%u jh=%u d=%u c=%u pos=(%.1f,%.1f,%.1f) v=(%.1f,%.1f,%.1f) g=%u gn=(%.2f,%.2f,%.2f) wall=%u ceil=%u wn=(%.2f,%.2f,%.2f) st=%u coy=%.2f snap=%.2f\n",
                   static_cast<unsigned int>(physics_tick), physics_phase,
                   static_cast<double>(tick_input.move.x),
                   static_cast<double>(tick_input.move.y),
                   tick_input.jump_pressed ? 1u : 0u,
                   tick_input.jump_held ? 1u : 0u,
                   tick_input.dash_pressed ? 1u : 0u,
                   tick_input.climb_held ? 1u : 0u,
                   static_cast<double>(p.position.x),
                   static_cast<double>(p.position.y),
                   static_cast<double>(p.position.z),
                   static_cast<double>(p.velocity.x),
                   static_cast<double>(p.velocity.y),
                   static_cast<double>(p.velocity.z),
                   p.grounded ? 1u : 0u,
                   static_cast<double>(p.contact.ground_normal.x),
                   static_cast<double>(p.contact.ground_normal.y),
                   static_cast<double>(p.contact.ground_normal.z),
                   p.wall_contact ? 1u : 0u,
                   p.ceiling_contact ? 1u : 0u,
                   static_cast<double>(p.wall_normal.x),
                   static_cast<double>(p.wall_normal.y),
                   static_cast<double>(p.wall_normal.z),
                   static_cast<unsigned int>(p.movement_state),
                   static_cast<double>(p.coyote_time_remaining),
                   static_cast<double>(p.contact.ground_snap_cooldown_remaining));
        }

        // Spring board (OG Spring.cs Pickup/pickupRadius + Player.Spring):
        // when the player overlaps an active, non-cooling-down board, launch
        // her (OG parity v.y=160). The board's own 1.0 s cooldown prevents a
        // standing player being relaunched every tick (OG Spring.cs tCooldown).
        if (SpringActor* sp = impl_->actor_world.Get<SpringActor>()) {
            if (sp->active && sp->cooldown_remaining <= 0.0f) {
                const float dx = impl_->player.position.x - sp->position.x;
                const float dy = impl_->player.position.y - sp->position.y;
                const float dz = impl_->player.position.z - sp->position.z;
                if ((dx * dx + dy * dy + dz * dz) <= sp->pickup_radius * sp->pickup_radius) {
                    impl_->player_controller.Spring(impl_->player, sp->position);
                    sp->cooldown_remaining = SpringActor::kCooldown;
                    // UNGATED (Inc 3): a spring launch must be attributable on
                    // device — fires once per overlap (board cooldown gates re-fire).
                    debugf("[spring] launch v.y=%.1f pos=(%.1f,%.1f,%.1f)\n",
                           static_cast<double>(impl_->player.velocity.y),
                           static_cast<double>(impl_->player.position.x),
                           static_cast<double>(impl_->player.position.y),
                           static_cast<double>(impl_->player.position.z));
                }
            }
        }

        // Collapse telemetry onto the controller-owned ground state. The old
        // MotorResult is a 4-field adapter now (position/velocity/ice/oneway
        // fields are gone with the motor).
        motor_result.grounded = impl_->player.grounded;
        motor_result.ground_normal = impl_->player.contact.ground_normal;
        motor_result.wall_contact = impl_->player.wall_contact;
        // Death (F2, restored parity with the old motor's FaceIsDeath): the
        // mover flags a repaired contact face carrying MAT_DEATH; the kill
        // plane is the other trigger. Same threshold + operator as
        // RespawnSystem's own gate (S2: one contract, not two).
        motor_result.death = impl_->player.death_contact ||
                             impl_->player.position.y <= impl_->room.kill_plane_y;
        (void)was_grounded_pre_tick;  // consumed by the telemetry call post-loop

        if (!was_climbing_pre &&
            impl_->player.movement_state == PlayerMovementState::Climbing) {
            impl_->telemetry.RecordClimbStart();
            // UNGATED: a bogus auto-grab must name its raw button bits at the
            // exact grab frame — counter deltas alone cannot attribute it.
            const InputSystem::RawSnapshot& raw = impl_->input_system.LastRaw();
            debugf("[climb] START pad=(style%u,current%04x,held%04x) "
                   "z=%u l=%u r=%u al=%d ar=%d source=%s scripted_c=%u "
                   "wn=(%.2f,%.2f) pos=(%.1f,%.1f,%.1f)\n",
                   static_cast<unsigned int>(raw.style),
                   static_cast<unsigned int>(raw.buttons_current),
                   static_cast<unsigned int>(raw.buttons_held),
                   static_cast<unsigned int>(raw.z),
                   static_cast<unsigned int>(raw.l),
                   static_cast<unsigned int>(raw.r),
                   static_cast<int>(raw.analog_l),
                   static_cast<int>(raw.analog_r),
                   kDebugPhysicsAutoplay ? "autoplay" : "live",
                   tick_input.climb_held ? 1u : 0u,
                   static_cast<double>(impl_->player.climb.wall_normal.x),
                   static_cast<double>(impl_->player.climb.wall_normal.z),
                   static_cast<double>(impl_->player.position.x),
                   static_cast<double>(impl_->player.position.y),
                   static_cast<double>(impl_->player.position.z));
        }
        if (player_step.mantle_triggered) {
            impl_->telemetry.RecordClimbMantle();
            // UNGATED (Inc 5): a mantle hop must be attributable on device —
            // the counter hook alone cannot time it. Fires once per hop.
            debugf("[climb] MANTLE hop=%.1f pos=(%.1f,%.1f,%.1f)\n",
                   static_cast<double>(impl_->player.velocity.y),
                   static_cast<double>(impl_->player.position.x),
                   static_cast<double>(impl_->player.position.y),
                   static_cast<double>(impl_->player.position.z));
        }
        if (player_step.corner_triggered) {
            // UNGATED (Inc 5): fires once per corner adopt — validates the
            // Inc 3 corner system on device (≥1 CORNER line per intentional
            // corner traversal, no START-spam at a single position).
            debugf("[climb] CORNER wn=(%.2f,%.2f) pos=(%.1f,%.1f,%.1f)\n",
                   static_cast<double>(impl_->player.climb.wall_normal.x),
                   static_cast<double>(impl_->player.climb.wall_normal.z),
                   static_cast<double>(impl_->player.position.x),
                   static_cast<double>(impl_->player.position.y),
                   static_cast<double>(impl_->player.position.z));
        }
        // (was_grounded snapshot is owned by MovePhase's LateContactPhase.)

        if (impl_->respawn_system.Step(impl_->player, impl_->checkpoint, impl_->room, impl_->player_controller.Mover(), motor_result.death)) {
            did_respawn = true;
            impl_->player.prev_position = impl_->player.position;
            // Multi-room death-respawn: ensure the checkpoint's room is active
            // (per-chunk respawn routing). The checkpoint Vec3 is the spawn
            // point; resolve which room it lives in and load it.
            if (impl_->use_map_pack_) {
                const char* cp_room = impl_->map_runtime_.ResolveCellByPosition(impl_->checkpoint);
                if (cp_room && cp_room[0] != '\0') {
                    if (impl_->map_runtime_.CommitActive(cp_room)) {
                        // Mirror active room into the legacy `room` reference so
                        // the next tick's collision query sees the chunk.
                        impl_->room.coll_mesh = impl_->map_runtime_.Active()->room.coll_mesh;
                    } else {
                        debugf("[respawn] CommitActive(%s) FAILED — using current room\n", cp_room);
                    }
                }
            }
        }

    }

    impl_->render_alpha = impl_->fixed_step.Alpha();

    impl_->telemetry.BeginFrame();
    impl_->telemetry.RecordPlayerState(impl_->player);
    // Inc 7: surface/carry sample counters fed from mover-owned player state + room.
    impl_->telemetry.RecordSurfaceSample(
        static_cast<uint32_t>(impl_->room.moving_surface_count),
        motor_result.grounded,
        motor_result.ground_normal.y,
        motor_result.grounded && was_grounded_pre_last_tick &&
            impl_->player.contact.ground_snap_cooldown_remaining <= 0.0f);
    // Inc 3: Record active room id, floor normal, and render origin for
    // chunk-traversal diagnostics.
    {
        const ActiveRoomView* active = impl_->map_runtime_.Active();
        impl_->telemetry.RecordActiveRoom(
            impl_->map_runtime_.ActiveRoomId(),
            motor_result.grounded ? motor_result.ground_normal.y : 0.0f,
            active ? active->render_origin : Vec3{0.0f, 0.0f, 0.0f});
    }
    // Inc 8: Record per-frame collision query cost from the global collision.
    impl_->telemetry.RecordCollisionQueryCounters(
        impl_->map_runtime_.GlobalCollision().Counters());
    if (did_respawn) {
        // Inc 1: the device owns the dash press buffer — clear the buffered
        // edge on respawn (the old ResetState cleared the controller's
        // dash_buffer_remaining) so a pre-death press cannot fire after respawn.
        impl_->input_system.ConsumeDashPress();
        input.dash_pressed = false;
        impl_->telemetry.RecordRespawn();
        impl_->camera_controller.Reset(impl_->camera, impl_->player.position);
    }

    // Camera reads the post-move (and post-respawn) player state.
    const float horiz_speed = std::sqrt(
        (impl_->player.velocity.x * impl_->player.velocity.x) +
        (impl_->player.velocity.z * impl_->player.velocity.z));
    impl_->camera_controller.Step(
        impl_->camera,
        impl_->player.position,
        impl_->player.movement_state == PlayerMovementState::Climbing,
        impl_->player.grounded,
        horiz_speed,
        camera_input,
        delta_seconds,
        &impl_->room
    );

    // Actors run after the player + camera so they can read the resolved
    // player state for pickup checks and other gameplay reactions.
    if (impl_->baked_level_loaded_) {
        if (impl_->room.has_cassette && !impl_->cassette_reload_active_) {
            if (impl_->cassette_actor.Step(delta_seconds, impl_->player.position)) {
                impl_->cassette_reload_active_ = true;
                impl_->cassette_reload_timer_ = 0.0f;
            }
        }
        impl_->actor_world.Update(delta_seconds);
    } else {
        TryCollect(impl_->collectible, impl_->player.position);
    }

    if (impl_->cassette_reload_active_) {
        impl_->cassette_reload_timer_ += delta_seconds;
        if (impl_->cassette_reload_timer_ >= kFadeReloadSeconds) {
            // If the cassette has a target level, load it via SetLevel (B-side Push/Pop).
            if (impl_->room.has_cassette && impl_->room.cassette_target[0] != '\0') {
                // Store the target and reload via SetLevel path.
                impl_->lvl_path = impl_->room.cassette_target;
                impl_->level_name = "cassette-target";  // placeholder name
                impl_->ReloadBakedLevel();
            } else {
                impl_->ReloadBakedLevel();
            }
            return;
        }
    }

    {
        const Vec3 interp = impl_->player.InterpolatedPosition(impl_->render_alpha);
        // Camera-at-origin view (see Render): the fallback cube must also be
        // offset by -camera, otherwise it is drawn at world coordinates and
        // disappears off-screen when the camera is far from the origin.
        Vec3 camera_relative = interp;
        camera_relative.x -= impl_->camera.position.x;
        camera_relative.y -= impl_->camera.position.y;
        camera_relative.z -= impl_->camera.position.z;
        SetTransform(impl_->player_render, camera_relative, {5.0f, 10.0f, 5.0f});
    }
    if (!impl_->baked_level_loaded_) {
        SetTransform(
            impl_->collectible_render,
            impl_->collectible.position,
            impl_->collectible.collected ? Vec3{0.0f, 0.0f, 0.0f} : Vec3{7.5f, 7.5f, 7.5f}
        );
    }

    DebugCounters counters;
    counters.active_scene_id = 0;
    counters.actor_count =
        1 + impl_->room_geometry_count + impl_->actor_world.Count() +
        ((impl_->room.has_cassette && !impl_->cassette_actor.collected) ? 1 : 0);
    impl_->debug_hud.Update(counters);

    // Print telemetry every 60 frames (~1 second) to avoid serial spam.
    // Inc 1 / D5: gated behind kVerboseFrameLogging — the burst is diagnostics
    // and can spike one frame on the USB serial path. The 60-frame profiler
    // report is NOT gated (it is the on-device measurement).
    if (kVerboseFrameLogging && impl_->telemetry.frame_index % 60 == 0) {
        impl_->telemetry.PrintLine();
    }
    // Raw input echo — UNGATED (same cadence as the profiler report): a stuck
    // shoulder bit shows up here even with verbose logging off. Constant
    // grabbing with no button held is visible as z/l/r=1 at rest.
    if (impl_->telemetry.frame_index % 60 == 0) {
        const InputSystem::RawSnapshot& raw = impl_->input_system.LastRaw();
        debugf("[input] z=%u l=%u r=%u al=%d ar=%d | j=%u d=%u c=%u p=%u\n",
               static_cast<unsigned int>(raw.z),
               static_cast<unsigned int>(raw.l),
               static_cast<unsigned int>(raw.r),
               static_cast<int>(raw.analog_l),
               static_cast<int>(raw.analog_r),
               raw.jump_down ? 1u : 0u,
               raw.dash_down ? 1u : 0u,
               raw.climb_down ? 1u : 0u,
               raw.pause_down ? 1u : 0u);
    }
    // og-mover-overhaul Inc 2: ungated `[move]` line at 30-frame cadence so
    // clip forensics are grep-able from the first device capture (a position
    // inside a wall is unambiguous). State string derived from the FSM enum.
    if (impl_->telemetry.frame_index % 30 == 0) {
        const char* st = "N";
        switch (impl_->player.movement_state) {
            case PlayerMovementState::Dashing: st = "D"; break;
            case PlayerMovementState::Climbing: st = "C"; break;
            case PlayerMovementState::Skidding: st = "K"; break;  // review C9
            default: st = "N"; break;
        }
        debugf("[move] pos=(%.1f,%.1f,%.1f) v=(%.1f,%.1f,%.1f) g=%u wall=%u ceil=%u st=%s\n",
               static_cast<double>(impl_->player.position.x),
               static_cast<double>(impl_->player.position.y),
               static_cast<double>(impl_->player.position.z),
               static_cast<double>(impl_->player.velocity.x),
               static_cast<double>(impl_->player.velocity.y),
               static_cast<double>(impl_->player.velocity.z),
               impl_->player.grounded ? 1u : 0u,
               impl_->player.wall_contact ? 1u : 0u,
               impl_->player.ceiling_contact ? 1u : 0u,
               st);
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

    // Dynamic FOV: base 45 degrees scaled by fov_multiplier (1.0 at rest,
    // up to 1.2 at high horizontal speed).
    const float fov_deg = 45.0f * impl_->camera.fov_multiplier;
    const float near_plane = impl_->camera.render_near_plane;

    // Single near-pass camera. The far plane is 800u for the 9-cell ring; when
    // the distant tier is enabled (render-distance Inc 5), widen it to cover
    // the whole-map horizon (.dlod cells up to ~2400u).
    const CameraDesc cams = MakeNearCamera(
        fov_deg, near_plane, kEnableDistantTier ? 2000.0f : 800.0f,
        impl_->camera.position, impl_->camera.target,
        Vec3{0.0f, 1.0f, 0.0f});

    t3d_viewport_set_projection(&impl_->viewport, T3D_DEG_TO_RAD(fov_deg), near_plane,
                                cams.far);
    // CRITICAL camera-at-origin coupling: model matrices are camera-relative
    // (LvlRoomRenderer::SetCameraPosition translates by render_origin -
    // camera_position). For the near-pass view to agree with those model
    // matrices, the view must ALSO be camera-at-origin — origin at zero and
    // target offset by -camera — otherwise geometry is double-offset by
    // `-camera` and the world shifts/popps. This coupling is t3d-only and is
    // validated on device (Ares) with a walk-across-a-seam check.
    const T3DVec3 view_origin = {{0.0f, 0.0f, 0.0f}};
    const T3DVec3 view_target = {{
        camera_target.x - camera_position.x,
        camera_target.y - camera_position.y,
        camera_target.z - camera_position.z
    }};
    t3d_viewport_look_at(&impl_->viewport, &view_origin, &view_target, &camera_up);

    // Rebase loaded room geometry so it renders camera-relative. Both the
    // map-pack ring (via the orchestrator) and the legacy single-room renderer
    // are LvlRoomRenderers.
    impl_->open_world_.SetCameraPosition(impl_->camera.position);
    impl_->room_renderer.SetCameraPosition(impl_->camera.position);

    rdpq_attach(display_get(), display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(&impl_->viewport);
    t3d_screen_clear_color(RGBA32(88, 163, 221, 0xFF));  // normal blue sky
    t3d_screen_clear_depth();

    t3d_light_set_ambient(impl_->ambient_light);
    t3d_light_set_directional(0, impl_->directional_light, &impl_->light_direction);
    t3d_light_set_count(1);
    if (impl_->room_fixture_visible_) {
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        rdpq_set_prim_color(RGBA32(255, 100, 100, 255));  // red — fixture diagnostic
        impl_->room_fixture_model.Draw();
    } else if (impl_->baked_level_loaded_) {
        // Baked level: draw room geometry from .lvl via LvlRoomRenderer.
        // Uses PRIM*SHADE combiner with per-material primColor set per batch.
        t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED | T3D_FLAG_DEPTH));
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
        // In map-pack mode, drive the single near-pass orchestrator.
        // Otherwise use the legacy single-room renderer.
        if (impl_->use_map_pack_) {
            // The viewport is already attached above with the gameplay
            // projection. SetViewport is kept for host-test compatibility.
            impl_->open_world_.SetViewport(&impl_->viewport);
            impl_->open_world_.UpdateCamera(cams, impl_->last_delta);
            impl_->open_world_.Render(cams);
        } else {
            impl_->room_renderer.Draw();
        }
        if (impl_->room.has_cassette && !impl_->cassette_actor.collected && impl_->cassette_model.IsLoaded()) {
            constexpr float kCassetteScale = 0.18f;
            const Vec3 cam = impl_->camera.position;
            Vec3 pos = impl_->cassette_actor.position;
            pos.x -= cam.x;  // camera-at-origin view: world coords must be offset by -camera
            pos.y -= cam.y;
            pos.z -= cam.z;
            impl_->cassette_model.UpdateMatrix(pos, kCassetteScale, impl_->cassette_actor.SpinYawRadians());
            SetActorShadedState();
            impl_->cassette_model.Draw();
        }
        constexpr float kStrawberryScale = 0.05f;
        if (StrawberryActor* sa = impl_->actor_world.Get<StrawberryActor>()) {
            const Vec3 cam = impl_->camera.position;
            Vec3 pos = sa->position;
            pos.x -= cam.x;  // camera-at-origin view: world coords must be offset by -camera
            pos.y -= cam.y;
            pos.z -= cam.z;
            impl_->strawberry_model.UpdateMatrix(pos, kStrawberryScale, 0.0f);
            SetActorShadedState();
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
            constexpr float kStrawberryScale = 0.05f;
            impl_->strawberry_model.UpdateMatrix(impl_->collectible.position, kStrawberryScale, 0.0f);
            impl_->strawberry_model.Draw();
        }
    }

    if (impl_->madeline_model.IsLoaded()) {
        constexpr float kMadelineScale = 5.0f;  // coin is tiny; scale up
        constexpr float kMadelineYOffset = 0.0f;
        const Vec3 facing = impl_->player.facing;
        const float yaw = std::atan2(facing.x, facing.z);
        Vec3 draw_pos = impl_->player.InterpolatedPosition(impl_->render_alpha);
        draw_pos.y += kMadelineYOffset;
        draw_pos.x -= impl_->camera.position.x;
        draw_pos.y -= impl_->camera.position.y;
        draw_pos.z -= impl_->camera.position.z;
        impl_->madeline_model.UpdateMatrix(draw_pos, kMadelineScale, yaw);
        SetActorShadedState();
        impl_->madeline_model.Draw();
    } else {
        DrawCube(impl_->cube_vertices, impl_->player_render.matrix_fp);
    }

    // Switch to 2D for overlays
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    if (impl_->cassette_reload_active_) {
        const float fade_alpha = impl_->cassette_reload_timer_ / kFadeReloadSeconds;
        const uint8_t alpha = static_cast<uint8_t>(fade_alpha >= 1.0f ? 255.0f : fade_alpha * 255.0f);
        rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
        rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
    }
    impl_->debug_hud.Render();

    // Close the renderer's per-frame profiler span (Inc 1 / instrumentation).
    // Called unconditionally (both the map-pack and legacy branches) so the
    // whole-frame average covers either path. Safe when no phases were opened.
    impl_->open_world_.EndFrame();

    rdpq_detach_show();
}

}  // namespace madeline_cube
