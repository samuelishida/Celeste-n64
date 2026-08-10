#include "gameplay/world/map.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>

#include "gameplay/physics/coll_mesh.hpp"

#ifdef __mips__
#include <libdragon.h>
#define MAP_LOG debugf
#include "gameplay/render/lvl_room_renderer.hpp"
#else
#define MAP_LOG printf
#endif

namespace madeline_cube {

namespace {

// Big-endian readers reused for the colmesh swap-extension logic (not needed
// here, but kept consistent with the codebase conventions).

}  // namespace

Map::~Map() {
    Reset();
}

void Map::Init(const MapSpec& spec) {
    Reset();
    spec_ = spec;
    active_index_ = -1;
    checkpoint_slot_ = -1;
    lru_counter_ = 0;
}

void Map::Reset() {
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        FreeSlot(i);
    }
    active_index_ = -1;
    checkpoint_slot_ = -1;
    spec_ = MapSpec{};
}

void Map::FreeSlot(int slot) {
    if (slot < 0 || slot >= kMaxLoadedRooms) return;
    LoadedRoom& lr = pool_[slot];
#ifdef __mips__
    if (lr.renderer) {
        lr.renderer->Free();
        // LvlRoomRenderer is owned via pointer only on device.
        delete lr.renderer;
        lr.renderer = nullptr;
    }
#endif
    if (lr.room.coll_mesh) {
        physics::FreeCollMesh(lr.room.coll_mesh);
        lr.room.coll_mesh = nullptr;
    }
    lr = LoadedRoom{};
}

int Map::FindOrEvictSlot(const char* room_id) {
    // Already loaded?
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        if (pool_[i].loaded && std::strncmp(pool_[i].id, room_id, LoadedRoom::kIdLen) == 0) {
            return i;
        }
    }
    // Free slot?
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        if (!pool_[i].loaded) return i;
    }
    // Evict the LRU non-pinned room.
    int victim = -1;
    int oldest_tick = INT32_MAX;
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        if (i == active_index_) continue;
        if (i == checkpoint_slot_) continue;
        if (pool_[i].lru_tick < oldest_tick) {
            oldest_tick = pool_[i].lru_tick;
            victim = i;
        }
    }
    if (victim < 0) {
        MAP_LOG("[map] no evictable slot (all pinned) for room %s\n", room_id);
        return -1;
    }
    MAP_LOG("[map] evicting room %s (slot %d)\n", pool_[victim].id, victim);
    FreeSlot(victim);
    return victim;
}

bool Map::LoadSlot(int slot, const MapRoomSpec& room_spec) {
    LoadedRoom& lr = pool_[slot];
    lr = LoadedRoom{};
    std::strncpy(lr.id, room_spec.id, LoadedRoom::kIdLen - 1);

    MAP_LOG("[map] LoadSlot: %s lvl=%s colmesh=%s has_colmesh=%d\n",
            room_spec.id, room_spec.lvl_path, room_spec.colmesh_path, (int)room_spec.has_colmesh);

    // Load .lvl into the shared geometry scratch + room (entities, atmosphere).
    if (!LoadLevelInto(room_spec.lvl_path, lr.room, lr.geometry)) {
        MAP_LOG("[map] LoadLevelInto FAILED for %s (%s)\n",
                room_spec.id, room_spec.lvl_path);
        lr.loaded = false;
        return false;
    }
    MAP_LOG("[map] LoadLevelInto OK for %s: player_start=(%.1f,%.1f,%.1f) coll_mesh=%p\n",
            room_spec.id, (double)lr.room.player_start.x, (double)lr.room.player_start.y, (double)lr.room.player_start.z, (void*)lr.room.coll_mesh);

    // Load .colmesh sidecar (collision). The room's player_start/checkpoint
    // are filled by LoadLevelInto from the .lvl entities.
    if (room_spec.has_colmesh && room_spec.colmesh_path[0] != '\0') {
        MAP_LOG("[map] Loading colmesh: %s\n", room_spec.colmesh_path);
        lr.room.coll_mesh = physics::LoadCollMesh(room_spec.colmesh_path);
        if (!lr.room.coll_mesh) {
            MAP_LOG("[map] LoadCollMesh FAILED for %s (%s)\n",
                    room_spec.id, room_spec.colmesh_path);
            // Non-fatal: room still has entities/atmosphere; collision just absent.
        } else {
            MAP_LOG("[map] LoadCollMesh OK for %s: coll_mesh=%p\n", room_spec.id, (void*)lr.room.coll_mesh);
        }
    } else {
        MAP_LOG("[map] No colmesh for %s (has_colmesh=%d path=%s)\n",
                room_spec.id, (int)room_spec.has_colmesh, room_spec.colmesh_path);
    }

#ifdef __mips__
    // Build the renderer from the .lvl (N64-only).
    lr.renderer = new LvlRoomRenderer();
    if (!lr.renderer->Load(room_spec.lvl_path)) {
        MAP_LOG("[map] LvlRoomRenderer::Load FAILED for %s\n", room_spec.id);
        delete lr.renderer;
        lr.renderer = nullptr;
        // Keep the room loaded (collision + entities) even if render fails.
    }
#endif

    lr.lru_tick = ++lru_counter_;
    lr.loaded = true;
    MAP_LOG("[map] loaded room %s into slot %d\n", room_spec.id, slot);
    return true;
}

