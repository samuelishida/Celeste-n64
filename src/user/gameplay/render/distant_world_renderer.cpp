#include "gameplay/render/distant_world_renderer.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include <rdpq.h>
#include <rdpq_mode.h>

#include "gameplay/render/lvl_room_renderer.hpp"

namespace madeline_cube {

namespace {

// Localize a "rom:/lvl/<pack>/<chunk>_distant.lvl" path to a filesystem path
// "<build_dir>/<chunk>_distant.lvl" for host-side loading. On device the
// rom:/ path is used as-is.
const char* LocalizePath(const char* rom_path, const char* build_dir) {
    if (!build_dir || build_dir[0] == '\0') return rom_path;
    static thread_local char local[256];
    const char* slash = std::strrchr(rom_path, '/');
    const char* fname = slash ? slash + 1 : rom_path;
    std::snprintf(local, sizeof(local), "%s/%s", build_dir, fname);
    return local;
}

}  // namespace

DistantWorldRenderer::~DistantWorldRenderer() {
    for (int i = 0; i < entry_count_; ++i) {
        DistantLodEntry& en = entries_[i];
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (en.meshes[d]) {
                en.meshes[d]->Free();
                delete en.meshes[d];
                en.meshes[d] = nullptr;
            }
        }
    }
    entry_count_ = 0;
}

bool DistantWorldRenderer::Load(const MapSpecV2& spec, const char* build_dir) {
    // Free any existing distant meshes.
    for (int i = 0; i < entry_count_; ++i) {
        DistantLodEntry& en = entries_[i];
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (en.meshes[d]) {
                en.meshes[d]->Free();
                delete en.meshes[d];
                en.meshes[d] = nullptr;
            }
        }
    }
    entry_count_ = 0;

    if (spec.room_count > 64) return false;  // entries_ cap
    for (int i = 0; i < spec.room_count; ++i) {
        const V2RoomSpec& rs = spec.rooms[i];
        if (rs.id[0] == '\0') continue;

        // Distant LVL path: <chunk>_distant.lvl (the bake emits these).
        // Build from the room's lvl_path by inserting "_distant" before ".lvl".
        char distant_path[V2RoomSpec::kPathLen + 16] = {};
        std::strncpy(distant_path, rs.lvl_path, sizeof(distant_path) - 1);
        char* dot = std::strrchr(distant_path, '.');
        if (dot) {
            // Shift the extension right by the length of "_distant".
            const char* ext = dot;
            const size_t ext_len = std::strlen(ext);
            std::memmove(dot + 9, dot, ext_len + 1);  // +9 for "_distant" + NUL
            std::memcpy(dot, "_distant", 8);
        }

        LvlRoomRenderer* mesh = new LvlRoomRenderer();
        const char* path = LocalizePath(distant_path, build_dir);
        if (!mesh->Load(path, rs.render_origin, kLodScale)) {
            delete mesh;
            // A cell with no distant LVL (no renderable geometry) is skipped.
            continue;
        }

        DistantLodEntry& en = entries_[entry_count_];
        en = DistantLodEntry{};
        en.lod_scale = kLodScale;
        en.priority = 1;
        en.cell_ix = rs.cell_ix;
        en.cell_iz = rs.cell_iz;
        en.origin = rs.render_origin;
        en.child_count = 0;
        // First version: all four directional slots share the same mesh.
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            en.meshes[d] = mesh;
        }
        ++entry_count_;
    }
    return entry_count_ > 0;
}

void DistantWorldRenderer::SetCameraPosition(const Vec3& camera_pos) {
    camera_pos_ = camera_pos;
    for (int i = 0; i < entry_count_; ++i) {
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (entries_[i].meshes[d]) {
                entries_[i].meshes[d]->SetCameraPosition(camera_pos);
            }
        }
    }
}

void DistantWorldRenderer::UpdateCamera(const Vec3& camera_pos,
                                        const CameraDesc&) {
    camera_pos_ = camera_pos;
}

void DistantWorldRenderer::SetFog(const FogParams& fog) {
    fog_ = fog;
}

void DistantWorldRenderer::Render(const CameraDesc& cam) {
    // Build the render list, culled + distance-ordered (arch.md §8).
    DistantRenderItem order[64];
    const int n = BuildDistantRenderList(camera_pos_, entries_, entry_count_,
                                         order, 64);

    // arch.md §7-8: Z off, draw farthest first, then restore Z.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Fog (Inc 6): configure the RDP fog mode + color/range for the distant
    // pass so the horizon fades into the atmosphere. Torn down after.
    if (fog_.enabled && ValidateFogRange(fog_)) {
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_set_fog_color((color_t){
            (uint8_t)fog_.color.x, (uint8_t)fog_.color.y,
            (uint8_t)fog_.color.z, 0xFF});
        t3d_fog_set_range(fog_.min, fog_.max);
        t3d_fog_set_enabled(true);
    }

    // Sort back-to-front: BuildDistantRenderList sets priority = distance², so
    // descending priority draws far cells first.
    std::sort(order, order + n,
              [](const DistantRenderItem& a, const DistantRenderItem& b) {
                  return a.priority > b.priority;
              });

    for (int i = 0; i < n; ++i) {
        const int e = order[i].cell_index;
        if (e < 0 || e >= entry_count_) continue;
        for (int d = 0; d < DistantLodEntry::kMaxDirMeshes; ++d) {
            if (entries_[e].meshes[d]) entries_[e].meshes[d]->Draw();
        }
    }

    // Tear down fog + Z before the near pass.
    if (fog_.enabled) {
        t3d_fog_set_enabled(false);
        rdpq_mode_fog(0);
    }
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);
    (void)cam;
}

}  // namespace madeline_cube
