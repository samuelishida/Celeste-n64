#include "gameplay/render/t3dm_room_renderer.hpp"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <libdragon.h>
#include <rdpq_sprite.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

namespace madeline_cube {

T3dmRoomRenderer::T3dmRoomRenderer() {}

T3dmRoomRenderer::~T3dmRoomRenderer() { Free(); }

bool T3dmRoomRenderer::Load(const char* dfs_path) {
    // Check if file exists before attempting to load
    int fd = open(dfs_path, O_RDONLY);
    if (fd < 0) {
        debugf("[room] file not found: %s\n", dfs_path);
        return false;
    }
    close(fd);

    model_ = t3d_model_load(dfs_path);
    if (!model_) {
        debugf("[room] load FAILED: %s\n", dfs_path);
        return false;
    }

    // Base-scale=1 keeps vertices in game units (no fixed-point scaling).
    // The map extent (~973 game units) fits in int16 without overflow at scale=1.
    // Precision is 1 game unit (~5 Quake units) — adequate for N64.
    identity_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 identity;
    t3d_mat4_identity(&identity);
    t3d_mat4_to_fixed(identity_fp_, &identity);

    debugf("[room] loaded: %s\n", dfs_path);
    return true;
}

void T3dmRoomRenderer::Free() {
    // Free cached sprites
    for (auto& [name, sprite] : sprites_) {
        if (sprite) {
            sprite_free(sprite);
        }
    }
    sprites_.clear();

    if (model_) {
        t3d_model_free(model_);
        model_ = nullptr;
    }
    if (identity_fp_) {
        free_uncached(identity_fp_);
        identity_fp_ = nullptr;
    }
}

void T3dmRoomRenderer::Draw() {
    if (!model_ || !identity_fp_) return;

    t3d_matrix_push(identity_fp_);

    // Use the custom draw API so our dynTextureCb runs per material.
    // The filterCb accepts everything (return true).
    // The dynTextureCb loads sprites from material name on first encounter.
    T3DModelDrawConf conf;
    conf.userData = this;
    conf.tileCb = nullptr;
    conf.filterCb = nullptr;
    conf.matrices = nullptr;
    conf.dynTextureCb = [](void* userData, const T3DMaterial* material,
                            rdpq_texparms_t* tileParams, rdpq_tile_t tile) {
        auto* self = static_cast<T3dmRoomRenderer*>(userData);
        const char* name = material->name;
        if (!name || name[0] == '\0') return;

        // Per-material coloring is handled by vertex colors baked
        // into the .t3dm (see bake_glb.py MATERIAL_COLORS).
        // This dynTextureCb handles sprite texture loading when sprites exist.

        // Check cache (uses const char* keys — no per-frame heap alloc)
        auto it = self->sprites_.find(name);
        if (it != self->sprites_.end()) {
            if (it->second) {
                rdpq_sprite_upload(tile, it->second, tileParams);
            }
            return;
        }

        // Probe for sprite file before loading (sprite_load asserts on missing)
        char sprite_path[80];
        snprintf(sprite_path, sizeof(sprite_path), "rom:/tex/%s.sprite", name);
        FILE* probe = fopen(sprite_path, "rb");
        if (!probe) {
            debugf("[room] sprite MISSING: %s\n", sprite_path);
            self->sprites_[name] = nullptr;
            return;
        }
        fclose(probe);

        sprite_t* s = sprite_load(sprite_path);
        self->sprites_[name] = s;
        if (s) {
            debugf("[room] sprite loaded: %s\n", sprite_path);
            rdpq_sprite_upload(tile, s, tileParams);
        }
    };

    t3d_model_draw_custom(model_, conf);
    t3d_matrix_pop(1);
}

}  // namespace madeline_cube
