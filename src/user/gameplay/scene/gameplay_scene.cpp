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
#include "gameplay/player/camera_controller.hpp"
#include "gameplay/world/collectible.hpp"
#include "gameplay/debug_hud.hpp"
#include "gameplay/physics_contracts.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/player/player_motor.hpp"
#include "gameplay/input/input_system.hpp"
#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/render/open_world_renderer.hpp"
#include "gameplay/render/fog_math.hpp"
#include "gameplay/render/material_catalog.hpp"
#include "gameplay/render/model.hpp"
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

// Build the PlayerInput snapshot from the frame's InputSystem state.
// Called once per render frame and replayed across all fixed-step ticks so
// the physics sees one consistent input snapshot (§34: capture raw once).
PlayerInput ReadPlayerInput(const InputSystem& input_system) {
    PlayerInput result;
    const Vec2 move = input_system.MoveValue();
    result.move = move;
    result.jump_pressed = input_system.jump.Pressed();
    result.jump_held = input_system.jump.Down();
    result.dash_pressed = input_system.dash.Pressed();
    result.climb_held = input_system.climb.Down();
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
    T3DViewport viewport = t3d_viewport_create();
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
    PlayerMotor player_motor;
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
    // Material catalog for the textured near pass (Inc 5). Loaded from the
    // map-pack's `.manifest`; owned by this Impl.
    MaterialCatalog material_catalog_;

    // Resolve the active room for query/update/render. Routes to the MapRuntime
    // active room when a map-pack is in use, else the legacy single room.
    Room& ActiveRoom() {
        return use_map_pack_ ? map_runtime_.Active()->room : room;
    }
    const Room& ActiveRoom() const {
        return use_map_pack_ ? map_runtime_.Active()->room : room;
    }

    const char* lvl_path  = "rom:/lvl/1-1.lvl";
    const char* level_name = "1-1";

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
};

