// Host test for the material manifest (Inc 5). Asserts the baked map-pack's
// `.manifest` (one material name per line, in bake order) lists materials in
// the SAME order as the per-cell `lvl.strings` for every cell in the pack.
// This is the index-stability contract: `LvlRoomRenderer::material_color()`
// and `MaterialCatalog::Load` both use material id = index into the string
// table / manifest, so a mismatch shifts the texture<->material mapping.
//
// Also asserts the `TB_empty` null-slot reservation is preserved (a material
// that must not draw resolves to a null sprite, never a shifted index).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/material_catalog_test.cpp \
//     src/user/gameplay/world/mappack_loader.cpp \
//     -o /tmp/material_catalog_test
// Run (after baking the fixture):
//   /tmp/material_catalog_test /tmp/inc4-build/staging

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gameplay/render/lvl_strings.hpp"
#include "gameplay/world/mappack_loader.hpp"

using namespace madeline_cube;

int main(int argc, char** argv) {
    const char* staging = argc > 1 ? argv[1] : "/tmp/inc4-build/staging";

    // Load the map-pack manifest so we know the per-cell lvl_path values.
    std::string mappack_path = std::string(staging) + "/forsyken-city.mappack";
    MapSpecV2 spec;
    assert(LoadMapPackV2(mappack_path.c_str(), spec));
    assert(spec.room_count > 0);

    // Read the per-pack material manifest (one name per line, bake order).
    std::string manifest_path = std::string(staging) + "/forsyken-city.manifest";
    FILE* mf = std::fopen(manifest_path.c_str(), "r");
    assert(mf && "manifest file must exist");
    std::vector<std::string> manifest_materials;
    char line[64];
    while (std::fgets(line, sizeof(line), mf)) {
        size_t len = std::strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        manifest_materials.push_back(std::string(line, len));
    }
    std::fclose(mf);
    assert(!manifest_materials.empty() && "manifest has at least one material");

    printf("manifest has %zu materials\n", manifest_materials.size());

    // For every cell, assert its lvl.strings match the manifest prefix (a
    // cell only references a subset of materials, in bake order).
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& r = spec.rooms[i];
        if (r.id[0] == '\0') continue;

        // Localize the rom:/ lvl_path to the staging dir.
        const char* slash = std::strrchr(r.lvl_path, '/');
        std::string fname = slash ? slash + 1 : r.lvl_path;
        std::string lvl_path = std::string(staging) + "/" + fname;

        char cell_strings[64][64] = {};
        const int n = LvlStringReader::Read(lvl_path.c_str(), cell_strings, 64);
        assert(n >= 0 && "cell LVL string table readable");
        for (int k = 0; k < n; ++k) {
            assert(k < (int)manifest_materials.size() &&
                   "cell string index within manifest");
            if (std::strncmp(cell_strings[k], manifest_materials[k].c_str(),
                             sizeof(cell_strings[0])) != 0) {
                std::fprintf(stderr,
                             "FAIL: cell %s string[%d]='%s' != manifest[%d]='%s'\n",
                             r.id, k, cell_strings[k], k,
                             manifest_materials[k].c_str());
                return 1;
            }
        }
    }
    printf("PASS: every cell's lvl.strings matches the manifest order\n");

    // TB_empty null-slot reservation: a manifest material named "TB_empty"
    // must exist (reserved at the end, never shifting solid-material indices).
    bool found_tb_empty = false;
    for (size_t i = 0; i < manifest_materials.size(); ++i) {
        if (manifest_materials[i] == "TB_empty") {
            found_tb_empty = true;
            break;
        }
    }
    assert(found_tb_empty && "manifest preserves the TB_empty null slot");
    printf("PASS: TB_empty null slot preserved in manifest\n");

    printf("ALL PASS\n");
    return 0;
}
