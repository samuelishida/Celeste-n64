#include "gameplay/world/level_loader.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/entity_ids.hpp"

#ifdef __mips__
#include <libdragon.h>
#define LVL_LOG debugf
#else
#define LVL_LOG printf
#endif

namespace madeline_cube {

namespace {

static uint32_t ReadU32(FILE* f) {
    uint8_t b[4];
    fread(b, 1, 4, f);
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) |
           static_cast<uint32_t>(b[3]);
}

static uint16_t ReadU16(FILE* f) {
    uint8_t b[2];
    fread(b, 1, 2, f);
    return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
}

static int16_t ReadS16(FILE* f) {
    return static_cast<int16_t>(ReadU16(f));
}

static float ReadF32(FILE* f) {
    const uint32_t v = ReadU32(f);
    float result;
    memcpy(&result, &v, sizeof(float));
    return result;
}

static Vec3 ReadVec3(FILE* f) {
    Vec3 v;
    v.x = ReadF32(f);
    v.y = ReadF32(f);
    v.z = ReadF32(f);
    return v;
}

static void CopyString(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

}  // namespace

bool LoadLevel(const char* path, Room& room, LevelGeometry& geometry) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LVL_LOG("[lvl] open FAILED: %s\n", path);
        return false;
    }

    // --- Header ---
    char magic[4];
    fread(magic, 1, 4, f);
    if (magic[0] == 'L' && magic[1] == 'V' && magic[2] == 'L' && magic[3] == '1') {
        LVL_LOG("[lvl] file is LVL1; re-bake required (run 'make clean && make')\n");
        fclose(f);
        return false;
    }
    if (magic[0] != 'L' || magic[1] != 'V' || magic[2] != 'L' || magic[3] != '2') {
        LVL_LOG("[lvl] bad magic in %s\n", path);
        fclose(f);
        return false;
    }

    const uint32_t version = ReadU32(f);
    if (version == 1) {
        LVL_LOG("[lvl] file is LVL1; re-bake required (run 'make clean && make')\n");
        fclose(f);
        return false;
    }
    if (version != 2) {
        LVL_LOG("[lvl] unsupported version %lu\n", static_cast<unsigned long>(version));
        fclose(f);
        return false;
    }

    (void)ReadU32(f);  // collider_count — static colliders no longer loaded
    const uint32_t face_count     = ReadU32(f);
    const uint32_t vertex_count   = ReadU32(f);
    const uint32_t entity_count   = ReadU32(f);
    const uint32_t string_count   = ReadU32(f);

    const uint16_t skybox_str_id = ReadU16(f);
    const uint16_t music_str_id = ReadU16(f);
    const uint16_t ambience_str_id = ReadU16(f);
    const uint16_t snow_amount_q8 = ReadU16(f);
    const int16_t snow_dir_x_q8 = ReadS16(f);
    const int16_t snow_dir_y_q8 = ReadS16(f);
    const int16_t snow_dir_z_q8 = ReadS16(f);
    ReadU16(f);  // reserved

    const uint32_t off_strings = ReadU32(f);
    (void)ReadU32(f);  // off_colliders — static collision replaced by .colmesh
    const uint32_t off_faces      = ReadU32(f);
    const uint32_t off_vertices   = ReadU32(f);
    const uint32_t off_entities   = ReadU32(f);
    const uint32_t off_props_blob = ReadU32(f);

    std::vector<std::string> strings;
    strings.reserve(string_count);
    if (string_count > 0) {
        fseek(f, static_cast<long>(off_strings), SEEK_SET);
        for (uint32_t i = 0; i < string_count; ++i) {
            const uint8_t str_len = static_cast<uint8_t>(fgetc(f));
            std::string s;
            s.resize(str_len);
            if (str_len > 0) {
                fread(&s[0], 1, str_len, f);
            }
            strings.push_back(std::move(s));
        }
    }

    std::vector<uint8_t> props_blob;
    long file_size = 0;
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    if (off_props_blob > 0 &&
        file_size >= 0 &&
        off_props_blob <= static_cast<uint32_t>(file_size)) {
        const size_t props_size = static_cast<size_t>(file_size) - off_props_blob;
        props_blob.resize(props_size);
        fseek(f, static_cast<long>(off_props_blob), SEEK_SET);
        if (props_size > 0) {
            fread(props_blob.data(), 1, props_size, f);
        }
    }

    const auto StringAt = [&](uint16_t id) -> const char* {
        if (id >= strings.size()) return "";
        return strings[id].c_str();
    };
    const auto PropsRange = [&](uint32_t offset, uint32_t len, const uint8_t** out) -> bool {
        if (out) *out = nullptr;
        if (offset > props_blob.size()) return false;
        if (len == 0) {
            if (out && !props_blob.empty()) *out = props_blob.data() + offset;
            return true;
        }
        if (len > props_blob.size() - offset) return false;
        if (out) *out = props_blob.data() + offset;
        return true;
    };

    CopyString(room.skybox, sizeof(room.skybox), StringAt(skybox_str_id));
    CopyString(room.music, sizeof(room.music), StringAt(music_str_id));
    CopyString(room.ambience, sizeof(room.ambience), StringAt(ambience_str_id));
    room.snow_amount = static_cast<float>(snow_amount_q8) / 256.0f;
    room.snow_dir = {
        static_cast<float>(snow_dir_x_q8) / 256.0f,
        static_cast<float>(snow_dir_y_q8) / 256.0f,
        static_cast<float>(snow_dir_z_q8) / 256.0f,
    };
    LVL_LOG("[lvl] atmos: sky=%s music=%s amb=%s snow=%.2f dir=(%.2f,%.2f,%.2f)\n",
            room.skybox, room.music, room.ambience,
            static_cast<double>(room.snow_amount),
            static_cast<double>(room.snow_dir.x),
            static_cast<double>(room.snow_dir.y),
            static_cast<double>(room.snow_dir.z));

    // --- Colliders: skipped (static collision now uses .colmesh) ---
    // collider_count is left at 0; dynamic actors (moving platforms) add
    // to room.colliders at runtime via SolidActor::SyncToRoom.
    room.collider_count = 0;

    // --- Faces (24 bytes each) ---
    if (face_count > static_cast<unsigned>(LevelGeometry::kMaxFaces)) {
        LVL_LOG("[lvl] face_count %lu exceeds kMaxFaces %d\n",
                static_cast<unsigned long>(face_count), LevelGeometry::kMaxFaces);
        fclose(f);
        return false;
    }
    fseek(f, static_cast<long>(off_faces), SEEK_SET);
    geometry.face_count = 0;
    for (uint32_t i = 0; i < face_count; ++i) {
        const uint32_t vs = ReadU32(f);
        const uint32_t vc = ReadU32(f);
        const uint16_t mid = ReadU16(f);
        const uint16_t flags = ReadU16(f);
        const Vec3 norm = ReadVec3(f);

        LevelFace& lf = geometry.faces[geometry.face_count++];
        lf.vertex_start = vs;
        lf.vertex_count = vc;
        lf.material_id  = mid;
        lf.flags        = flags;
        lf.normal       = norm;
    }

    // --- Vertices (20 bytes each) ---
    if (vertex_count > static_cast<unsigned>(LevelGeometry::kMaxVertices)) {
        LVL_LOG("[lvl] vertex_count %lu exceeds kMaxVertices %d\n",
                static_cast<unsigned long>(vertex_count), LevelGeometry::kMaxVertices);
        fclose(f);
        return false;
    }
    fseek(f, static_cast<long>(off_vertices), SEEK_SET);
    geometry.vertex_count = 0;
    for (uint32_t i = 0; i < vertex_count; ++i) {
        LevelVertex& lv = geometry.vertices[geometry.vertex_count++];
        lv.pos = ReadVec3(f);
        lv.u   = ReadF32(f);
        lv.v   = ReadF32(f);
    }

    // --- Entities (24 bytes each) ---
    if (entity_count > static_cast<uint32_t>(Room::kMaxSpawns + 1)) {
        LVL_LOG("[lvl] entity_count %lu large; clamping spawns to kMaxSpawns\n",
                static_cast<unsigned long>(entity_count));
    }
    fseek(f, static_cast<long>(off_entities), SEEK_SET);
    room.spawn_count = 0;
    for (uint32_t i = 0; i < entity_count; ++i) {
        const uint16_t classname_id = ReadU16(f);
        ReadU16(f); // reserved
        const Vec3 position = ReadVec3(f);
        const uint32_t props_offset = ReadU32(f);
        const uint32_t props_len = ReadU32(f);
        const uint8_t* props_data = nullptr;
        (void)PropsRange(props_offset, props_len, &props_data);

        if (classname_id == kEntPlayerSpawn) {
            room.player_start = position;
            room.checkpoint   = position;
        } else if (classname_id == kEntCassette) {
            room.cassette = position;
            room.has_cassette = true;
        } else if (room.spawn_count < Room::kMaxSpawns) {
            room.spawns[room.spawn_count].position       = position;
            room.spawns[room.spawn_count].placeholder_id = classname_id;
            ++room.spawn_count;
        }
    }

    fclose(f);

    LVL_LOG("[lvl] loaded %s: colliders=%d faces=%d vertices=%d entities=%lu\n",
            path, room.collider_count, geometry.face_count,
            geometry.vertex_count, static_cast<unsigned long>(entity_count));

    // Try loading the .colmesh sidecar (same path, .lvl → .colmesh).
    {
        char colmesh_path[256];
        strncpy(colmesh_path, path, sizeof(colmesh_path) - 1);
        colmesh_path[sizeof(colmesh_path) - 1] = '\0';
        const size_t len = strlen(colmesh_path);
        if (len >= 4 && len <= sizeof(colmesh_path) - 9 &&
            colmesh_path[len-4] == '.' &&
            colmesh_path[len-3] == 'l' &&
            colmesh_path[len-2] == 'v' &&
            colmesh_path[len-1] == 'l') {
            strcpy(colmesh_path + len - 4, ".colmesh");
            physics::CollMesh* cm = physics::LoadCollMesh(colmesh_path);
            if (cm) {
                room.coll_mesh = cm;
                LVL_LOG("[lvl] colmesh loaded: %s (%lu tris)\n",
                        colmesh_path, (unsigned long)cm->header->triangle_count);
            }
        }
    }

    return true;
}

}  // namespace madeline_cube
