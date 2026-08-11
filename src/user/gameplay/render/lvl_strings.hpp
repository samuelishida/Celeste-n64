#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace madeline_cube {

// Host-safe LVL2 string-table reader (Inc 5). Reads the ordered material-name
// string table from a baked LVL2 file so host tests can verify a map-pack's
// `.manifest` lists materials in the SAME order as the per-cell `lvl.strings`
// (which `LvlRoomRenderer::material_color()` uses for material ids). No N64
// types — pure file I/O.
//
// LVL2 layout (big-endian):
//   0x00 magic "LVL2", 0x04 version u32, 0x08 collider u32, 0x0C face u32,
//   0x10 vertex u32, 0x14 entity u32, 0x18 string u32, 0x1C atmosphere(16),
//   0x2C off_strings u32, 0x30 off_colliders u32, 0x34 off_faces u32,
//   0x38 off_vertices u32, 0x3C off_entities u32, 0x40 off_props u32.
class LvlStringReader {
public:
    // Read the string table. `out` receives the material names (each a
    // NUL-terminated line); `out_capacity` bounds the count. Returns the
    // number of strings read, or -1 on a parse/open failure.
    static int Read(const char* lvl_path, char out[][64], int out_capacity) {
        FILE* f = std::fopen(lvl_path, "rb");
        if (!f) return -1;
        char magic[4];
        if (std::fread(magic, 1, 4, f) != 4 ||
            std::memcmp(magic, "LVL2", 4) != 0) {
            std::fclose(f);
            return -1;
        }
        uint32_t u32;
        if (!ReadU32(f, u32)) { std::fclose(f); return -1; }  // version
        if (!ReadU32(f, u32)) { std::fclose(f); return -1; }  // collider
        if (!ReadU32(f, u32)) { std::fclose(f); return -1; }  // face
        if (!ReadU32(f, u32)) { std::fclose(f); return -1; }  // vertex
        if (!ReadU32(f, u32)) { std::fclose(f); return -1; }  // entity
        uint32_t string_count;
        if (!ReadU32(f, string_count)) { std::fclose(f); return -1; }
        // Skip atmosphere (16 bytes: 8x uint16 from 0x1C..0x2B). off_strings
        // is the first of six u32 offsets at 0x2C.
        std::fseek(f, 16, SEEK_CUR);
        uint32_t off_strings;
        if (!ReadU32(f, off_strings)) { std::fclose(f); return -1; }

        if (string_count > (uint32_t)out_capacity) {
            string_count = (uint32_t)out_capacity;
        }
        if (std::fseek(f, (long)off_strings, SEEK_SET) != 0) {
            std::fclose(f);
            return -1;
        }
        // String table format: each string is a 1-byte length prefix followed
        // by that many bytes (NOT NUL-terminated).
        int n = 0;
        for (uint32_t i = 0; i < string_count; ++i) {
            int len = std::fgetc(f);
            if (len == EOF) break;
            if (len > 255) len = 255;
            if (len >= (int)sizeof(out[n])) len = (int)sizeof(out[n]) - 1;
            if (std::fread(out[n], 1, (size_t)len, f) != (size_t)len) break;
            out[n][len] = '\0';
            ++n;
        }
        std::fclose(f);
        return n;
    }

private:
    static bool ReadU32(FILE* f, uint32_t& out) {
        uint8_t b[4];
        if (std::fread(b, 1, 4, f) != 4) return false;
        out = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
              (uint32_t(b[2]) << 8) | uint32_t(b[3]);
        return true;
    }
};

}  // namespace madeline_cube
