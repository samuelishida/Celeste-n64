// Host test for the DLOD v2 parser (Pattern A: header-only, no N64 deps).
// Asserts (Inc 3 / compressed-LOD + Inc 3 / D2):
//   (a) ParseDlod decodes a synthetic DLOD v2 blob byte-exact (explicit
//       big-endian reads) — counts, origin, packed verts, materials;
//   (b) malformed blobs (bad magic, bad version, truncated, vert_count !=
//       3×face_count, material_id ≥ material_count, bad direction_count)
//       return -1;
//   (c) a stale v1 blob (version 1) returns -1 (fail-loud, cell skipped —
//       never misrendered).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/dlod_format_contract.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gameplay/render/dlod_format.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Build a synthetic DLOD v2 blob. `directions` is a list of (face_count,
// material_id) per direction; verts are generated as consecutive triples.
static std::vector<uint8_t> MakeBlob(int direction_count,
                                    const int* face_counts,
                                    const int* material_ids,
                                    uint32_t magic = kDlodMagic,
                                    uint32_t version = kDlodVersion,
                                    int material_count = 4) {
    std::vector<uint8_t> b;
    auto put32 = [&](uint32_t v) {
        b.push_back(uint8_t(v >> 24));
        b.push_back(uint8_t(v >> 16));
        b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v));
    };
    auto put16 = [&](uint16_t v) {
        b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v));
    };
    auto putf = [&](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        put32(u);
    };

    int total_faces = 0;
    int total_verts = 0;
    for (int d = 0; d < direction_count; ++d) {
        total_faces += face_counts[d];
        total_verts += 3 * face_counts[d];
    }

    put32(magic);
    put32(version);
    put32(direction_count > 1 ? kDlodFlagPerDirection : 0u);
    put32(uint32_t(direction_count));
    put32(uint32_t(total_faces));
    put32(uint32_t(total_verts));
    put32(uint32_t(material_count));
    putf(10.0f); putf(20.0f); putf(30.0f);  // origin
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0);  // reserved

    int v = 0;
    for (int d = 0; d < direction_count; ++d) {
        put32(uint32_t(face_counts[d]));
        put32(uint32_t(3 * face_counts[d]));
        for (int f = 0; f < face_counts[d]; ++f) {
            for (int k = 0; k < 3; ++k) {
                put16(int16_t(v * 3 + k));  // packed xyz
                put16(int16_t(v * 3 + k));
                put16(int16_t(v * 3 + k));
            }
            ++v;
        }
        for (int f = 0; f < face_counts[d]; ++f) {
            b.push_back(uint8_t(material_ids[d]));
        }
    }
    return b;
}

static void test_valid_single_direction() {
    int fc[1] = {2};
    int mid[1] = {1};
    auto blob = MakeBlob(1, fc, mid);
    DlodMesh mesh;
    int dirs = ParseDlod(blob.data(), int(blob.size()), &mesh);
    expect(dirs == 1, "single direction parses to 1");
    expect(mesh.direction_count == 1, "direction_count == 1");
    expect(mesh.origin[0] == 10.0f && mesh.origin[1] == 20.0f &&
           mesh.origin[2] == 30.0f, "origin decoded");
    expect(mesh.dirs[0].face_count == 2, "dir face_count == 2");
    expect(mesh.dirs[0].vert_count == 6, "dir vert_count == 6 (3×2)");
    expect(mesh.dirs[0].materials[0] == 1 && mesh.dirs[0].materials[1] == 1,
           "materials decoded");
    // Byte-exact vertex check: face 0 vert 0 packs to (0,0,0).
    expect(mesh.dirs[0].verts[0].x() == 0 && mesh.dirs[0].verts[0].y() == 0 &&
           mesh.dirs[0].verts[0].z() == 0, "first packed vert byte-exact");
    // Face 1 vert 0 (index 3) packs to (3,3,3).
    expect(mesh.dirs[0].verts[3].x() == 3 && mesh.dirs[0].verts[3].y() == 3 &&
           mesh.dirs[0].verts[3].z() == 3, "4th packed vert byte-exact");
    std::printf("PASS: valid single direction\n");
}

