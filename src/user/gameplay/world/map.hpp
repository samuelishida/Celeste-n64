#pragma once

#include <cstdint>
#include <cstring>

#include "gameplay/math_types.hpp"
#include "gameplay/world/level_loader.hpp"  // LevelGeometry, LoadLevel
#include "gameplay/world/mappack_loader.hpp"  // MapSpec
#include "gameplay/world/world.hpp"          // Room, ActorSpawn, AABB

namespace madeline_cube {

namespace physics { struct CollMesh; }
class LvlRoomRenderer;        // forward decl (N64-only; null on host)

// One loaded room (chunk) in the Map pool. Per the plan, LevelGeometry is
// NOT resident here — it lives in a single shared scratch buffer reused per
// load and discarded once the renderer is built. Runtime collision uses
// room.coll_mesh and rendering uses renderer.
struct LoadedRoom {
    static constexpr int kIdLen = 16;

    char id[kIdLen] = {};
    Room room;                       // existing single-room struct, reused
    LevelGeometry geometry;          // scratch: filled on load, reusable
    LvlRoomRenderer* renderer = nullptr;  // N64-only; null on host
    bool loaded = false;
    bool pinned = false;             // active or checkpoint room — never evicted
    int lru_tick = 0;                // for LRU eviction of non-pinned rooms
};

// Multi-room container: owns a MapSpec + a small pool of loaded rooms with
// active-chunk streaming and LRU eviction. Built on top of the existing
// single-Room/LvlRoomRenderer/ActorWorld/coll_mesh machinery — nothing in
// the single-room code is rewritten; it is pooled.
//
// Active-cell resolution is by MATHEMATICAL grid index
// (floor(pos.x / chunk_size), floor(pos.z / chunk_size)), NOT by world-AABB
// containment, so overlapping brush AABBs across cells don't cause flapping.
class Map {
public:
    static constexpr int kMaxLoadedRooms = 3;  // active + 1 neighbor + spare

    Map() = default;
    ~Map();

    // Initialize from a loaded map-pack spec. Does not load any rooms.
    void Init(const MapSpec& spec);

    // Release all loaded rooms (frees coll_mesh + renderer per room).
    void Reset();

    // Ensure a room and (optionally) its neighbor ring are loaded. Ring size
    // defaults to 0 (active-only) for v1; expand after measuring footprints.
    // The active room and checkpoint room (SetCheckpointRoom) are pinned and
    // never evicted. On pool exhaustion, evicts the LRU non-pinned room.
    bool EnsureLoaded(const char* room_id, int ring = 0);

    // Mark a room as the checkpoint room (pinned, never evicted). Used by
    // the respawn path so the checkpoint chunk is resident on death.
    void SetCheckpointRoom(const char* room_id);

    // Resolve which grid cell a world position falls in (math, not AABB).
    // Returns the room id string (empty if no non-empty room at that cell).
    const char* ResolveCellByPosition(const Vec3& pos) const;

    // If the player's cell differs from the active room, returns true and
    // sets *new_room_id to the resolved room id. Does NOT perform the swap —
    // the caller (GameplayScene) calls SetActive to commit it.
    bool SetActivByPosition(const Vec3& player_pos, const char** new_room_id);

    // Commit a room as active (must already be loaded). Returns the active
    // LoadedRoom or null if not loaded.
    LoadedRoom* SetActive(const char* room_id);

    // Load a room's geometry WITHOUT resetting the player (chunk transition
    // path). Loads .lvl -> renderer + .colmesh and dispatches the room's
    // entities into its own ActorWorld. Skips ResetPlayerToRoomStart and
    // CameraController::Reset (the caller preserves player pos/velocity).
    // Returns true on success. Used by transitions; the initial boot and
    // death-respawn paths use the full ReloadBakedLevel-style reset instead.
    bool LoadRoomGeometry(const char* room_id);

    // Accessors for the active room.
    LoadedRoom* Active() { return active_index_ >= 0 ? &pool_[active_index_] : nullptr; }
    const LoadedRoom* Active() const { return active_index_ >= 0 ? &pool_[active_index_] : nullptr; }
    const char* ActiveRoomId() const;
    Room& ActiveRoom();
    const Room& ActiveRoom() const;

    const MapSpec& Spec() const { return spec_; }
    bool HasMap() const { return spec_.room_count > 0; }

private:
    // Find a pool slot for a room, evicting LRU non-pinned rooms if needed.
    // Returns the slot index, or -1 if the room is already loaded.
    int FindOrEvictSlot(const char* room_id);

    // Load one room's .lvl + .colmesh into slot, building the renderer.
    bool LoadSlot(int slot, const MapRoomSpec& room_spec);

    // Free a slot's resources (renderer, coll_mesh) and mark unloaded.
    void FreeSlot(int slot);

    MapSpec spec_;
    LoadedRoom pool_[kMaxLoadedRooms];
    int active_index_ = -1;
    int checkpoint_slot_ = -1;
    int lru_counter_ = 0;
};

}  // namespace madeline_cube