void GameplayScene::Impl::ResetPlayerToRoomStart() {
    constexpr float kSpawnSkin = 0.2f;
    const PlayerMotorConfig& motor_config = player_motor.Config();
    const float spawn_lift = motor_config.half_height + kSpawnSkin;
    const auto snap_spawn_center = [&](const Vec3& authored_point) {
        Vec3 center = authored_point;
        center.y += spawn_lift;

        const GroundHit floor = ProbeFloorDebug(
            room,
            center,
            motor_config.half_height,
            motor_config.ground_snap_distance,
            motor_config.radius);
        if (floor.hit) {
            center.y = floor.point.y + motor_config.half_height + kSpawnSkin;
        }
        return center;
    };

    const Vec3 authored_start = room.player_start;
    checkpoint = snap_spawn_center(room.checkpoint);
    player = {};
    player.position = snap_spawn_center(room.player_start);
    player.prev_position = player.position;

    MotorInput refresh_input;
    refresh_input.requested_velocity = {0.0f, 0.0f, 0.0f};
    refresh_input.wants_ground_snap = false;
    refresh_input.wants_coyote_refresh = true;
    refresh_input.wants_dash_refill = true;
    player_motor.RefreshContacts(player, room, refresh_input);
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

    // Configure the distant-pass fog (Inc 6). The horizon fades into a
    // blue-grey atmosphere to hide the distant/near transition.
    {
        FogParams fog = MakeFog(300.0f, 1200.0f, {120.0f, 150.0f, 180.0f});
        open_world_.SetFog(fog);
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

    ResetPlayerToRoomStart();

    // Load the render-only neighbor ring for the start cell.
    const V2RoomSpec* start_spec = map_runtime_.ActiveSpec();
    if (start_spec) {
        open_world_.SetCenter(map_runtime_.Spec(), *start_spec, nullptr);
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
    // Refresh the render-only neighbor ring to the new cell's neighbors.
    const V2RoomSpec* active_spec = map_runtime_.ActiveSpec();
    if (active_spec) {
        open_world_.SetCenter(map_runtime_.Spec(), *active_spec, nullptr);
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

    delete impl_;
    impl_ = nullptr;
}

void GameplayScene::Update(float delta_seconds) {
    if (impl_ == nullptr) return;

    // Sample the controller once per render frame; same snapshot replayed
    // across all fixed-step substeps (§34: capture raw input once).
    joypad_poll();
    impl_->input_system.Poll();

    // Start (N64) toggles the room fixture overlay (debug aid). The spec maps
    // Start -> Pause/menu; the fixture toggle is kept as a temporary stand-in
    // until a full pause menu exists.
    if (impl_->input_system.pause.Pressed()) {
        impl_->room_fixture_visible_ = !impl_->room_fixture_visible_;
        debugf("[fixture] room fixture %s\n", impl_->room_fixture_visible_ ? "ON" : "OFF");
    }
    const PlayerInput input = ReadPlayerInput(impl_->input_system);
    const CameraInput camera_input = ReadCameraInput(impl_->input_system);
    const Vec3 camera_forward = {
        impl_->camera.target.x - impl_->camera.position.x,
        impl_->camera.target.y - impl_->camera.position.y,
        impl_->camera.target.z - impl_->camera.position.z,
    };

    // Fixed-step physics loop.
    const int n_ticks = impl_->fixed_step.BeginFrame(delta_seconds);
    debugf("[update] frame start: coll_mesh=%p n_ticks=%d\n",
           (void*)impl_->room.coll_mesh, n_ticks);
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
    bool was_grounded_pre_motor = impl_->player.contact.was_grounded;
    bool did_respawn = false;

    for (int tick = 0; tick < n_ticks; ++tick) {
        // Motor first: resolves position/velocity and sets state.grounded
        MotorInput motor_input;
        motor_input.requested_velocity = impl_->player.velocity;
        motor_input.wants_ground_snap = impl_->player.contact.was_grounded &&
                                        impl_->player.movement_state != PlayerMovementState::Dashing;
        motor_input.wants_coyote_refresh = true;
        motor_input.wants_dash_refill = impl_->player.dash_reset_cooldown_remaining <= 0.0f;
        AdvanceMovingSurfaces(impl_->room, FixedStepAccumulator::kTickDt);
        was_grounded_pre_motor = impl_->player.contact.was_grounded;
        debugf("[tick%d] pre-step: coll_mesh=%p vel=(%.1f,%.1f,%.1f)\n",
               tick, (void*)impl_->room.coll_mesh,
               (double)impl_->player.velocity.x,
               (double)impl_->player.velocity.y,
               (double)impl_->player.velocity.z);
        motor_result = impl_->player_motor.Step(impl_->player, impl_->room, motor_input, FixedStepAccumulator::kTickDt);

        // Controller reads post-motor grounded state for FSM transitions
        PlayerController::StepContext player_step = impl_->player_controller.TimerInputPhase(
            impl_->player, input, camera_forward, FixedStepAccumulator::kTickDt);
        impl_->player_controller.StatePhase(impl_->player, input, player_step, FixedStepAccumulator::kTickDt, &impl_->room);
        impl_->player_controller.LateContactPhase(impl_->player);

        if (impl_->respawn_system.Step(impl_->player, impl_->checkpoint, impl_->room, impl_->player_motor, motor_result.death)) {
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
                        // the next motor tick's collision query sees the chunk.
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
    // Inc 7: surface/carry sample counters fed from motor result + room state.
    impl_->telemetry.RecordSurfaceSample(
        static_cast<uint32_t>(impl_->room.moving_surface_count),
        motor_result.grounded,
        motor_result.ground_normal.y,
        motor_result.grounded && was_grounded_pre_motor &&
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
    if (did_respawn) {
        impl_->telemetry.RecordRespawn();
        impl_->camera_controller.Reset(impl_->camera, impl_->player.position);
    }

    // Camera reads the post-motor (and post-respawn) player state.
    const float horiz_speed = std::sqrt(
        (impl_->player.velocity.x * impl_->player.velocity.x) +
        (impl_->player.velocity.z * impl_->player.velocity.z));
    impl_->camera_controller.Step(
        impl_->camera,
        impl_->player.position,
        impl_->player.wall_grabbing,
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

    // Dynamic FOV: base 45 degrees scaled by fov_multiplier (1.0 at rest,
    // up to 1.2 at high horizontal speed).
    const float fov_deg = 45.0f * impl_->camera.fov_multiplier;
    t3d_viewport_set_projection(&impl_->viewport, T3D_DEG_TO_RAD(fov_deg), 20.0f, 800.0f);
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
        // In map-pack mode, drive the two-pass orchestrator (arch.md §21
        // order: distant Z-off, low-priority Z-off, high-priority Z-on). Inc 2
        // keeps the legacy ring as the high-priority near pass (no regression);
        // Inc 3 swaps it for TileStreamer. Otherwise use the legacy single-room
        // renderer.
        if (impl_->use_map_pack_) {
            const PassCameras cams = BuildPassCameras(
                impl_->camera.position, impl_->camera.target,
                fov_deg, 20.0f, 800.0f,
                /*tile_size=*/impl_->map_runtime_.Spec().chunk_size *
                    impl_->map_runtime_.Spec().scale,
                /*lod_scale=*/0.25f);
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

    rdpq_detach_show();
}

}  // namespace madeline_cube
