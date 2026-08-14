#pragma once

#include <cmath>
#include <cstdint>

namespace madeline_cube {

// ── DLOD v2 compact distant-LOD parser (Inc 3) ────────────────────────────
// Pure, zero-copy, host-safe (no N64 includes). Parses a DLOD v2 buffer into
// views that point into the caller's `data` (no copy/allocation). Big-endian
// fields are read through explicit byte-swap accessors so the Pattern-A host
// test is byte-exact on a little-endian host and the device (big-endian)
// reads the same bytes.
//
// DLOD v2 layout (big-endian):
//   header (44 B):
//     u32 magic   0x444C4F44 ("DLOD")
//     u32 version 2
//     u32 flags            (bit0 = per-direction)
//     u32 direction_count  (1, or 4 from Inc 4)
//     u32 face_count       (total across directions)
//     u32 vert_count       (total across directions; = 3 × face_count)
//     u32 material_count   (≤ manifest size)
//     f32 origin_x/y/z     (SHARED map-center origin, world — Inc 3 / D2)
//     u8  reserved[4]
//   per-direction section (direction_count ×):
//     u32 dir_face_count
//     u32 dir_vert_count       (= 3 × dir_face_count)
//     verts: dir_vert_count × s16 xyz     (packed (world - origin) * kLodScale;
//                                         consecutive triples — face i uses
//                                         verts[3i..3i+2])
//     materials: dir_face_count × u8 material_id  (index into the shared
//                                                 manifest)

// DLOD magic + version.
inline constexpr uint32_t kDlodMagic = 0x444C4F44u;  // "DLOD"
// Version 2 (Inc 3 / D2): the header `origin` is the SHARED map-center origin
// (all cells pack relative to it), not the per-cell render origin. The byte
// layout is otherwise unchanged. A stale v1 `.dlod` (per-cell origins) fails
// to parse (returns -1, cell skipped) instead of silently misrendering.
inline constexpr uint32_t kDlodVersion = 2u;
// Flags bit 0 = per-direction (direction_count > 1).
inline constexpr uint32_t kDlodFlagPerDirection = 0x00000001u;
// Max directions a DLOD can carry (matches DistantLodEntry::kMaxDirMeshes).
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
    // Bit-cast via memcpy (host-safe, no aliasing UB).
    __builtin_memcpy(&f, &u, sizeof(f));
    return f;
}

}  // namespace dlod_detail

// One packed distant vertex (s16 xyz, origin-relative at kLodScale). Stored as
// raw big-endian bytes; accessors byte-swap so the value is correct on both a
// little-endian host and the big-endian device. Zero-copy: the bytes point
// into the parsed buffer.
struct DlodVertex {
    uint8_t raw[6];  // big-endian x, y, z (each s16)

    int16_t x() const { return dlod_detail::ReadS16BE(raw + 0); }
    int16_t y() const { return dlod_detail::ReadS16BE(raw + 2); }
    int16_t z() const { return dlod_detail::ReadS16BE(raw + 4); }
};

// One direction's geometry (views point into the parsed buffer, zero-copy).
struct DlodDirection {
    const DlodVertex* verts = nullptr;   // dir_vert_count entries
    int vert_count = 0;                  // = 3 × face_count
    const uint8_t* materials = nullptr;  // face_count entries
    int face_count = 0;
};

// A parsed DLOD mesh (views point into the caller's buffer).
struct DlodMesh {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    int direction_count = 0;
    DlodDirection dirs[4];
};

// Parse a DLOD v2 buffer into `out` (views point into `data`, zero-copy).
// Returns the direction_count parsed, or -1 on malformed input (bad magic,
// version, truncated, vert_count != 3×face_count, material_id ≥
// material_count, direction_count out of range). Strict: the artifact is
// bake-produced, so strictness is safe. A stale v1 `.dlod` (version != 2)
// returns -1 so the cell is skipped (fail-loud, never misrendered).
inline int ParseDlod(const uint8_t* data, int size, DlodMesh* out) {
    if (!data || size < 44 || !out) return -1;

    const uint32_t magic = dlod_detail::ReadU32BE(data + 0);
    if (magic != kDlodMagic) return -1;
    const uint32_t version = dlod_detail::ReadU32BE(data + 4);
    if (version != kDlodVersion) return -1;
    /*const uint32_t flags =*/ dlod_detail::ReadU32BE(data + 8);
    const uint32_t direction_count = dlod_detail::ReadU32BE(data + 12);
    /*const uint32_t face_count =*/ dlod_detail::ReadU32BE(data + 16);
    /*const uint32_t vert_count =*/ dlod_detail::ReadU32BE(data + 20);
    const uint32_t material_count = dlod_detail::ReadU32BE(data + 24);
    if (direction_count < 1 || direction_count > kDlodMaxDirections) return -1;
    if (material_count == 0) return -1;

    out->origin[0] = dlod_detail::ReadF32BE(data + 28);
    out->origin[1] = dlod_detail::ReadF32BE(data + 32);
    out->origin[2] = dlod_detail::ReadF32BE(data + 36);
    // Reject non-finite origin floats: a NaN/Inf shared origin would flow into
    // the pass matrix (shared_origin - camera) and produce a garbage transform
    // for the whole distant pass. Fail-loud (cell skipped), never misrender.
    if (!std::isfinite(out->origin[0]) || !std::isfinite(out->origin[1]) ||
        !std::isfinite(out->origin[2])) {
        return -1;
    }
    out->direction_count = static_cast<int>(direction_count);

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
        // Validate every material id is in range.
        for (int f = 0; f < dir.face_count; ++f) {
            if (dir.materials[f] >= material_count) return -1;
        }
    }
    return static_cast<int>(direction_count);
}

}  // namespace madeline_cube
