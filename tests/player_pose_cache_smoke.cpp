#include <cassert>
#include "gameplay/render/player_pose_sample_cache.hpp"
using namespace madeline_cube;
int main(){
 PlayerAnimation a; PlayerPoseSampleCache cache; PlayerAnimation::SampleTrack samples[7];
 a.Clear();a.Play(PlayerAnimState::Run);a.Advance(1.0f/60.0f);int count=a.Samples(samples,7);
 assert(!cache.Matches(samples,count));cache.Store(samples,count);
 // Source airborne Clear+Run repeats exactly the same sampled first pose.
 a.Clear();a.Play(PlayerAnimState::Run);a.Advance(1.0f/60.0f);count=a.Samples(samples,7);assert(cache.Matches(samples,count));
 a.Advance(1.0f/60.0f);count=a.Samples(samples,7);assert(!cache.Matches(samples,count));cache.Store(samples,count);
 a.Play(PlayerAnimState::Idle);a.Advance(.01f);count=a.Samples(samples,7);assert(!cache.Matches(samples,count));cache.Store(samples,count);
 samples[0].weight+=.01f;assert(!cache.Matches(samples,count));
 cache.Clear();assert(!cache.Matches(samples,count));
}
