#pragma once

#include <cmath>
#include "gameplay/player/animation_state.hpp"

namespace madeline_cube {

// Mirrors OG SkinnedModel's retained tracks, independently of the renderer.
class PlayerAnimation {
public:
    static constexpr int kClipCount = 7;
    struct Track {
        PlayerAnimState state = PlayerAnimState::Idle;
        float time = 0.0f;
        float duration = 1.0f;
        float blend = 0.0f;
        float blend_duration = 0.01f;
        bool loops = true;
    };
    struct SampleTrack {
        PlayerAnimState state;
        float time;
        float weight;
    };
    float rate = 1.0f;

    void SetDuration(PlayerAnimState state, float duration, bool loops = true) {
        const int index = static_cast<int>(state);
        if (index < 0 || index >= kClipCount) return;
        durations_[index] = std::isfinite(duration) && duration > 0 ? duration : 1.0f;
        loops_[index] = loops;
    }
    static float BlendDuration(PlayerAnimState a, PlayerAnimState b) {
        if (a == b) return 0.01f;
        if (a == PlayerAnimState::Idle || b == PlayerAnimState::Idle) {
            const auto other = a == PlayerAnimState::Idle ? b : a;
            if (other == PlayerAnimState::Dash) return 0.05f;
            if (other == PlayerAnimState::Run) return 0.2f;
        }
        if ((a == PlayerAnimState::Run && b == PlayerAnimState::Skid) ||
            (b == PlayerAnimState::Run && a == PlayerAnimState::Skid)) return 0.125f;
        return 0.01f;
    }
    void Play(PlayerAnimState state, bool restart = false) {
        const int index = static_cast<int>(state);
        if (index < 0 || index >= kClipCount) return;
        Track selected;
        selected.state = state;
        for (int i = 0; i < count_; ++i) {
            if (tracks_[i].state != state) continue;
            selected = tracks_[i];
            for (int j = i; j + 1 < count_; ++j) tracks_[j] = tracks_[j + 1];
            --count_;
            break;
        }
        selected.duration = durations_[index];
        selected.loops = loops_[index];
        if (count_ == 0) selected.blend = 1.0f;
        if (restart || selected.blend <= 0) selected.time = 0;
        selected.blend_duration = count_ > 0
            ? BlendDuration(state, tracks_[count_ - 1].state) : 0.01f;
        tracks_[count_++] = selected;
    }
    void Clear() { count_ = 0; }
    void Advance(float dt) {
        if (!std::isfinite(dt) || dt <= 0 || count_ == 0) return;
        const float speed = std::isfinite(rate) && rate >= 0 ? rate : 1.0f;
        const float advance = speed * dt;
        if (!std::isfinite(advance)) return;
        const float blend_duration = tracks_[count_ - 1].blend_duration;
        for (int i = 0; i < count_; ++i) {
            auto& track = tracks_[i];
            const float next_time = track.time + advance;
            if (!std::isfinite(next_time)) continue;
            track.time = next_time;
            if (track.time >= track.duration) {
                if (track.loops) track.time -= track.duration;
                else track.time = track.duration;
            }
            const float target = i == count_ - 1 ? 1.0f : 0.0f;
            const float step = dt / blend_duration;
            track.blend = target > track.blend
                ? std::fmin(target, track.blend + step)
                : std::fmax(target, track.blend - step);
        }
    }
    int Samples(SampleTrack* output, int capacity) const {
        int result = 0;
        for (int i = count_ - 1; i >= 0 && result < capacity; --i) {
            if (tracks_[i].blend <= 0) continue;
            // One-shot tracks are clamped to `duration` by Advance so they hold
            // the final pose; fmod(duration, duration) would snap them to t=0.
            const float sample_time = tracks_[i].loops
                ? std::fmod(tracks_[i].time, tracks_[i].duration)
                : std::fmin(tracks_[i].time, tracks_[i].duration);
            output[result++] = {tracks_[i].state, sample_time,
                                1.0f - tracks_[i].blend};
        }
        return result;
    }
    int Count() const { return count_; }
    const Track& At(int index) const { return tracks_[index]; }
    PlayerAnimState Current() const {
        return count_ > 0 ? tracks_[count_ - 1].state : PlayerAnimState::Idle;
    }
private:
    Track tracks_[kClipCount] = {};
    // Source player.glb track durations, in enum order. Runtime conversion may
    // replace these with its stream duration before the first simulation tick.
    float durations_[kClipCount] = {5.833333333f, 0.666666667f, 1.041666667f,
        0.625f, 3.333333333f, 1.083333333f, 0.833333333f};
    bool loops_[kClipCount] = {true, true, true, false, true, true, true};
    int count_ = 0;
};

}  // namespace madeline_cube
