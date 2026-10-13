#pragma once
#include <cstdint>
extern uint64_t profiler_test_ticks;
inline uint64_t timer_ticks(){return profiler_test_ticks;}
#define TIMER_TICKS_LL(us) (us)
