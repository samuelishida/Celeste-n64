#include "gameplay/world/mappack_loader.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <new>

#include "gameplay/debug_flags.hpp"  // kEnableCompressedCells (memory-opt Inc 2)

#ifdef __mips__
#include <libdragon.h>
#include <asset.h>
#define MP_LOG debugf
#else
#define MP_LOG printf
#endif

namespace madeline_cube {

namespace {

// memory-opt Inc 2: open a map-pack file through the asset layer when
// compression is enabled (streaming decompression via asset_fopen — the
// manifest is read sequentially, never fseek'd), else the plain fopen path.
// Host builds (no __mips__) always use fopen. Tolerant: on asset_fopen
// failure we fall back to fopen (a wrong-level bake then fails loud in the
// magic checks below).
// memory-opt Inc 3: both paths are CPU-copied reads (fread / asset
// streaming decompression) — the manifest buffer is never handed to DMA, so
// alignment is n/a.
FILE* OpenMapPackFile(const char* path) {
#ifdef __mips__
    if (kEnableCompressedCells) {
        int size = 0;
        FILE* f = asset_fopen(path, &size);
        if (f) return f;
        MP_LOG("[mappack] asset_fopen FAILED (falling back to fopen): %s\n", path);
    }
#endif
    return fopen(path, "rb");
}

}  // namespace

namespace {

// Big-endian readers (matching the .lvl/.colmesh convention and
// tools/mappack_format.py's struct.pack(">...")).
static uint16_t ReadU16(FILE* f, bool* ok = nullptr) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) {
        if (ok) *ok = false;
        return 0;
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
}

static uint32_t ReadU32(FILE* f, bool* ok = nullptr) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) {
        if (ok) *ok = false;
        return 0;
    }
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) |
           static_cast<uint32_t>(b[3]);
}

static float ReadF32(FILE* f, bool* ok = nullptr) {
    const uint32_t v = ReadU32(f, ok);
    float result;
    std::memcpy(&result, &v, sizeof(float));
    return result;
}

static Vec3 ReadVec3(FILE* f, bool* ok = nullptr) {
    Vec3 v;
    v.x = ReadF32(f, ok);
    v.y = ReadF32(f, ok);
    v.z = ReadF32(f, ok);
    return v;
}

