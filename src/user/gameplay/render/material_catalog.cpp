#include "gameplay/render/material_catalog.hpp"

#include <cstdio>
#include <cstring>
#include <libdragon.h>
#include <sprite.h>

#include "gameplay/render/material_meta_contract.hpp"

namespace madeline_cube {

namespace {

struct MatMetaRow {
    bool present = false;
    char name[64] = {};
    char rom_path[96] = {};
    char alpha[16] = {};
    char filter[16] = {};
    char wrap_s[16] = {};
    char wrap_t[16] = {};
};

bool LoadMatMeta(const char* path, MatMetaRow* rows, int max_rows) {
    FILE* f = fopen(path, "r");
    if (!f) return true;
    char line[256];
    int declared = 0;
    if (!fgets(line, sizeof(line), f) ||
        sscanf(line, "MATMETA\t1\t%d", &declared) != 1 ||
        declared < 0 || declared > max_rows) {
        fclose(f);
        return false;
    }
    for (int row = 0; row < declared; ++row) {
        if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char* fields[10] = {};
        fields[0] = line;
        for (int field = 1; field < 10; ++field) {
            char* tab = strchr(fields[field - 1], '\t');
            if (!tab) { fclose(f); return false; }
            *tab = '\0';
            fields[field] = tab + 1;
        }
        if (strchr(fields[9], '\t')) { fclose(f); return false; }  // exactly 10 fields
        // Fold the bounds guard into the copy so a field can never be copied
        // without its length check.
        auto copy_field = [](char* dst, size_t capacity, const char* src) {
            const size_t length = strlen(src);
            if (length >= capacity) return false;
            memcpy(dst, src, length + 1);
            return true;
        };
        int id = -1;
        if (sscanf(fields[0], "%d", &id) != 1 || id != row || !fields[1][0] ||
            !copy_field(rows[row].name, sizeof(rows[row].name), fields[1]) ||
            !copy_field(rows[row].rom_path, sizeof(rows[row].rom_path), fields[2]) ||
            !copy_field(rows[row].alpha, sizeof(rows[row].alpha), fields[4]) ||
            !copy_field(rows[row].filter, sizeof(rows[row].filter), fields[7]) ||
            !copy_field(rows[row].wrap_s, sizeof(rows[row].wrap_s), fields[5]) ||
            !copy_field(rows[row].wrap_t, sizeof(rows[row].wrap_t), fields[6]) ||
            !IsSupportedMaterialMetaFilter(fields[7], fields[2][0] != '\0') ||
            (fields[2][0] && strncmp(fields[2], "rom:/tex/", 9) != 0)) {
            fclose(f);
            return false;
        }
        rows[row].present = true;
    }
    fclose(f);
    return true;
}

}  // namespace

bool MaterialCatalog::Load(const char* level_name) {
    char manifest_path[96];
    snprintf(manifest_path, sizeof(manifest_path), "rom:/lvl/%s.manifest", level_name);
    FILE* f = fopen(manifest_path, "r");
    if (!f) {
        debugf("[matcat] manifest open FAILED: %s\n", manifest_path);
        return false;
    }
    count_ = 0;
    for (int i = 0; i < kMaxMaterials; ++i) {
        sprites_[i] = nullptr;
        profiles_[i] = MaterialProfile{};
        profiles_[i].repeat_s = true;
        profiles_[i].repeat_t = true;
    }
    MatMetaRow meta[kMaxMaterials] = {};
    char matmeta_path[96];
    snprintf(matmeta_path, sizeof(matmeta_path), "rom:/lvl/%s.matmeta", level_name);
    if (!LoadMatMeta(matmeta_path, meta, kMaxMaterials)) {
        debugf("[matcat] malformed matmeta: %s\n", matmeta_path);
        fclose(f);
        return false;
    }

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        if (count_ >= kMaxMaterials) { fclose(f); return false; }
        const int material_id = count_;
        if (meta[material_id].present && strcmp(meta[material_id].name, line) != 0) {
            debugf("[matcat] matmeta order mismatch id=%d name=%s meta=%s\n",
                   material_id, line, meta[material_id].name);
            fclose(f);
            return false;
        }
        if (strcmp(line, "TB_empty") == 0 ||
            (meta[material_id].present && meta[material_id].rom_path[0] == '\0')) {
            sprites_[count_] = nullptr;
            debugf("[matcat] material[%d]=%s NULL\n", count_, line);
            ++count_;
            continue;
        }

        char sprite_path[96];
        if (meta[material_id].present && meta[material_id].rom_path[0]) {
            snprintf(sprite_path, sizeof(sprite_path), "%s", meta[material_id].rom_path);
            profiles_[count_].alpha_blend = strcmp(meta[material_id].alpha, "opaque") != 0;
            profiles_[count_].bilinear_filter = strcmp(meta[material_id].filter, "bilinear") == 0;
            profiles_[count_].repeat_s = strcmp(meta[material_id].wrap_s, "repeat") == 0;
            profiles_[count_].repeat_t = strcmp(meta[material_id].wrap_t, "repeat") == 0;
        } else {
            snprintf(sprite_path, sizeof(sprite_path), "rom:/tex/%s.sprite", line);
        }
        FILE* probe = fopen(sprite_path, "rb");
        if (!probe) {
            debugf("[matcat] material[%d]=%s MISSING (reserved)\n", count_, line);
            ++count_;
            continue;
        }
        fclose(probe);
        sprite_t* s = sprite_load(sprite_path);
        if (s) {
            sprites_[count_] = s;
            profiles_[count_].has_texture = true;
            debugf("[matcat] material[%d]=%s OK lods=%d filter=%s\n",
                   count_, line, sprite_get_lod_count(s),
                   profiles_[count_].bilinear_filter ? "bilinear" : "point");
        } else {
            debugf("[matcat] material[%d]=%s MISSING (reserved)\n", count_, line);
        }
        ++count_;
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
        profiles_[i] = MaterialProfile{};
    }
    count_ = 0;
}

sprite_t* MaterialCatalog::MaterialFor(uint16_t material_id) const {
    if (static_cast<int>(material_id) >= count_) return nullptr;
    return sprites_[material_id];
}

const MaterialCatalog::MaterialProfile* MaterialCatalog::ProfileFor(uint16_t material_id) const {
    if (static_cast<int>(material_id) >= count_) return nullptr;
    return &profiles_[material_id];
}

}  // namespace madeline_cube
