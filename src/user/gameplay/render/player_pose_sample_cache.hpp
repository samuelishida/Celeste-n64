#pragma once

#include "gameplay/player/player_animation.hpp"

namespace madeline_cube {

// Cache sampled inputs, not FSM flags: interrupted blends and clip clocks
// are part of pose identity even when the current clip name stays the same.
class PlayerPoseSampleCache {
public:
    bool Matches(const PlayerAnimation::SampleTrack* samples, int count) const {
        if (count != count_) return false;
        for (int i = 0; i < count; ++i) {
            if (samples[i].state != samples_[i].state ||
                samples[i].time != samples_[i].time ||
                samples[i].weight != samples_[i].weight) return false;
        }
        return true;
    }

    void Store(const PlayerAnimation::SampleTrack* samples, int count) {
        if (count < 0 || count > PlayerAnimation::kClipCount) {
            Clear();
            return;
        }
        for (int i = 0; i < count; ++i) samples_[i] = samples[i];
        count_ = count;
    }

    void Clear() { count_ = -1; }

private:
    PlayerAnimation::SampleTrack samples_[PlayerAnimation::kClipCount]{};
    int count_ = -1;
};

} // namespace madeline_cube
