#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gameplay/player/player_animation.hpp"

using namespace madeline_cube;

namespace {

constexpr int kClipCount = PlayerAnimation::kClipCount;
constexpr float kEpsilon = 1.0e-6f;

// Independent test-side transcription of OG SkinnedModel's retained track
// list. Vector erase/insert deliberately differs from production's fixed array.
class SourceAnimationOracle {
public:
    struct Track {
        PlayerAnimState state;
        float time = 0.0f;
        float duration = 1.0f;
        float blend = 0.0f;
        float blend_duration = 0.01f;
        bool loops = true;
    };

    SourceAnimationOracle() {
        constexpr float durations[kClipCount] = {
            5.83333349f, 0.666666687f, 1.04166663f, 0.625f,
            3.33333325f, 1.08333337f, 0.833333313f,
        };
        for (int i = 0; i < kClipCount; ++i) {
            durations_[i] = durations[i];
        }
    }

    void Play(PlayerAnimState state, bool restart) {
        Track track{state};
        for (auto it = tracks_.begin(); it != tracks_.end(); ++it) {
            if (it->state != state) continue;
            track = *it;
            tracks_.erase(it);
            break;
        }
        const int index = static_cast<int>(state);
        track.duration = durations_[index];
        track.loops = state != PlayerAnimState::Dash;
        if (tracks_.empty()) track.blend = 1.0f;
        if (restart || track.blend <= 0.0f) track.time = 0.0f;
        track.blend_duration = tracks_.empty()
            ? 0.01f : BlendDuration(state, tracks_.back().state);
        tracks_.push_back(track);
    }

    void Clear() { tracks_.clear(); }

    void Advance(float dt) {
        if (tracks_.empty()) return;
        const float blend_duration = tracks_.back().blend_duration;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            Track& track = tracks_[i];
            track.time += rate * dt;
            if (track.time >= track.duration) {
                track.time = track.loops ? track.time - track.duration : track.duration;
            }
            if (blend_duration > 0.0f) {
                const float target = i + 1 == tracks_.size() ? 1.0f : 0.0f;
                const float step = dt / blend_duration;
                track.blend = target > track.blend
                    ? std::fmin(track.blend + step, target)
                    : std::fmax(track.blend - step, target);
            }
        }
    }

    float rate = 1.0f;
    const std::vector<Track>& Tracks() const { return tracks_; }

    static float BlendDuration(PlayerAnimState a, PlayerAnimState b) {
        if ((a == PlayerAnimState::Idle && b == PlayerAnimState::Dash) ||
            (b == PlayerAnimState::Idle && a == PlayerAnimState::Dash)) return 0.05f;
        if ((a == PlayerAnimState::Idle && b == PlayerAnimState::Run) ||
            (b == PlayerAnimState::Idle && a == PlayerAnimState::Run)) return 0.2f;
        if ((a == PlayerAnimState::Run && b == PlayerAnimState::Skid) ||
            (b == PlayerAnimState::Run && a == PlayerAnimState::Skid)) return 0.125f;
        return 0.01f;
    }

private:
    std::vector<Track> tracks_;
    float durations_[kClipCount]{};
};

uint32_t Next(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

bool Near(float a, float b) {
    return std::fabs(a - b) <= kEpsilon;
}

void AssertEquivalent(const PlayerAnimation& actual,
                      const SourceAnimationOracle& expected) {
    const auto& tracks = expected.Tracks();
    assert(actual.Count() == static_cast<int>(tracks.size()));
    for (int i = 0; i < actual.Count(); ++i) {
        const auto& a = actual.At(i);
        const auto& e = tracks[static_cast<std::size_t>(i)];
        assert(a.state == e.state);
        assert(Near(a.time, e.time));
        assert(Near(a.duration, e.duration));
        assert(Near(a.blend, e.blend));
        assert(Near(a.blend_duration, e.blend_duration));
        assert(a.loops == e.loops);
    }

    PlayerAnimation::SampleTrack samples[kClipCount]{};
    const int count = actual.Samples(samples, kClipCount);
    int expected_count = 0;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
        if (it->blend > 0.0f) ++expected_count;
    }
    assert(count == expected_count);
    int sample = 0;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
        if (it->blend <= 0.0f) continue;
        assert(samples[sample].state == it->state);
        // Mirror the production rule: one-shot tracks clamp to their final
        // pose (no wrap), looping tracks wrap via fmod.
        const float expected_time = it->loops
            ? std::fmod(it->time, it->duration)
            : std::fmin(it->time, it->duration);
        assert(Near(samples[sample].time, expected_time));
        assert(Near(samples[sample].weight, 1.0f - it->blend));
        ++sample;
    }
}

}  // namespace

