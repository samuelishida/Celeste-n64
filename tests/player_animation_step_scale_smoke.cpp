#include <cassert>
#include "gameplay/render/player_animation_source.hpp"
using namespace madeline_cube;
int main() {
    assert(kPlayerSourceScaleTrackCount == 10);
    for (int i = 0; i < kPlayerSourceScaleTrackCount; ++i) {
        const auto& track = kPlayerSourceScaleTracks[i];
        const auto& first = kPlayerSourceScaleKeys[track.offset];
        const auto& last = kPlayerSourceScaleKeys[track.offset + track.count - 1];
        assert(&SamplePlayerSourceScale(i, 0) == &first);
        assert(&SamplePlayerSourceScale(i, last.time - .00001f) == &first);
        assert(&SamplePlayerSourceScale(i, last.time) == &last);
        assert(&SamplePlayerSourceScale(i, last.time + .1f) == &last);
    }
    assert(kPlayerSourceClipDurations[6] == .8333333134651184f);
}
