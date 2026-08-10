#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"  // AABB

namespace madeline_cube {

// One room (chunk) entry in a map-pack manifest. Mirrors the binary layout
// written by tools/mappack_format.py::write_mappack_binary.
struct MapRoomSpec {
    static constexpr int kIdLen = 16;
    static constexpr int kPathLen = 64;
    static constexpr int kNeighborLen = 16;

    char id[kIdLen] = {};
    char lvl_path[kPathLen] = {};        // rom:/lvl/<pack>/<chunk>.lvl
    char colmesh_path[kPathLen] = {};   // rom:/lvl/<pack>/<chunk>.colmesh ("" if none)
    AABB world_aabb;                     // for preload culling only (NOT containment)
    int cell_ix = 0;                     // parsed from id "cell_ix_iz" (may be negative)
    int cell_iz = 0;
    bool has_colmesh = true;
    bool has_start_spawn = false;
    Vec3 start_spawn = {0.0f, 0.0f, 0.0f};
    // Neighbor room ids per axis (+X,-X,+Z,-Z); "" = no neighbor (map edge).
    char neighbors[4][kNeighborLen] = {};
};

// A parsed map-pack manifest: the room table + shared atmosphere + start room.
// Sits above LVL2/colmesh; each room is a normal single-room .lvl + .colmesh.
struct MapSpec {
    static constexpr int kMaxRooms = 64;  // bake fatal-errors if exceeded
    static constexpr int kSkyboxLen = 16;
    static constexpr int kMusicLen = 24;
    static constexpr int kAmbienceLen = 16;
    static constexpr int kIdLen = 16;

    MapRoomSpec rooms[kMaxRooms];
    int room_count = 0;

    char start_room_id[kIdLen] = {};

    // Shared atmosphere (one skybox/music per map-pack).
    char atmosphere_skybox[kSkyboxLen] = {};
    char atmosphere_music[kMusicLen] = {};
    char atmosphere_ambience[kAmbienceLen] = {};
    float snow_amount = 0.0f;
    Vec3 snow_dir = {0.0f, 0.0f, 0.0f};

    float scale = 0.2f;
    float chunk_size = 1000.0f;  // MAP units (pre-scale); for ResolveCellByPosition

    // Find a room by id. Returns null if not found.
    const MapRoomSpec* FindRoom(const char* id) const;
};

// Load a baked .mappack binary manifest. Path may be "rom:/lvl/<pack>.mappack"
// on device or a local filesystem path in host tests. Returns false on failure.
bool LoadMapPack(const char* path, MapSpec& out);

// ── Map-pack v2 (Inc 6) ─────────────────────────────────────────────
//
// v2 carries the metadata v1 loses: source-map hash, pipeline version, one
// global collision path/hash/counts, per-visual-room render origins, world
// AABBs, artifact hashes/counts, neighbor ids, and a fixed spawn table with
// named Start/anchor/actor records.

// Spawn kinds.
enum SpawnKind : uint8_t {
    kSpawnStart = 0,
    kSpawnAnchor = 1,
    kSpawnActor = 2,
};

struct V2SpawnSpec {
    static constexpr int kRoomIdLen = 16;
    static constexpr int kNameLen = 32;
    static constexpr int kClassLen = 32;

    uint8_t kind = kSpawnActor;
    uint32_t source_id = 0;
    char room_id[kRoomIdLen] = {};
    Vec3 position = {0.0f, 0.0f, 0.0f};
    char name[kNameLen] = {};
    char classname[kClassLen] = {};
};

struct V2RoomSpec {
    static constexpr int kIdLen = 16;
    static constexpr int kPathLen = 64;
    static constexpr int kNeighborLen = 16;
    static constexpr int kMaxSpawns = 64;

    char id[kIdLen] = {};
    char lvl_path[kPathLen] = {};
    Vec3 render_origin = {0.0f, 0.0f, 0.0f};
    AABB world_aabb;
    uint32_t lvl_crc32 = 0;
    uint32_t lvl_size = 0;
    char neighbors[4][kNeighborLen] = {};
    V2SpawnSpec spawns[kMaxSpawns];
    int spawn_count = 0;
    int cell_ix = 0;
    int cell_iz = 0;
};

struct MapSpecV2 {
    static constexpr int kMaxRooms = 64;
    static constexpr int kShaLen = 64;
    static constexpr int kIdLen = 16;
    static constexpr int kPathLen = 64;

    V2RoomSpec rooms[kMaxRooms];
    int room_count = 0;

    char source_sha256[kShaLen] = {};
    uint16_t pipeline_version = 0;
    float scale = 0.2f;
    float chunk_size = 1000.0f;

    char global_colmesh_path[kPathLen] = {};
    uint32_t global_colmesh_crc32 = 0;
    uint32_t global_colmesh_size = 0;
    uint32_t global_vertex_count = 0;
    uint32_t global_triangle_count = 0;
    uint32_t global_bvh_node_count = 0;

    char start_room_id[kIdLen] = {};

    const V2RoomSpec* FindRoom(const char* id) const;
    // Find the Start spawn record (kind == kSpawnStart). Returns null if none.
    const V2SpawnSpec* FindStartSpawn() const;
};

// Load a baked map-pack v2 binary manifest. Returns false on failure.
bool LoadMapPackV2(const char* path, MapSpecV2& out);

}  // namespace madeline_cube