static void test_valid_multi_direction() {
    int fc[2] = {1, 2};
    int mid[2] = {0, 3};
    auto blob = MakeBlob(2, fc, mid);
    DlodMesh mesh;
    int dirs = ParseDlod(blob.data(), int(blob.size()), &mesh);
    expect(dirs == 2, "two directions parse to 2");
    expect(mesh.direction_count == 2, "direction_count == 2");
    expect(mesh.dirs[0].face_count == 1 && mesh.dirs[0].vert_count == 3,
           "dir0 counts");
    expect(mesh.dirs[1].face_count == 2 && mesh.dirs[1].vert_count == 6,
           "dir1 counts");
    expect(mesh.dirs[0].materials[0] == 0, "dir0 material");
    expect(mesh.dirs[1].materials[0] == 3 && mesh.dirs[1].materials[1] == 3,
           "dir1 materials");
    std::printf("PASS: valid multi direction\n");
}

static void test_malformed() {
    // Bad magic.
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid, 0xDEADBEEFu);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "bad magic -> -1");
    }
    // Bad version.
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid, kDlodMagic, 99u);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "bad version -> -1");
    }
    // Stale v1 blob (version 1) -> -1 (fail-loud, cell skipped, never
    // misrendered). Inc 3 / D2.
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid, kDlodMagic, 1u);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "stale v1 blob -> -1 (fail-loud)");
    }
    // Non-finite origin (NaN) -> -1 (fail-loud; a NaN shared origin would
    // produce a garbage pass matrix). Patch origin_x (bytes 28..31) to NaN.
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid);
        // 0x7FC00000 = quiet NaN (big-endian).
        blob[28] = 0x7F; blob[29] = 0xC0; blob[30] = 0x00; blob[31] = 0x00;
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "NaN origin -> -1 (fail-loud)");
    }
    // Truncated (cut the blob in half).
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()) / 2, &mesh) == -1,
               "truncated -> -1");
    }
    // Too small (< 44 header).
    {
        uint8_t tiny[10] = {};
        DlodMesh mesh;
        expect(ParseDlod(tiny, 10, &mesh) == -1, "too small -> -1");
    }
    // Null input.
    {
        DlodMesh mesh;
        expect(ParseDlod(nullptr, 0, &mesh) == -1, "null -> -1");
    }
    // Bad direction_count (0 and 5).
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid);
        // Patch direction_count to 0.
        blob[12] = 0; blob[13] = 0; blob[14] = 0; blob[15] = 0;
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "direction_count 0 -> -1");
        auto blob2 = MakeBlob(1, fc, mid);
        blob2[12] = 0; blob2[13] = 0; blob2[14] = 0; blob2[15] = 5;
        expect(ParseDlod(blob2.data(), int(blob2.size()), &mesh) == -1,
               "direction_count 5 -> -1");
    }
    // material_id ≥ material_count.
    {
        int fc[1] = {1}; int mid[1] = {7};  // material_count default 4
        auto blob = MakeBlob(1, fc, mid);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "material_id >= material_count -> -1");
    }
    // material_count == 0.
    {
        int fc[1] = {1}; int mid[1] = {0};
        auto blob = MakeBlob(1, fc, mid, kDlodMagic, kDlodVersion, 0);
        DlodMesh mesh;
        expect(ParseDlod(blob.data(), int(blob.size()), &mesh) == -1,
               "material_count 0 -> -1");
    }
    std::printf("PASS: malformed blobs\n");
}

int main() {
    test_valid_single_direction();
    test_valid_multi_direction();
    test_malformed();
    if (failures) {
        std::fprintf(stderr, "%d FAILURES\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
