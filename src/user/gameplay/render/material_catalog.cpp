#include "gameplay/render/material_catalog.hpp"

#include <cstdio>
#include <cstring>
#include <libdragon.h>
#include <sprite.h>

namespace madeline_cube {

bool MaterialCatalog::Load(const char* level_name) {
    char manifest_path[80];
    snprintf(manifest_path, sizeof(manifest_path), "rom:/lvl/%s.manifest", level_name);

    FILE* f = fopen(manifest_path, "r");
    if (!f) {
        debugf("[matcat] manifest open FAILED: %s\n", manifest_path);
        return false;
    }

    count_ = 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char sprite_path[80];
        snprintf(sprite_path, sizeof(sprite_path), "rom:/tex/%s.sprite", line);

        // Pre-check: sprite_load asserts on missing files, so probe first
        FILE* probe = fopen(sprite_path, "rb");
        if (!probe) {
            debugf("[matcat] material=%s MISSING (skipped)\n", line);
            continue;
        }
        fclose(probe);

        sprite_t* s = sprite_load(sprite_path);
        if (s) {
            if (count_ >= kMaxMaterials) {
                sprite_free(s);
                break;
            }
            sprites_[count_] = s;
            debugf("[matcat] material[%d]=%s OK\n", count_, line);
            ++count_;
        } else {
            debugf("[matcat] material=%s MISSING (skipped)\n", line);
        }
    }

    fclose(f);
    debugf("[matcat] loaded %d materials from %s\n", count_, manifest_path);
    return count_ > 0;
}

void MaterialCatalog::Unload() {
    for (int i = 0; i < count_; ++i) {
        if (sprites_[i]) {
            sprite_free(sprites_[i]);
            sprites_[i] = nullptr;
        }
    }
    count_ = 0;
}

sprite_t* MaterialCatalog::MaterialFor(uint16_t material_id) const {
    if (static_cast<int>(material_id) >= count_) return nullptr;
    return sprites_[material_id];
}

}  // namespace madeline_cube
