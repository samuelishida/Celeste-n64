#include <cassert>
#include "gameplay/player/player_animation.hpp"
using namespace madeline_cube;
int main() {
    PlayerAnimation animation;
    animation.Play(PlayerAnimState::Run);
    animation.Advance(1.0f/60.0f);
    // Original synchronous skid exit resets Idle before dash handler starts.
    animation.Play(PlayerAnimState::Skid, true);
    animation.Play(PlayerAnimState::Idle, true);
    assert(animation.Current()==PlayerAnimState::Idle);
    animation.Play(PlayerAnimState::Dash);
    assert(animation.Current()==PlayerAnimState::Dash);
    // Normal airborne Clear+Run discards an interrupted transition.
    animation.Clear(); animation.Play(PlayerAnimState::Run);
    assert(animation.Count()==1 && animation.At(0).time==0);
    animation.Advance(1.0f/60.0f);
    assert(animation.At(0).time>0);
}