int main() {
    PlayerAnimation actual;
    SourceAnimationOracle expected;
    for (int i = 0; i < kClipCount; ++i) {
        const auto state = static_cast<PlayerAnimState>(i);
        constexpr float durations[kClipCount] = {
            5.83333349f, 0.666666687f, 1.04166663f, 0.625f,
            3.33333325f, 1.08333337f, 0.833333313f,
        };
        actual.SetDuration(state, durations[i], state != PlayerAnimState::Dash);
    }

    uint32_t seed = 0x5EED1234u;
    for (int step = 0; step < 12000; ++step) {
        const uint32_t choice = Next(seed) % 100u;
        if (choice < 52u) {
            const auto state = static_cast<PlayerAnimState>(Next(seed) % kClipCount);
            const bool restart = (Next(seed) & 3u) == 0;
            actual.Play(state, restart);
            expected.Play(state, restart);
        } else if (choice < 60u) {
            actual.Clear();
            expected.Clear();
        } else if (choice < 88u) {
            constexpr float deltas[] = {1.0f / 60.0f, 1.0f / 30.0f, 0.05f, 0.2f, 0.7f};
            const float dt = deltas[Next(seed) % (sizeof(deltas) / sizeof(deltas[0]))];
            actual.Advance(dt);
            expected.Advance(dt);
        } else {
            const float rate = static_cast<float>(Next(seed) % 401u) * 0.01f;
            actual.rate = rate;
            expected.rate = rate;
        }
        AssertEquivalent(actual, expected);
    }

    // Match the source-valid 3/5/7-track checkpoints used by the weighted
    // pose fixture, so blend parity consumes clocks from a production-checked
    // interruption sequence rather than only synthetic weights.
    PlayerAnimation interrupted_actual;
    SourceAnimationOracle interrupted_expected;
    constexpr float durations[kClipCount] = {
        5.83333349f, 0.666666687f, 1.04166663f, 0.625f,
        3.33333325f, 1.08333337f, 0.833333313f,
    };
    for (int i = 0; i < kClipCount; ++i) {
        const auto state = static_cast<PlayerAnimState>(i);
        interrupted_actual.SetDuration(state, durations[i], state != PlayerAnimState::Dash);
    }
    interrupted_actual.Play(PlayerAnimState::Idle);
    interrupted_expected.Play(PlayerAnimState::Idle, false);
    interrupted_actual.Advance(0.1f);
    interrupted_expected.Advance(0.1f);
    interrupted_actual.Play(PlayerAnimState::Dash);
    interrupted_expected.Play(PlayerAnimState::Dash, false);
    interrupted_actual.Advance(0.005f);
    interrupted_expected.Advance(0.005f);
    interrupted_actual.Play(PlayerAnimState::Run);
    interrupted_expected.Play(PlayerAnimState::Run, false);
    interrupted_actual.Advance(0.0005f);
    interrupted_expected.Advance(0.0005f);
    AssertEquivalent(interrupted_actual, interrupted_expected);
    PlayerAnimation::SampleTrack samples[kClipCount]{};
    assert(interrupted_actual.Samples(samples, kClipCount) == 3);

    interrupted_actual.Play(PlayerAnimState::Skid);
    interrupted_expected.Play(PlayerAnimState::Skid, false);
    interrupted_actual.Advance(0.0005f);
    interrupted_expected.Advance(0.0005f);
    interrupted_actual.Play(PlayerAnimState::Jump);
    interrupted_expected.Play(PlayerAnimState::Jump, false);
    interrupted_actual.Advance(0.00002f);
    interrupted_expected.Advance(0.00002f);
    AssertEquivalent(interrupted_actual, interrupted_expected);
    assert(interrupted_actual.Samples(samples, kClipCount) == 5);

    interrupted_actual.Play(PlayerAnimState::ClimbIdle);
    interrupted_expected.Play(PlayerAnimState::ClimbIdle, false);
    interrupted_actual.Advance(0.000005f);
    interrupted_expected.Advance(0.000005f);
    interrupted_actual.Play(PlayerAnimState::ClimbUp);
    interrupted_expected.Play(PlayerAnimState::ClimbUp, false);
    interrupted_actual.Advance(0.0000005f);
    interrupted_expected.Advance(0.0000005f);
    AssertEquivalent(interrupted_actual, interrupted_expected);
    assert(interrupted_actual.Samples(samples, kClipCount) == 7);
    return 0;
}
