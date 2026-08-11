#include "gameplay/render/chunk_ring_renderer.hpp"

#include <cstdio>
#include <cstring>

#include "gameplay/render/lvl_room_renderer.hpp"

namespace madeline_cube {

bool ChunkRingRenderer::Load(const MapSpecV2& spec, const V2RoomSpec& center,
                             const char* build_dir) {
    Free();

    const V2RoomSpec* rooms[5] = {};
    const int count = ResolveRingRooms(spec, center, rooms);
    if (count == 0) return false;

    // Center is always rooms[0]; a missing center LVL is fatal.
    for (int i = 0; i < count; ++i) {
        const V2RoomSpec& rs = *rooms[i];
        LvlRoomRenderer* r = new LvlRoomRenderer();
        // Localize the rom:/ path to a filesystem path on host (build_dir),
        // else use the rom:/ path as-is on device.
        const char* path = rs.lvl_path;
        if (build_dir && build_dir[0] != '\0') {
            const char* slash = std::strrchr(rs.lvl_path, '/');
            const char* fname = slash ? slash + 1 : rs.lvl_path;
            // Rebuild a local path "<build_dir>/<fname>".
            static thread_local char local[256];
            std::snprintf(local, sizeof(local), "%s/%s", build_dir, fname);
            path = local;
        }
        if (!r->Load(path, rs.render_origin)) {
            delete r;
            if (i == 0) {
                // Center failure is fatal.
                loaded_count_ = 0;
                return false;
            }
            continue;  // neighbor failure is skipped (not fatal)
        }
        renderers_[loaded_count_++] = r;
    }
    return loaded_count_ > 0;
}

void ChunkRingRenderer::Draw() const {
    for (int i = 0; i < loaded_count_; ++i) {
        if (renderers_[i]) renderers_[i]->Draw();
    }
}

void ChunkRingRenderer::SetCameraPosition(const Vec3& camera_pos) {
    for (int i = 0; i < loaded_count_; ++i) {
        if (renderers_[i]) renderers_[i]->SetCameraPosition(camera_pos);
    }
}

void ChunkRingRenderer::Free() {
    for (int i = 0; i < loaded_count_; ++i) {
        if (renderers_[i]) {
            renderers_[i]->Free();
            delete renderers_[i];
            renderers_[i] = nullptr;
        }
    }
    loaded_count_ = 0;
}

}  // namespace madeline_cube
