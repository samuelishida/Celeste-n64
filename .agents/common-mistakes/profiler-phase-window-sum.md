# Profiling phase means require a report-window sum

Symptom: phase average0.13ms alongside8ms peak on steady frames; occasional280ms peaks with a similarly tiny average.
Cause: BeginFrame reset phase_accum_ticks each frame, but EndFrame divided only the final frame's value by60. Whole-frame averages and phase peaks had separate accumulators and were not affected.
Fix: add phase_window_ticks, accumulate each completed frame, divide by actual window count, reset only when reporting. The host test drives10ms+2ms frames and requires6ms mean/10ms peak, then checks a clean second window.
All historical optimization decisions based on old phase means must be remeasured. Do not conclude physics costs<1ms from those captures.
