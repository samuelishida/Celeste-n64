#include <cassert>
#include <cmath>
#include "n64/profiler.hpp"
uint64_t profiler_test_ticks=0;
int main(){
 n64::FrameProfiler p(2); p.SetSilent(true);
 auto frame=[&](uint64_t update_ticks,uint64_t hair_ticks){
  p.BeginFrame();
  p.BeginPhase(n64::FrameProfiler::kPhaseUpdate);
  profiler_test_ticks+=update_ticks;
  p.EndPhase(n64::FrameProfiler::kPhaseUpdate);
  p.BeginPhase(n64::FrameProfiler::kPhasePlayerHair);
  profiler_test_ticks+=hair_ticks;
  p.EndPhase(n64::FrameProfiler::kPhasePlayerHair);
  p.BeginPhase(n64::FrameProfiler::kPhaseFixedStepLoop);
  profiler_test_ticks+=update_ticks/2;
  p.EndPhase(n64::FrameProfiler::kPhaseFixedStepLoop);
  p.BeginPhase(n64::FrameProfiler::kPhaseNearGroupBuild);
  profiler_test_ticks+=update_ticks/4;
  p.EndPhase(n64::FrameProfiler::kPhaseNearGroupBuild);
  p.BeginPhase(n64::FrameProfiler::kPhaseFixedStepLoop);
  profiler_test_ticks+=update_ticks-update_ticks/2;
  p.EndPhase(n64::FrameProfiler::kPhaseFixedStepLoop);
  p.EndFrame();
 };
 frame(10000,2000);frame(2000,6000);
 assert(std::fabs(p.phase_average_ms(n64::FrameProfiler::kPhaseUpdate)-6.0f)<.001f);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseUpdate)==10);
 assert(std::fabs(p.phase_average_ms(n64::FrameProfiler::kPhasePlayerHair)-4.0f)<.001f);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhasePlayerHair)==6);
 assert(std::fabs(p.phase_average_ms(n64::FrameProfiler::kPhaseFixedStepLoop)-6.0f)<.001f);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseFixedStepLoop)==10);
 assert(std::fabs(p.phase_average_ms(n64::FrameProfiler::kPhaseNearGroupBuild)-1.5f)<.001f);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseNearGroupBuild)==2.5f);
 frame(3000,0);frame(1000,0);
 assert(p.phase_average_ms(n64::FrameProfiler::kPhaseUpdate)==2);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseUpdate)==3);
 assert(p.phase_average_ms(n64::FrameProfiler::kPhasePlayerHair)==0);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhasePlayerHair)==0);
 assert(p.phase_average_ms(n64::FrameProfiler::kPhaseFixedStepLoop)==2);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseFixedStepLoop)==3);
 assert(p.phase_average_ms(n64::FrameProfiler::kPhaseNearGroupBuild)==0.5f);
 assert(p.phase_peak_ms(n64::FrameProfiler::kPhaseNearGroupBuild)==0.75f);
 assert(p.phase_average_ms(n64::FrameProfiler::kPhasePlayerAnimation)==0);
}
