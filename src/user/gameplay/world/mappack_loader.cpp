#include "gameplay/world/mappack_loader.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef __mips__
#include <libdragon.h>
#define MP_LOG debugf
#else
#define MP_LOG printf
#endif

namespace madeline_cube {

namespace {

// Big-endian readers (matching the .lvl/.colmesh convention and
// tools/mappack_format.py's struct.pack(">...")).
static uint16_t ReadU16(FILE* f) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
}

static uint32_t ReadU32(FILE* f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) |
           static_cast<uint32_t>(b[3]);
}

static float ReadF32(FILE* f) {
    const uint32_t v = ReadU32(f);
    float result;
    std::memcpy(&result, &v, sizeof(float));
    return result;
}

static Vec3 ReadVec3(FILE* f) {
    Vec3 v;
    v.x = ReadF32(f);
    v.y = ReadF32(f);
    v.z = ReadF32(f);
    return v;
}

// Read a fixed-width null-padded string field of `len` bytes into `dst`,
// which must be a buffer of at least `len` chars. The last byte is forced
// to '\0' so the field is always a valid C string (sacrificing one char if
// the source string exactly filled the field).
static void ReadFixedString(FILE* f, char* dst, int len) {
    if (len <= 0) return;
    if (fread(dst, 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
        dst[0] = '\0';
        return;
    }
    // Force null-termination within the buffer.
    dst[len - 1] = '\0';
}

}  // namespace

// Parse "cell_ix_iz" where ix/iz may have an 'n' prefix for negative values
// (e.g. "cell_n05_00" -> ix=-5, iz=0). Returns true if parsing succeeded.
static bool ParseCellId(const char* id, int& out_ix, int& out_iz) {
    if (!id) return false;
    const char* p = std::strstr(id, "cell_");
    if (!p) return false;
    p += 5;  // skip "cell_"
    // parse ix
    if (*p == 'n') {
        out_ix = -atoi(p + 1);
    } else {
        out_ix = atoi(p);
    }
    // skip to '_'
    while (*p && *p != '_') p++;
    if (*p != '_') return false;
    p++;  // skip '_'
    // parse iz
    if (*p == 'n') {
        out_iz = -atoi(p + 1);
    } else {
        out_iz = atoi(p);
    }
    return true;
}

const MapRoomSpec* MapSpec::FindRoom(const char* id) const {
    if (!id || id[0] == '\0') return nullptr;
    for (int i = 0; i < room_count; ++i) {
        if (std::strncmp(rooms[i].id, id, MapRoomSpec::kIdLen) == 0) {
            return &rooms[i];
        }
    }
    return nullptr;
}

bool LoadMapPack(const char* path, MapSpec& out) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        MP_LOG("[mappack] open FAILED: %s\n", path);
        return false;
    }

    // Magic
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MPPK", 4) != 0) {
        MP_LOG("[mappack] bad magic: %s\n", path);
        fclose(f);
        return false;
    }

    const uint16_t version = ReadU16(f);
    if (version != 1) {
        MP_LOG("[mappack] unsupported version %u: %s\n", version, path);
        fclose(f);
        return false;
    }

    out.scale = ReadF32(f);
    out.chunk_size = ReadF32(f);

    ReadFixedString(f, out.atmosphere_skybox, MapSpec::kSkyboxLen);
    ReadFixedString(f, out.atmosphere_music, MapSpec::kMusicLen);
    ReadFixedString(f, out.atmosphere_ambience, MapSpec::kAmbienceLen);
    out.snow_amount = ReadF32(f);
    out.snow_dir = ReadVec3(f);

    ReadFixedString(f, out.start_room_id, MapSpec::kIdLen);

    const uint16_t room_count = ReadU16(f);
    if (room_count > MapSpec::kMaxRooms) {
        MP_LOG("[mappack] too many rooms %u (cap %d): %s\n",
               room_count, MapSpec::kMaxRooms, path);
        fclose(f);
        return false;
    }
    out.room_count = static_cast<int>(room_count);

    for (int i = 0; i < out.room_count; ++i) {
        MapRoomSpec& r = out.rooms[i];
        ReadFixedString(f, r.id, MapRoomSpec::kIdLen);
        ReadFixedString(f, r.lvl_path, MapRoomSpec::kPathLen);
        ReadFixedString(f, r.colmesh_path, MapRoomSpec::kPathLen);
        r.world_aabb.min = ReadVec3(f);
        r.world_aabb.max = ReadVec3(f);
        uint8_t flags[2];
        if (fread(flags, 1, 2, f) != 2) { flags[0] = flags[1] = 0; }
        r.has_colmesh = (flags[0] != 0);
        r.has_start_spawn = (flags[1] != 0);
        r.start_spawn = ReadVec3(f);
        for (int n = 0; n < 4; ++n) {
            ReadFixedString(f, r.neighbors[n], MapRoomSpec::kNeighborLen);
        }
        // Parse cell indices from the room id for fast grid lookup.
        if (!ParseCellId(r.id, r.cell_ix, r.cell_iz)) {
            r.cell_ix = r.cell_iz = 0;  // fallback
        }
    }

    fclose(f);
    MP_LOG("[mappack] loaded %s: %d rooms, start=%s, skybox=%s\n",
           path, out.room_count, out.start_room_id, out.atmosphere_skybox);
    return true;
}

