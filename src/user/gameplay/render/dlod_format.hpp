#pragma once

#include <cstdint>

namespace madeline_cube {

// ── DLOD v3 compact distant-LOD parser ────────────────────────────────────
// Pure, zero-copy, host-safe (no N64 includes). Parses a DLOD v3 buffer into
// views that point into the caller's `data`. Big-endian fields are read
// through explicit byte-swap accessors so a host test is byte-exact on a
// little-endian host and the device (big-endian) reads the same bytes.
//
// DLOD v3 layout (big-endian; see tools/writers/dlod_writer.py):
//   header (44 B):
//     u32 magic   0x444C4F44 ("DLOD")
//     u32 version 3
//     u32 flags            (bit0 = per-direction, bit1 = has vertex colors)
//     u32 direction_count  (1, or 4)
//     u32 face_count       (total across directions)
//     u32 vert_count       (total across directions; = 3 × face_count)
//     u32 material_count   (≤ manifest size)
//     f32 origin_x/y/z     (SHARED map-center origin, world)
//     u8  reserved[4]
//   per-direction section (direction_count ×):
//     u32 dir_face_count
//     u32 dir_vert_count       (= 3 × dir_face_count)
//     verts: dir_vert_count × s16 xyz   (packed (world - origin) * kLodScale;
//                                         consecutive triples — face i uses
//                                         verts[3i..3i+2])
//     materials: dir_face_count × u8 material_id
//     u8 color_flag            (1 = per-face colors follow, else 0)
//     colors: dir_face_count × u8     (present iff color_flag == 1)

inline constexpr uint32_t kDlodMagic = 0x444C4F44u;  // "DLOD"
inline constexpr uint32_t kDlodVersion = 3u;
inline constexpr uint32_t kDlodFlagPerDirection = 0x00000001u;
inline constexpr uint32_t kDlodFlagHasColors = 0x00000002u;
inline constexpr int kDlodMaxDirections = 4;

namespace dlod_detail {

inline uint32_t ReadU32BE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint16_t ReadU16BE(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}
inline int16_t ReadS16BE(const uint8_t* p) {
    return static_cast<int16_t>(ReadU16BE(p));
}
inline float ReadF32BE(const uint8_t* p) {
    const uint32_t u = ReadU32BE(p);
    float f;
    __builtin_memcpy(&f, &u, sizeof(f));
    return f;
}

}  // namespace dlod_detail

// One packed distant vertex (s16 xyz, map-center-origin-relative at kLodScale).
struct DlodVertex {
    uint8_t raw[6];
    int16_t x() const { return dlod_detail::ReadS16BE(raw + 0); }
    int16_t y() const { return dlod_detail::ReadS16BE(raw + 2); }
    int16_t z() const { return dlod_detail::ReadS16BE(raw + 4); }
};

// One direction's geometry (views point into the parsed buffer, zero-copy).
struct DlodDirection {
    const DlodVertex* verts = nullptr;   // dir_vert_count entries (= 3×faces)
    int vert_count = 0;
    const uint8_t* materials = nullptr;  // face_count entries
    int face_count = 0;
    const uint8_t* colors = nullptr;     // face_count entries (null if none)
    bool has_colors = false;
};

// A parsed DLOD v3 mesh.
struct DlodMesh {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    int direction_count = 0;
    DlodDirection dirs[4];
    uint32_t material_count = 0;
    uint32_t flags = 0;
};

// Parse a DLOD buffer (v2 or v3) into `out`. Returns direction_count, or -1 on
// malformed input. The parser reads version; it accepts 2 and 3 (v3 adds the
// per-direction color channel; v2 has color_flag absent — the layout is
// otherwise identical, so a v2 file's last face_count bytes are its materials
// and we just treat color_flag as 0). Safe strict.
inline int ParseDlod(const uint8_t* data, int size, DlodMesh* out) {
    if (!data || size < 44 || !out) return -1;

    const uint32_t magic = dlod_detail::ReadU32BE(data + 0);
    if (magic != kDlodMagic) return -1;
    const uint32_t version = dlod_detail::ReadU32BE(data + 4);
    if (version != 2u && version != 3u) return -1;
    const uint32_t flags = dlod_detail::ReadU32BE(data + 8);
    const uint32_t direction_count = dlod_detail::ReadU32BE(data + 12);
    /*const uint32_t face_count =*/ dlod_detail::ReadU32BE(data + 16);
    /*const uint32_t vert_count =*/ dlod_detail::ReadU32BE(data + 20);
    const uint32_t material_count = dlod_detail::ReadU32BE(data + 24);
    if (direction_count < 1 || direction_count > kDlodMaxDirections) return -1;
    if (material_count == 0) return -1;

    out->origin[0] = dlod_detail::ReadF32BE(data + 28);
    out->origin[1] = dlod_detail::ReadF32BE(data + 32);
    out->origin[2] = dlod_detail::ReadF32BE(data + 36);
    out->direction_count = static_cast<int>(direction_count);
    out->material_count = material_count;
    out->flags = flags;

    int offset = 44;
    for (uint32_t d = 0; d < direction_count; ++d) {
        if (offset + 8 > size) return -1;
        const uint32_t dir_face_count = dlod_detail::ReadU32BE(data + offset);
        const uint32_t dir_vert_count = dlod_detail::ReadU32BE(data + offset + 4);
        offset += 8;
        if (dir_vert_count != 3 * dir_face_count) return -1;
        const uint64_t vert_bytes = uint64_t(dir_vert_count) * 6u;  // 3 × s16
        const uint64_t mat_bytes = uint64_t(dir_face_count) * 1u;
        if (uint64_t(offset) + vert_bytes + mat_bytes > uint64_t(size)) return -1;

        DlodDirection& dir = out->dirs[d];
        dir.verts = reinterpret_cast<const DlodVertex*>(data + offset);
        dir.vert_count = static_cast<int>(dir_vert_count);
        offset += static_cast<int>(vert_bytes);
        dir.materials = data + offset;
        dir.face_count = static_cast<int>(dir_face_count);
        offset += static_cast<int>(mat_bytes);

        // Per-direction color channel (v3). v2 has no color_flag byte — the
        // header flags bit1 tells us whether colors exist; when bit1 is clear
        // the per-direction color_flag byte is absent and the layout ends here.
        bool has_colors = false;
        if (version >= 3u) {
            if (offset >= size) return -1;
            const uint8_t color_flag = data[offset];
            offset += 1;
            has_colors = (color_flag != 0);
            if (has_colors) {
                if (uint64_t(offset) + uint64_t(dir_face_count) > uint64_t(size))
                    return -1;
                dir.colors = data + offset;
                dir.has_colors = true;
                offset += dir_face_count;
            }
        }
        // Validate every material id is in range.
        for (int f = 0; f < dir.face_count; ++f) {
            if (dir.materials[f] >= material_count) return -1;
        }
    }
    return static_cast<int>(direction_count);
}

}  // namespace madeline_cube
