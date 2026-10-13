#include <cassert>
#include <cmath>
#include <limits>
#include "gameplay/player/player_animation.hpp"

using namespace madeline_cube;
static bool Near(float a, float b) { return std::fabs(a - b) < 0.00001f; }

int main() {
    PlayerAnimation animation;
    PlayerAnimation::SampleTrack samples[7];
    animation.Play(PlayerAnimState::Idle);
    assert(animation.Samples(samples, 7) == 1 && samples[0].weight == 0);
    animation.Advance(.1f);
    animation.Play(PlayerAnimState::Run);
    animation.Advance(.05f);
    assert(animation.Samples(samples, 7) == 2);
    assert(samples[0].state == PlayerAnimState::Run && Near(samples[0].weight, .75f));
    assert(samples[1].state == PlayerAnimState::Idle && Near(samples[1].weight, .25f));
    animation.Play(PlayerAnimState::Dash);
    animation.Advance(.005f);
    assert(animation.Count() == 3 && animation.Samples(samples, 7) == 2);
    assert(samples[0].state == PlayerAnimState::Dash);
    assert(Near(animation.At(0).time, .155f));
    animation.Play(PlayerAnimState::Run);
    assert(animation.At(animation.Count() - 1).time == 0); // fully faded Run restarts
    animation.Play(PlayerAnimState::Run, true);
    assert(animation.At(animation.Count() - 1).time == 0);
    assert(animation.Count() == 3);
    assert(Near(PlayerAnimation::BlendDuration(PlayerAnimState::Idle, PlayerAnimState::Run), .2f));
    assert(Near(PlayerAnimation::BlendDuration(PlayerAnimState::Run, PlayerAnimState::Idle), .2f));
    assert(Near(PlayerAnimation::BlendDuration(PlayerAnimState::Dash, PlayerAnimState::Idle), .05f));
    assert(Near(PlayerAnimation::BlendDuration(PlayerAnimState::Run, PlayerAnimState::Skid), .125f));
    assert(Near(PlayerAnimation::BlendDuration(PlayerAnimState::ClimbIdle, PlayerAnimState::ClimbUp), .01f));
    animation.Clear();
    animation.Play(PlayerAnimState::Run);
    animation.Advance(.016f);
    animation.Clear();
    animation.Play(PlayerAnimState::Run);
    assert(animation.Count() == 1 && animation.At(0).time == 0);
    animation.rate = 1.8f;
    animation.Advance(.1f);
    assert(Near(animation.At(0).time, .18f));
    animation.Advance(std::numeric_limits<float>::infinity());
    assert(Near(animation.At(0).time, .18f));
    animation.Clear();
    animation.Play(PlayerAnimState::Dash);
    animation.Advance(.625f);
    // One-shot Dash clamps to its final pose; it must sample the end of the
    // track, not wrap back to t = 0.
    assert(animation.Samples(samples, 7) == 1 && samples[0].time == 0.625f);
    animation.Advance(.2f);
    assert(Near(animation.At(0).time, .625f));
    // Faded tracks are retained and their clocks still advance.
    animation.Play(PlayerAnimState::Idle);
    animation.Advance(.1f);
    assert(animation.Count() == 2 && animation.At(0).blend == 0);
    animation.Play(PlayerAnimState::Dash);
    assert(animation.At(1).time == 0);

    // Exercise every directed two-clip transition against OG's retained track
    // clock and reverse-ordered (1 - Blend) sampling contract.
    constexpr PlayerAnimState states[] = {
        PlayerAnimState::Idle, PlayerAnimState::Run, PlayerAnimState::Jump,
        PlayerAnimState::Dash, PlayerAnimState::ClimbIdle,
        PlayerAnimState::ClimbUp, PlayerAnimState::Skid,
    };
    constexpr float fractions[] = {0.0f, 0.125f, 0.5f, 0.875f, 1.0f};
    for (PlayerAnimState from : states) {
        for (PlayerAnimState to : states) {
            if (from == to) continue;
            for (float fraction : fractions) {
                PlayerAnimation transition;
                transition.SetDuration(from, 10.0f);
                transition.SetDuration(to, 10.0f);
                transition.Play(from);
                transition.Advance(0.25f);
                transition.Play(to);
                const float elapsed = PlayerAnimation::BlendDuration(from, to) * fraction;
                transition.Advance(elapsed);
                const int count = transition.Samples(samples, 7);
                if (fraction == 0.0f || fraction == 1.0f) {
                    assert(count == 1);
                    assert(samples[0].state == (fraction == 0.0f ? from : to));
                    assert(samples[0].weight == 0.0f);
                } else {
                    assert(count == 2);
                    assert(samples[0].state == to && samples[1].state == from);
                    assert(Near(samples[0].weight, 1.0f - fraction));
                    assert(Near(samples[1].weight, fraction));
                    assert(Near(samples[0].time, elapsed));
                    assert(Near(samples[1].time, 0.25f + elapsed));
                }
            }
        }
    }

    // Source retains fading clocks when a third clip interrupts a transition.
    PlayerAnimation interrupted;
    interrupted.SetDuration(PlayerAnimState::Idle, 10.0f);
    interrupted.SetDuration(PlayerAnimState::Run, 10.0f);
    interrupted.SetDuration(PlayerAnimState::Dash, 10.0f);
    interrupted.Play(PlayerAnimState::Idle);
    interrupted.Advance(0.1f);
    interrupted.Play(PlayerAnimState::Run);
    interrupted.Advance(0.08f);  // Idle .6, Run .4
    interrupted.Play(PlayerAnimState::Dash);
    interrupted.Advance(0.0025f);  // all three tracks move using Run->Dash .01 s
    assert(interrupted.Count() == 3);
    assert(Near(interrupted.At(0).blend, 0.35f));
    assert(Near(interrupted.At(1).blend, 0.15f));
    assert(Near(interrupted.At(2).blend, 0.25f));
    assert(interrupted.Samples(samples, 7) == 3);
    assert(samples[0].state == PlayerAnimState::Dash);
    assert(samples[1].state == PlayerAnimState::Run);
    assert(samples[2].state == PlayerAnimState::Idle);
    assert(Near(samples[0].weight, 0.75f));
    assert(Near(samples[1].weight, 0.85f));
    assert(Near(samples[2].weight, 0.65f));
}
