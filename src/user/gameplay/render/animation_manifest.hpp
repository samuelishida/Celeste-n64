#pragma once

#include <cstdint>

namespace madeline_cube {

struct AnimationClipRecord {
    char name[40] = {};
    char rom_stream_path[96] = {};
    bool loop = false;
};

class AnimationManifest {
public:
    static constexpr int kMaxClips = 16;

    bool Load(const char* rom_path);
    const AnimationClipRecord* Find(const char* name) const;
    int Count() const { return count_; }
    const AnimationClipRecord& At(int index) const { return clips_[index]; }

private:
    AnimationClipRecord clips_[kMaxClips] = {};
    int count_ = 0;
};

}  // namespace madeline_cube