// Read a fixed-width null-padded string field of `len` bytes into `dst`,
// which must be a buffer of at least `len` chars. The last byte is forced
// to '\0' so the field is always a valid C string (sacrificing one char if
// the source string exactly filled the field).
static void ReadFixedString(FILE* f, char* dst, int len, bool* ok = nullptr) {
    if (len <= 0) return;
    if (fread(dst, 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
        dst[0] = '\0';
        if (ok) *ok = false;
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
    FILE* f = OpenMapPackFile(path);
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

int MapSpecV2::AllActorSpawns(ActorSpawn* out, int max_out) const {
    if (!out || max_out < 0) return -1;
    int count = 0;
    for (int r = 0; r < room_count; ++r) {
        const V2RoomSpec& room = rooms[r];
        for (int s = 0; s < room.spawn_count; ++s) {
            const V2SpawnSpec& sp = room.spawns[s];
            if (sp.kind != kSpawnActor) continue;
            uint16_t placeholder_id = 0;
            if (std::strncmp(sp.classname, "Strawberry", V2SpawnSpec::kClassLen) == 0) {
                placeholder_id = 2;
            } else if (std::strncmp(sp.classname, "Refill", V2SpawnSpec::kClassLen) == 0) {
                placeholder_id = 3;
            } else if (std::strncmp(sp.classname, "Spring", V2SpawnSpec::kClassLen) == 0) {
                placeholder_id = 7;
            } else {
                continue;
            }
            if (count >= max_out) return -1;
            out[count].position = sp.position;
            out[count].source_id = sp.source_id;
            out[count].placeholder_id = placeholder_id;
            out[count].actor_flags = sp.actor_flags;
            ++count;
        }
    }
    return count;
}

static bool LoadMapPackV2Impl(const char* path, MapSpecV2& out) {
    out.room_count = 0;
    out.hazard_count = 0;
    FILE* f = OpenMapPackFile(path);
    if (!f) {
        MP_LOG("[mappack] open FAILED: %s\n", path);
        return false;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 ||
        (std::memcmp(magic, "MPP2", 4) != 0 &&
         std::memcmp(magic, "MPP3", 4) != 0 &&
         std::memcmp(magic, "MPP4", 4) != 0 &&
         std::memcmp(magic, "MPP5", 4) != 0)) {
        MP_LOG("[mappack] bad magic: %s\n", path);
        fclose(f);
        return false;
    }

    bool read_ok = true;
    const uint16_t version = ReadU16(f, &read_ok);
    const bool has_actor_flags = version >= 3;
    const bool has_hazards = version >= 4;
    const bool has_dynamic_solids = version == 5;
    if (!read_ok || version < 2 || version > 5 ||
        std::memcmp(magic, version == 2 ? "MPP2" :
                    (version == 3 ? "MPP3" :
                     (version == 4 ? "MPP4" : "MPP5")), 4) != 0) {
        MP_LOG("[mappack] magic/version mismatch %u: %s\n", version, path);
        fclose(f);
        return false;
    }

    out.pipeline_version = ReadU16(f, &read_ok);
    ReadFixedString(f, out.source_sha256, MapSpecV2::kShaLen, &read_ok);
    out.scale = ReadF32(f, &read_ok);
    out.chunk_size = ReadF32(f, &read_ok);
    ReadFixedString(f, out.global_colmesh_path, MapSpecV2::kPathLen, &read_ok);
    out.global_colmesh_crc32 = ReadU32(f, &read_ok);
    out.global_colmesh_size = ReadU32(f, &read_ok);
    out.global_vertex_count = ReadU32(f, &read_ok);
    out.global_triangle_count = ReadU32(f, &read_ok);
    out.global_bvh_node_count = ReadU32(f, &read_ok);
    ReadFixedString(f, out.start_room_id, MapSpecV2::kIdLen, &read_ok);
    if (!read_ok) {
        MP_LOG("[mappack] truncated v%u header: %s\n", version, path);
        fclose(f);
        return false;
    }

    const uint16_t room_count = ReadU16(f, &read_ok);
    if (!read_ok) {
        MP_LOG("[mappack] truncated room count: %s\n", path);
        fclose(f);
        return false;
    }
    if (room_count > MapSpecV2::kMaxRooms) {
        MP_LOG("[mappack] v2 too many rooms %u (cap %d): %s\n",
               room_count, MapSpecV2::kMaxRooms, path);
        fclose(f);
        return false;
    }
    out.room_count = static_cast<int>(room_count);

    for (int i = 0; i < out.room_count; ++i) {
        V2RoomSpec& r = out.rooms[i];
        ReadFixedString(f, r.id, V2RoomSpec::kIdLen, &read_ok);
        ReadFixedString(f, r.lvl_path, V2RoomSpec::kPathLen, &read_ok);
        r.render_origin = ReadVec3(f, &read_ok);
        r.world_aabb.min = ReadVec3(f, &read_ok);
        r.world_aabb.max = ReadVec3(f, &read_ok);
        r.lvl_crc32 = ReadU32(f, &read_ok);
        r.lvl_size = ReadU32(f, &read_ok);
        for (int n = 0; n < 4; ++n) {
            ReadFixedString(f, r.neighbors[n], V2RoomSpec::kNeighborLen, &read_ok);
        }
        const uint16_t spawn_count = ReadU16(f, &read_ok);
        if (!read_ok) {
            MP_LOG("[mappack] truncated room record: %s\n", path);
            fclose(f);
            out.room_count = 0;
            return false;
        }
        if (spawn_count > V2RoomSpec::kMaxSpawns) {
            MP_LOG("[mappack] v2 room %s too many spawns %u (cap %d)\n",
                   r.id, spawn_count, V2RoomSpec::kMaxSpawns);
            fclose(f);
            out.room_count = 0;
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
            if (fread(&kind, 1, 1, f) != 1) { kind = kSpawnActor; read_ok = false; }
            sp.kind = kind;
            sp.source_id = ReadU32(f, &read_ok);
            ReadFixedString(f, sp.room_id, V2SpawnSpec::kRoomIdLen, &read_ok);
            sp.position = ReadVec3(f, &read_ok);
            ReadFixedString(f, sp.name, V2SpawnSpec::kNameLen, &read_ok);
            ReadFixedString(f, sp.classname, V2SpawnSpec::kClassLen, &read_ok);
            if (has_actor_flags) {
                if (fread(&sp.actor_flags, 1, 1, f) != 1) read_ok = false;
            } else {
                sp.actor_flags = 0;
            }
            if (!read_ok || sp.kind > kSpawnActor ||
                (sp.actor_flags & static_cast<uint8_t>(~0x01u)) != 0 ||
                (sp.actor_flags != 0 &&
                 (sp.kind != kSpawnActor || std::strcmp(sp.classname, "Refill") != 0))) {
                MP_LOG("[mappack] invalid/truncated spawn record in %s\n", path);
                fclose(f);
                out.room_count = 0;
                return false;
            }
        }
    }

    if (has_hazards) {
        const uint16_t hazard_count = ReadU16(f, &read_ok);
        if (!read_ok || hazard_count > MapSpecV2::kMaxHazards) {
            MP_LOG("[mappack] invalid hazard count %u (cap %d): %s\n",
                   hazard_count, MapSpecV2::kMaxHazards, path);
            fclose(f);
            out.room_count = 0;
            return false;
        }
        out.hazard_count = static_cast<int>(hazard_count);
        for (int i = 0; i < out.hazard_count; ++i) {
            HazardVolumeSpec& hazard = out.hazards[i];
            if (fread(&hazard.kind, 1, 1, f) != 1) read_ok = false;
            hazard.source_id = ReadU32(f, &read_ok);
            hazard.bounds.min = ReadVec3(f, &read_ok);
            hazard.bounds.max = ReadVec3(f, &read_ok);
            hazard.spike_direction = ReadVec3(f, &read_ok);
            const bool valid_kind = hazard.kind == kHazardDeathBlock ||
                                    hazard.kind == kHazardSpikeBlock;
            const Vec3& lo = hazard.bounds.min;
            const Vec3& hi = hazard.bounds.max;
            const Vec3& direction = hazard.spike_direction;
            const bool finite = std::isfinite(lo.x) && std::isfinite(lo.y) &&
                std::isfinite(lo.z) && std::isfinite(hi.x) &&
                std::isfinite(hi.y) && std::isfinite(hi.z) &&
                std::isfinite(direction.x) && std::isfinite(direction.y) &&
                std::isfinite(direction.z);
            const float length2 = direction.x * direction.x +
                                  direction.y * direction.y +
                                  direction.z * direction.z;
            const bool valid_direction = hazard.kind == kHazardDeathBlock
                ? length2 == 0.0f
                : std::fabs(length2 - 1.0f) <= 1.0e-4f;
            if (!read_ok || !valid_kind || !finite ||
                lo.x > hi.x || lo.y > hi.y || lo.z > hi.z ||
                !valid_direction) {
                MP_LOG("[mappack] invalid/truncated hazard record %d in %s\n",
                       i, path);
                fclose(f);
                out.room_count = 0;
                out.hazard_count = 0;
                return false;
            }
        }
    }

    if (has_dynamic_solids) {
        ReadFixedString(f, out.dsol_path, MapSpecV2::kDsolPathLen, &read_ok);
        out.dsol_crc32 = ReadU32(f, &read_ok);
        out.dsol_size = ReadU32(f, &read_ok);
        const uint16_t count = ReadU16(f, &read_ok);
        const size_t dsol_path_len = std::strlen(out.dsol_path);
        if (!read_ok || std::strncmp(out.dsol_path, "rom:/lvl/", 9) != 0 ||
            dsol_path_len < 14 ||
            std::strcmp(out.dsol_path + dsol_path_len - 5, ".dsol") != 0 ||
            std::strstr(out.dsol_path, "..") != nullptr ||
            out.dsol_size < 16 || count > MapSpecV2::kMaxDynamicSolids) {
            MP_LOG("[mappack] invalid DSOL metadata/count %u in %s\n", count, path);
            fclose(f);
            return false;
        }
        out.dynamic_solid_count = static_cast<int>(count);
        uint32_t next_face = 0;
        uint32_t next_vertex = 0;
        uint32_t previous_source = 0;
        for (int i = 0; i < out.dynamic_solid_count; ++i) {
            DynamicSolidSpec& actor = out.dynamic_solids[i];
            actor.source_id = ReadU32(f, &read_ok);
            actor.solid_id = ReadU16(f, &read_ok);
            if (fread(&actor.kind, 1, 1, f) != 1) read_ok = false;
            if (fread(&actor.flags, 1, 1, f) != 1) read_ok = false;
            actor.position = ReadVec3(f, &read_ok);
            actor.yaw_radians = ReadF32(f, &read_ok);
            actor.cms_first_source_face_id = ReadU16(f, &read_ok);
            actor.cms_face_count = ReadU16(f, &read_ok);
            actor.dsol_first_face = ReadU32(f, &read_ok);
            actor.dsol_face_count = ReadU32(f, &read_ok);
            actor.dsol_first_vertex = ReadU32(f, &read_ok);
            actor.dsol_vertex_count = ReadU32(f, &read_ok);
            actor.target_source_id = ReadU32(f, &read_ok);
            actor.target_position = ReadVec3(f, &read_ok);

            const bool finite = std::isfinite(actor.position.x) &&
                std::isfinite(actor.position.y) && std::isfinite(actor.position.z) &&
                std::isfinite(actor.yaw_radians) &&
                std::isfinite(actor.target_position.x) &&
                std::isfinite(actor.target_position.y) &&
                std::isfinite(actor.target_position.z);
            const bool valid_flags = (actor.flags & 0xFEu) == 0 &&
                (actor.flags == 0 || actor.kind == 2 || actor.kind == 4);
            const bool target_zero = actor.target_source_id != 0xFFFFFFFFu ||
                (actor.target_position.x == 0.0f && actor.target_position.y == 0.0f &&
                 actor.target_position.z == 0.0f);
            const bool valid_ranges = actor.cms_first_source_face_id != 0 &&
                actor.cms_face_count != 0 &&
                static_cast<uint32_t>(actor.cms_first_source_face_id) +
                    actor.cms_face_count - 1u <= 0xFFFFu &&
                actor.dsol_first_face == next_face && actor.dsol_face_count != 0 &&
                actor.dsol_first_vertex == next_vertex && actor.dsol_vertex_count != 0 &&
                actor.dsol_first_face + actor.dsol_face_count >= actor.dsol_first_face &&
                actor.dsol_first_vertex + actor.dsol_vertex_count >= actor.dsol_first_vertex;
            if (!read_ok || actor.kind < 1 || actor.kind > 8 || !valid_flags ||
                !target_zero || !finite || !valid_ranges ||
                (i > 0 && actor.source_id <= previous_source)) {
                MP_LOG("[mappack] invalid dynamic-solid record %d in %s\n", i, path);
                fclose(f);
                return false;
            }
            for (int j = 0; j < i; ++j) {
                if (out.dynamic_solids[j].source_id == actor.source_id ||
                    out.dynamic_solids[j].solid_id == actor.solid_id) {
                    MP_LOG("[mappack] duplicate dynamic-solid identity in %s\n", path);
                    fclose(f);
                    return false;
                }
            }
            previous_source = actor.source_id;
            next_face += actor.dsol_face_count;
            next_vertex += actor.dsol_vertex_count;
        }
    }

    if (fgetc(f) != EOF || ferror(f)) {
        MP_LOG("[mappack] trailing or unreadable bytes: %s\n", path);
        fclose(f);
        out.room_count = 0;
        out.hazard_count = 0;
        return false;
    }

    fclose(f);
    MP_LOG("[mappack] v%u loaded %s: %d rooms, %d hazards, %d dynamic solids, start=%s, global=%s (%lu tris)\n",
           version,
           path, out.room_count, out.hazard_count, out.dynamic_solid_count,
           out.start_room_id, out.global_colmesh_path,
           (unsigned long)out.global_triangle_count);
    return true;
}

bool LoadMapPackV2(const char* path, MapSpecV2& out) {
    // Parse off to the side so malformed input cannot partially replace an
    // already-published manifest. Heap allocation avoids a large N64 stack
    // frame from the fixed-capacity room/hazard/actor tables.
    MapSpecV2* candidate = new (std::nothrow) MapSpecV2{};
    if (!candidate) return false;
    const bool loaded = LoadMapPackV2Impl(path, *candidate);
    if (loaded) out = *candidate;
    delete candidate;
    return loaded;
}

}  // namespace madeline_cube