bool Map::EnsureLoaded(const char* room_id, int ring) {
    if (!room_id || room_id[0] == '\0') return false;
    const MapRoomSpec* rs = spec_.FindRoom(room_id);
    if (!rs) {
        MAP_LOG("[map] EnsureLoaded: unknown room %s\n", room_id);
        return false;
    }
    int slot = FindOrEvictSlot(room_id);
    if (slot < 0) return false;
    if (!pool_[slot].loaded) {
        if (!LoadSlot(slot, *rs)) return false;
    } else {
        pool_[slot].lru_tick = ++lru_counter_;
    }

    // Load the neighbor ring (ring > 0). Each neighbor is a non-pinned LRU
    // candidate. Errors here are non-fatal (preload best-effort).
    for (int r = 1; r <= ring; ++r) {
        for (int axis = 0; axis < 4; ++axis) {
            const char* nb_id = rs->neighbors[axis];
            if (nb_id[0] == '\0') continue;
            const MapRoomSpec* nb = spec_.FindRoom(nb_id);
            if (!nb) continue;
            int nb_slot = FindOrEvictSlot(nb_id);
            if (nb_slot < 0) continue;
            if (!pool_[nb_slot].loaded) {
                LoadSlot(nb_slot, *nb);
            } else {
                pool_[nb_slot].lru_tick = ++lru_counter_;
            }
        }
    }
    return true;
}

void Map::SetCheckpointRoom(const char* room_id) {
    if (!room_id || room_id[0] == '\0') {
        checkpoint_slot_ = -1;
        return;
    }
    // Find the loaded slot for this room; pin it.
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        if (pool_[i].loaded && std::strncmp(pool_[i].id, room_id, LoadedRoom::kIdLen) == 0) {
            checkpoint_slot_ = i;
            pool_[i].pinned = true;
            return;
        }
    }
    // Not currently loaded — remember to pin it when it loads via EnsureLoaded.
    // Store the desired id by loading it now (checkpoint must be resident).
    if (EnsureLoaded(room_id, 0)) {
        for (int i = 0; i < kMaxLoadedRooms; ++i) {
            if (pool_[i].loaded && std::strncmp(pool_[i].id, room_id, LoadedRoom::kIdLen) == 0) {
                checkpoint_slot_ = i;
                pool_[i].pinned = true;
                return;
            }
        }
    }
    checkpoint_slot_ = -1;
}

const char* Map::ResolveCellByPosition(const Vec3& pos) const {
    if (!HasMap() || spec_.chunk_size <= 0.0f) return "";
    // World coords are post-scale. The bake partition used chunk_size in MAP
    // units (pre-scale), and world = map * scale, so the world-space grid cell
    // index is floor(world / (chunk_size * scale)).
    const float cell_world = spec_.chunk_size * spec_.scale;
    const float invw = 1.0f / cell_world;
    const int ix = static_cast<int>(std::floor(pos.x * invw));
    const int iz = static_cast<int>(std::floor(pos.z * invw));
    // Match by the pre-parsed cell indices stored in MapRoomSpec.
    for (int i = 0; i < spec_.room_count; ++i) {
        if (spec_.rooms[i].cell_ix == ix && spec_.rooms[i].cell_iz == iz) {
            return spec_.rooms[i].id;
        }
    }
    return "";
}

bool Map::SetActivByPosition(const Vec3& player_pos, const char** new_room_id) {
    const char* resolved = ResolveCellByPosition(player_pos);
    if (resolved[0] == '\0') return false;
    if (active_index_ >= 0 &&
        std::strncmp(pool_[active_index_].id, resolved, LoadedRoom::kIdLen) == 0) {
        return false;  // unchanged
    }
    if (new_room_id) *new_room_id = resolved;
    return true;
}

LoadedRoom* Map::SetActive(const char* room_id) {
    if (!room_id || room_id[0] == '\0') return nullptr;
    // Unpin the previous active (unless it's the checkpoint).
    if (active_index_ >= 0 && active_index_ != checkpoint_slot_) {
        pool_[active_index_].pinned = false;
    }
    for (int i = 0; i < kMaxLoadedRooms; ++i) {
        if (pool_[i].loaded && std::strncmp(pool_[i].id, room_id, LoadedRoom::kIdLen) == 0) {
            active_index_ = i;
            pool_[i].pinned = true;
            pool_[i].lru_tick = ++lru_counter_;
            return &pool_[i];
        }
    }
    MAP_LOG("[map] SetActive: room %s not loaded\n", room_id);
    return nullptr;
}

bool Map::LoadRoomGeometry(const char* room_id) {
    // EnsureLoaded + SetActive, without resetting the player. The caller
    // (chunk transition) preserves player pos/velocity across the swap.
    if (!EnsureLoaded(room_id, 0)) return false;
    return SetActive(room_id) != nullptr;
}

const char* Map::ActiveRoomId() const {
    if (active_index_ < 0) return "";
    return pool_[active_index_].id;
}

Room& Map::ActiveRoom() {
    static Room fallback{};
    if (active_index_ < 0) return fallback;
    return pool_[active_index_].room;
}

const Room& Map::ActiveRoom() const {
    static Room fallback{};
    if (active_index_ < 0) return fallback;
    return pool_[active_index_].room;
}

}  // namespace madeline_cube