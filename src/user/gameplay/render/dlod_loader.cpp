#include "gameplay/render/dlod_loader.hpp"

#include <cstdio>
#include <cstring>

#include <libdragon.h>

#include "gameplay/render/lvl_room_renderer.hpp"
#include "gameplay/world/mappack_loader.hpp"  // V2RoomSpec::kPathLen

namespace madeline_cube {

namespace {

// Localize a "rom:/lvl/<pack>/<chunk>_distant.dlod" path to a filesystem path
// "<build_dir>/<chunk>_distant.dlod" for host-side loading. On device the
// rom:/ path is used as-is.
const char* LocalizePath(const char* rom_path, const char* build_dir) {
    if (!build_dir || build_dir[0] == '\0') return rom_path;
    static thread_local char local[256];
    const char* slash = std::strrchr(rom_path, '/');
    const char* fname = slash ? slash + 1 : rom_path;
    std::snprintf(local, sizeof(local), "%s/%s", build_dir, fname);
    return local;
}

// Read a whole file into a heap buffer (DFS-backed stdio, same as
// `LvlRoomRenderer::Load`). Returns the buffer (caller frees) or null on
// failure; `out_size` receives the byte count.
void* ReadWholeFile(const char* path, int* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return nullptr; }
    void* buf = malloc(static_cast<size_t>(len));
    if (!buf) { fclose(f); return nullptr; }
    const size_t got = fread(buf, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) { free(buf); return nullptr; }
    if (out_size) *out_size = static_cast<int>(len);
    return buf;
}

}  // namespace

int LoadDistantCellDlodAll(const char* pack_dir, const char* chunk,
                           float pos_scale, const char* build_dir,
                           LvlRoomRenderer* out[4], Vec3* out_shared_origin) {
    if (!out) return 0;
    for (int d = 0; d < 4; ++d) out[d] = nullptr;

    // Build the rom:/ path: rom:/lvl/<pack>/<chunk>_distant.dlod.
    char rom_path[V2RoomSpec::kPathLen + 16] = {};
    std::snprintf(rom_path, sizeof(rom_path), "rom:/lvl/%s/%s_distant.dlod",
                  pack_dir, chunk);

    const char* path = LocalizePath(rom_path, build_dir);
    int size = 0;
    void* data = ReadWholeFile(path, &size);
    if (!data) return 0;  // no .dlod — no distant geometry (Inc 5 removed the .lvl path)
    DlodMesh mesh;
    const int dirs = ParseDlod(static_cast<const uint8_t*>(data), size, &mesh);
    if (dirs <= 0) { free(data); return 0; }
    // Inc 3 / D2: the DLOD header origin is the SHARED map-center origin (the
    // source of truth for packing) — the caller's per-cell render origin is
    // NOT used, so the distant pass draws under one shared matrix.
    const Vec3 shared_origin = {mesh.origin[0], mesh.origin[1], mesh.origin[2]};
    if (out_shared_origin) *out_shared_origin = shared_origin;
    int loaded = 0;
    if (dirs == 1) {
        // Single-direction .dlod: load direction 0 into slot 0 ONLY, slots
        // 1..3 null (Inc 3 / D2 — stop sharing the pointer; the draw fallback
        // `meshes[d] -> meshes[0]` handles null slots). This removes the
        // shared-pointer path that caused the Inc 2 double-free; the
        // `FreeEntries` dedupe stays as defense-in-depth.
        LvlRoomRenderer* m = new LvlRoomRenderer();
        // streaming-memory-opt Inc 3: distant cells allocate ZERO RSPQ blocks —
        // set no-block mode BEFORE LoadFromDlod so its BuildRunsAndBlock skips
        // the block capture. The cell then draws via DrawRunsDirect.
        m->SetNoBlockMode();
        if (m->LoadFromDlod(mesh, 0, shared_origin, pos_scale)) {
            out[0] = m;
            loaded = 1;
        } else {
            delete m;
        }
    } else {
        // Multi-direction .dlod: load each direction into its slot.
        for (int d = 0; d < dirs && d < 4; ++d) {
            if (mesh.dirs[d].face_count <= 0) continue;  // empty dir
            LvlRoomRenderer* m = new LvlRoomRenderer();
            // streaming-memory-opt Inc 3: no-block mode (see single-dir branch).
            m->SetNoBlockMode();
            if (m->LoadFromDlod(mesh, d, shared_origin, pos_scale)) {
                out[d] = m;
                ++loaded;
            } else {
                delete m;
            }
        }
    }
    free(data);
    return loaded;
}

}  // namespace madeline_cube