// ── Map-pack v2 loader ─────────────────────────────────────────────

const V2RoomSpec* MapSpecV2::FindRoom(const char* id) const {
    if (!id || id[0] == '\0') return nullptr;
    for (int i = 0; i < room_count; ++i) {
        if (std::strncmp(rooms[i].id, id, V2RoomSpec::kIdLen) == 0) {
            return &rooms[i];
        }
    }
    return nullptr;
}

const V2SpawnSpec* MapSpecV2::FindStartSpawn() const {
    for (int i = 0; i < room_count; ++i) {
        for (int s = 0; s < rooms[i].spawn_count; ++s) {
            if (rooms[i].spawns[s].kind == kSpawnStart) {
                return &rooms[i].spawns[s];
            }
        }
    }
    return nullptr;
}

bool LoadMapPackV2(const char* path, MapSpecV2& out) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        MP_LOG("[mappack] v2 open FAILED: %s\n", path);
        return false;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MPP2", 4) != 0) {
        MP_LOG("[mappack] v2 bad magic: %s\n", path);
        fclose(f);
        return false;
    }

    const uint16_t version = ReadU16(f);
    if (version != 2) {
        MP_LOG("[mappack] v2 unsupported version %u: %s\n", version, path);
        fclose(f);
        return false;
    }

    out.pipeline_version = ReadU16(f);
    ReadFixedString(f, out.source_sha256, MapSpecV2::kShaLen);
    out.scale = ReadF32(f);
    out.chunk_size = ReadF32(f);
    ReadFixedString(f, out.global_colmesh_path, MapSpecV2::kPathLen);
    out.global_colmesh_crc32 = ReadU32(f);
    out.global_colmesh_size = ReadU32(f);
    out.global_vertex_count = ReadU32(f);
    out.global_triangle_count = ReadU32(f);
    out.global_bvh_node_count = ReadU32(f);
    ReadFixedString(f, out.start_room_id, MapSpecV2::kIdLen);

    const uint16_t room_count = ReadU16(f);
    if (room_count > MapSpecV2::kMaxRooms) {
        MP_LOG("[mappack] v2 too many rooms %u (cap %d): %s\n",
               room_count, MapSpecV2::kMaxRooms, path);
        fclose(f);
        return false;
    }
    out.room_count = static_cast<int>(room_count);

    for (int i = 0; i < out.room_count; ++i) {
        V2RoomSpec& r = out.rooms[i];
        ReadFixedString(f, r.id, V2RoomSpec::kIdLen);
        ReadFixedString(f, r.lvl_path, V2RoomSpec::kPathLen);
        r.render_origin = ReadVec3(f);
        r.world_aabb.min = ReadVec3(f);
        r.world_aabb.max = ReadVec3(f);
        r.lvl_crc32 = ReadU32(f);
        r.lvl_size = ReadU32(f);
        for (int n = 0; n < 4; ++n) {
            ReadFixedString(f, r.neighbors[n], V2RoomSpec::kNeighborLen);
        }
        const uint16_t spawn_count = ReadU16(f);
        if (spawn_count > V2RoomSpec::kMaxSpawns) {
            MP_LOG("[mappack] v2 room %s too many spawns %u (cap %d)\n",
                   r.id, spawn_count, V2RoomSpec::kMaxSpawns);
            fclose(f);
            return false;
        }
        r.spawn_count = static_cast<int>(spawn_count);
        if (!ParseCellId(r.id, r.cell_ix, r.cell_iz)) {
            r.cell_ix = r.cell_iz = 0;
        }
    }

    // Spawn table (all rooms' spawns, in room order).
    for (int i = 0; i < out.room_count; ++i) {
        V2RoomSpec& r = out.rooms[i];
        for (int s = 0; s < r.spawn_count; ++s) {
            V2SpawnSpec& sp = r.spawns[s];
            uint8_t kind;
            if (fread(&kind, 1, 1, f) != 1) { kind = kSpawnActor; }
            sp.kind = kind;
            sp.source_id = ReadU32(f);
            ReadFixedString(f, sp.room_id, V2SpawnSpec::kRoomIdLen);
            sp.position = ReadVec3(f);
            ReadFixedString(f, sp.name, V2SpawnSpec::kNameLen);
            ReadFixedString(f, sp.classname, V2SpawnSpec::kClassLen);
        }
    }

    fclose(f);
    MP_LOG("[mappack] v2 loaded %s: %d rooms, start=%s, global=%s (%lu tris)\n",
           path, out.room_count, out.start_room_id, out.global_colmesh_path,
           (unsigned long)out.global_triangle_count);
    return true;
}

}  // namespace madeline_cube