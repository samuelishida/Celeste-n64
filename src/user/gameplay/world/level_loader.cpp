#include "gameplay/world/level_loader.hpp"

#include <cstdio>
#include <cstring>

#ifdef __mips__
#include <libdragon.h>
#define LVL_LOG debugf
#else
#define LVL_LOG printf
#endif

namespace madeline_cube {

namespace {

// ENT_PLAYERSPAWN classname_id from entity_ids.py
static constexpr uint16_t kEntPlayerSpawn = 0;

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

static uint8_t ReadU8(FILE* f) {
    uint8_t b;
    fread(&b, 1, 1, f);
    return b;
}

// LVL format type: 0=Plane, 1=Box — runtime enum: Box=0, Plane=1
static ColliderType MapColliderType(uint8_t lvl_type) {
    return (lvl_type == 0) ? ColliderType::Plane : ColliderType::Box;
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
    if (magic[0] != 'L' || magic[1] != 'V' || magic[2] != 'L' || magic[3] != '1') {
        LVL_LOG("[lvl] bad magic in %s\n", path);
        fclose(f);
        return false;
    }

    const uint32_t version = ReadU32(f);
    if (version != 1) {
        LVL_LOG("[lvl] unsupported version %lu\n", version);
        fclose(f);
        return false;
    }

    const uint32_t collider_count = ReadU32(f);
    const uint32_t face_count     = ReadU32(f);
    const uint32_t vertex_count   = ReadU32(f);
    const uint32_t entity_count   = ReadU32(f);
    const uint32_t string_count   = ReadU32(f);

    // Discard skybox/music/ambience/snow fields (6 uint16 = 12 bytes)
    ReadU16(f); ReadU16(f); ReadU16(f); // skybox, music, ambience
    ReadU16(f);                          // snow_amount
    ReadS16(f); ReadS16(f); ReadS16(f); // snow_dir
    ReadU16(f);                          // reserved

    const uint32_t off_strings    = ReadU32(f);
    const uint32_t off_colliders  = ReadU32(f);
    const uint32_t off_faces      = ReadU32(f);
    const uint32_t off_vertices   = ReadU32(f);
    const uint32_t off_entities   = ReadU32(f);
    (void)ReadU32(f); // off_props_blob unused

    // --- Colliders (56 bytes each) ---
    if (collider_count > static_cast<unsigned>(Room::kMaxColliders)) {
        LVL_LOG("[lvl] collider_count %lu exceeds kMaxColliders %d\n",
                collider_count, Room::kMaxColliders);
        fclose(f);
        return false;
    }
    fseek(f, static_cast<long>(off_colliders), SEEK_SET);
    room.collider_count = 0;
    for (uint32_t i = 0; i < collider_count; ++i) {
        const uint8_t type      = ReadU8(f);
        const uint8_t solid     = ReadU8(f);
        const uint8_t has_orig  = ReadU8(f);
        ReadU8(f); // reserved
        const Vec3 bounds_min   = ReadVec3(f);
        const Vec3 bounds_max   = ReadVec3(f);
        const Vec3 normal       = ReadVec3(f);
        const Vec3 plane_origin = ReadVec3(f);
        const int16_t face_id   = ReadS16(f);
        const int16_t owner_id  = ReadS16(f);

        Collider& c = room.colliders[room.collider_count++];
        c.type             = MapColliderType(type);
        c.solid            = (solid != 0);
        c.has_plane_origin = (has_orig != 0);
        c.bounds           = {bounds_min, bounds_max};
        c.normal           = normal;
        c.plane_origin     = plane_origin;
        c.face_id          = face_id;
        c.owner_id         = owner_id;
        c.velocity         = {0.0f, 0.0f, 0.0f};
    }

    // --- Faces (24 bytes each) ---
    if (face_count > static_cast<unsigned>(LevelGeometry::kMaxFaces)) {
        LVL_LOG("[lvl] face_count %lu exceeds kMaxFaces %d\n",
                face_count, LevelGeometry::kMaxFaces);
        fclose(f);
        return false;
    }
    fseek(f, static_cast<long>(off_faces), SEEK_SET);
    geometry.face_count = 0;
    for (uint32_t i = 0; i < face_count; ++i) {
        const uint32_t vs = ReadU32(f);
        const uint32_t vc = ReadU32(f);
        const uint16_t mid = ReadU16(f);
        ReadS16(f); // reserved
        const Vec3 norm = ReadVec3(f);

        LevelFace& lf = geometry.faces[geometry.face_count++];
        lf.vertex_start = vs;
        lf.vertex_count = vc;
        lf.material_id  = mid;
        lf.normal       = norm;
    }

    // --- Vertices (20 bytes each) ---
    if (vertex_count > static_cast<unsigned>(LevelGeometry::kMaxVertices)) {
        LVL_LOG("[lvl] vertex_count %lu exceeds kMaxVertices %d\n",
                vertex_count, LevelGeometry::kMaxVertices);
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
                entity_count);
    }
    fseek(f, static_cast<long>(off_entities), SEEK_SET);
    room.spawn_count = 0;
    for (uint32_t i = 0; i < entity_count; ++i) {
        const uint16_t classname_id = ReadU16(f);
        ReadU16(f); // reserved
        const Vec3 position = ReadVec3(f);
        ReadU32(f); // props_offset
        ReadU32(f); // props_len

        if (classname_id == kEntPlayerSpawn) {
            room.player_start = position;
            room.checkpoint   = position;
        } else if (room.spawn_count < Room::kMaxSpawns) {
            room.spawns[room.spawn_count].position       = position;
            room.spawns[room.spawn_count].placeholder_id = classname_id;
            ++room.spawn_count;
        }
    }

    fclose(f);

    (void)off_strings;
    (void)string_count;

    LVL_LOG("[lvl] loaded %s: colliders=%d faces=%d vertices=%d entities=%lu\n",
            path, room.collider_count, geometry.face_count,
            geometry.vertex_count, entity_count);
    return true;
}

}  // namespace madeline_cube
