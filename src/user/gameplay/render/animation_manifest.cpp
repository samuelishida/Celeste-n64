#include "gameplay/render/animation_manifest.hpp"

#include <cstdio>
#include <cstring>

// Guard the device include so this parser (like its mappack/level siblings) can
// also link in host tests; it only needs debugf.
#ifdef __mips__
#include <libdragon.h>
#define AM_LOG debugf
#else
#define AM_LOG(...) do {} while (0)
#endif

namespace madeline_cube {

namespace {
bool copy_field(char* dst, size_t capacity, const char* src) {
    const size_t length = strlen(src);
    if (length == 0 || length >= capacity || strchr(src, '\t') || strchr(src, '\n') || strchr(src, '\r')) return false;
    memcpy(dst, src, length + 1);
    return true;
}
}

bool AnimationManifest::Load(const char* rom_path) {
    FILE* f = fopen(rom_path, "r");
    if (!f) {
        AM_LOG("[animmap] open FAILED: %s\n", rom_path);
        return false;
    }
    char line[256];
    int declared = 0;
    if (!fgets(line, sizeof(line), f) ||
        sscanf(line, "ANIMMAP\t1\t%d", &declared) != 1 ||
        declared < 0 || declared > kMaxClips) {
        fclose(f);
        return false;
    }
    count_ = 0;
    for (int row = 0; row < declared; ++row) {
        if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }
        int len = static_cast<int>(strlen(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char* fields[3] = {};
        char* cursor = line;
        for (int field = 0; field < 3; ++field) {
            fields[field] = cursor;
            char* tab = strchr(cursor, '\t');
            if (field < 2) {
                if (!tab) { fclose(f); return false; }
                *tab = '\0';
                cursor = tab + 1;
            } else if (tab) {
                fclose(f);
                return false;
            }
        }
        AnimationClipRecord& clip = clips_[count_];
        if (!copy_field(clip.name, sizeof(clip.name), fields[0]) ||
            !copy_field(clip.rom_stream_path, sizeof(clip.rom_stream_path), fields[1]) ||
            strncmp(clip.rom_stream_path, "rom:/mdl/", 9) != 0 ||
            (strcmp(fields[2], "loop") != 0 && strcmp(fields[2], "once") != 0)) {
            fclose(f);
            return false;
        }
        clip.loop = strcmp(fields[2], "loop") == 0;
        for (int prior = 0; prior < count_; ++prior) {
            if (strcmp(clips_[prior].name, clip.name) == 0 ||
                strcmp(clips_[prior].rom_stream_path, clip.rom_stream_path) == 0) {
                fclose(f);
                return false;
            }
        }
        ++count_;
    }
    if (fgets(line, sizeof(line), f)) {
        int extra_len = static_cast<int>(strlen(line));
        while (extra_len > 0 && (line[extra_len - 1] == '\n' || line[extra_len - 1] == '\r')) {
            line[--extra_len] = '\0';
        }
        if (extra_len != 0) { fclose(f); return false; }
    }
    fclose(f);
    return count_ == declared;
}

const AnimationClipRecord* AnimationManifest::Find(const char* name) const {
    if (!name) return nullptr;
    for (int i = 0; i < count_; ++i) {
        if (strcmp(clips_[i].name, name) == 0) return &clips_[i];
    }
    return nullptr;
}

}  // namespace madeline_cube
