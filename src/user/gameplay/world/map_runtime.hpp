#pragma once

#include <cstdint>
#include <cstring>

#include "gameplay/math_types.hpp"
#include "gameplay/world/mappack_loader.hpp"  // MapSpecV2, V2RoomSpec, V2SpawnSpec
#include "gameplay/world/world.hpp"          // WorldCollision, Room, AABB

namespace madeline_cube {

class LvlRoomRenderer;  // forward decl (N64-only; null on host)

// One authoritative active visual room view. Owns room metadata, dynamic
// colliders, a renderer handle, room id, cell key, render origin, and actor
// records. It does NOT own a static collision mesh — all static queries go
// through the global WorldCollision.
struct ActiveRoomView {
    static constexpr int kIdLen = 16;

    char id[kIdLen] = {};
    int cell_ix = 0;
    int cell_iz = 0;
    Vec3 render_origin = {0.0f, 0.0f, 0.0f};
    AABB world_aabb;
    Room room;                       // dynamic colliders, spawns, atmosphere
    LvlRoomRenderer* renderer = nullptr;  // N64-only; null on host
    bool loaded = false;
};

// The authoritative map runtime (Inc 6). Owns ONE global WorldCollision plus
// one staged/active visual room. Replaces the parallel legacy Map pool for
// map-pack v2.
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

    // Stage + commit a visual room as active. Loads the room's .lvl into the
    // active view (renderer + dynamic colliders + spawns). On failure, the old
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
    const MapSpecV2& Spec() const { return spec_; }

    // Explicit spawn/checkpoint lookup.
    const V2SpawnSpec* FindStartSpawn() const { return spec_.FindStartSpawn(); }
    const V2SpawnSpec* FindSpawnByName(const char* name) const;

    // Build ActorSpawn entries for the active room's manifest spawn records
    // (with stable source_id). Returns the count written to `out` (up to
    // `max_out`). This is the authoritative actor source for map-pack v2 —
    // it uses the manifest spawn table, not LVL entity order, so overlapping
    // visual geometry never duplicates an actor record.
    int ActiveSpawns(ActorSpawn* out, int max_out) const;

private:
    // Load a room's .lvl into the active view (renderer + dynamic state).
    bool LoadRoomIntoActive(const V2RoomSpec& room_spec, ActiveRoomView& view);

    MapSpecV2 spec_;
    WorldCollision collision_;
    ActiveRoomView active_;
    bool active_committed_ = false;
    char build_dir_[256] = {};
};

}  // namespace madeline_cube
