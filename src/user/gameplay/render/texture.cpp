#include "gameplay/render/texture.hpp"

#include <libdragon.h>
#include <rdpq_sprite.h>

namespace madeline_cube {

bool SpriteTexture::Load(const char* dfs_path) {
    sprite_ = sprite_load(dfs_path);
    if (!sprite_) {
        debugf("[texture] load FAILED: %s\n", dfs_path);
        return false;
    }
    debugf("[texture] loaded: %s\n", dfs_path);
    return true;
}

void SpriteTexture::Free() {
    if (sprite_) {
        sprite_free(sprite_);
        sprite_ = nullptr;
    }
}

void SpriteTexture::DebugBlit(float x, float y) const {
    if (!sprite_) return;
    rdpq_sprite_blit(sprite_, x, y, NULL);
}

}  // namespace madeline_cube
