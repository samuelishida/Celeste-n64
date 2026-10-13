#pragma once

#include <cstdint>
#include <cstring>

#include "gameplay/math_types.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/world/mappack_loader.hpp"  // MapSpecV2, V2RoomSpec, V2SpawnSpec
#include "gameplay/world/world.hpp"          // WorldCollision, Room, AABB

namespace madeline_cube {

// One authoritative active room view. Owns room metadata, dynamic
// colliders, room id, cell key, render origin, and actor records. It does NOT
// own a static collision mesh or visual renderer — TileStreamer owns visual
// cell residency, and all static queries use global WorldCollision.
struct ActiveRoomView {
    static constexpr int kIdLen = 16;

    char id[kIdLen] = {};
    int cell_ix = 0;
    int cell_iz = 0;
    Vec3 render_origin = {0.0f, 0.0f, 0.0f};
    AABB world_aabb;
    Room room;                       // dynamic colliders, spawns, atmosphere
    bool loaded = false;
};

// The authoritative map runtime (Inc 6). Owns ONE global WorldCollision plus
// one staged/active room state. TileStreamer owns visual cell residency.
class MapRuntime {
public:
    MapRuntime() = default;
    ~MapRuntime();

    MapRuntime(const MapRuntime&) = delete;
    MapRuntime& operator=(const MapRuntime&) = delete;

    // Load the v2 manifest + the one global CMSH. Returns false on failure.
    // On failure, no gameplay state is committed (full-world boot halts).
    bool Init(const char* mappack_path, const char* build_dir = nullptr);

    // Release all state (global mesh + active room).
    void Reset();

    bool HasMap() const { return spec_.room_count > 0; }
    bool HasGlobalCollision() const { return collision_.IsLoaded(); }

    // Resolve which grid cell a world position falls in. Returns the room id
    // string (empty if no non-empty room at that cell).
    const char* ResolveCellByPosition(const Vec3& pos) const;

    // If the player's cell differs from the active room, returns true and sets
    // *new_room_id. Does NOT perform the swap — the caller calls CommitActive.
    bool SetActiveByPosition(const Vec3& player_pos, const char** new_room_id);

    // Stage + commit a room state as active. Loads the room's .lvl into the
    // active view (dynamic colliders + spawns). On failure, the old
    // active room remains active and global collision stays valid. Returns true
    // on success.
    bool CommitActive(const char* room_id);

    // Accessors.
    const ActiveRoomView* Active() const { return active_committed_ ? &active_ : nullptr; }
    ActiveRoomView* Active() { return active_committed_ ? &active_ : nullptr; }
    const char* ActiveRoomId() const { return active_committed_ ? active_.id : ""; }
    // The active room's manifest spec (for resolving neighbor paths + render
    // origins). Returns null if no room is committed.
    const V2RoomSpec* ActiveSpec() const {
        return active_committed_ ? spec_.FindRoom(active_.id) : nullptr;
    }
    WorldCollision& GlobalCollision() { return collision_; }
    const WorldCollision& GlobalCollision() const { return collision_; }
    DynamicSolidGeometry& DynamicGeometry() { return dynamic_geometry_; }
    const DynamicSolidGeometry& DynamicGeometry() const { return dynamic_geometry_; }
    const MapSpecV2& Spec() const { return spec_; }

    // Explicit spawn/checkpoint lookup.
    const V2SpawnSpec* FindStartSpawn() const { return spec_.FindStartSpawn(); }
    const V2SpawnSpec* FindSpawnByName(const char* name) const;

    // Build ActorSpawn entries for the active room's manifest spawn records
    // (with stable source_id). Returns the count written to `out` (up to
    // `max_out`), or -1 if output capacity is insufficient. This uses the
    // manifest table, not LVL entity order, so visual geometry cannot
    // duplicate actor records.
    int ActiveSpawns(ActorSpawn* out, int max_out) const;

    // Enumerate supported actor records across every manifest room. Gameplay
    // uses this once at boot so actor cooldown/collected state survives room
    // transitions. Returns -1 rather than truncating on capacity overflow.
    int AllActorSpawns(ActorSpawn* out, int max_out) const;

private:
    bool InitInPlace(const char* mappack_path, const char* build_dir);
    bool SwapState(MapRuntime& other);
    // Load a room's .lvl into the active view (dynamic state).
    bool LoadRoomIntoActive(const V2RoomSpec& room_spec, ActiveRoomView& view);

    MapSpecV2 spec_;
    WorldCollision collision_;
    DynamicSolidGeometry dynamic_geometry_;
    ActiveRoomView active_;
    bool active_committed_ = false;
    char build_dir_[256] = {};
};

}  // namespace madeline_cube
