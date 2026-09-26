# Fixed-step work caps must preserve and report backlog

## Context
Physics timing regression audit after Ares reported a 121.7 ms render/streaming
spike and movement reports described inconsistent control response.

## Mistake
`FixedStepAccumulator::BeginFrame()` limited incoming `dt` to the per-frame
work budget, then discarded the excess. With a 5-tick work cap, any frame over
83.3 ms permanently skipped simulation time. A single 121.7 ms hitch discarded
about 38 ms; recurring stalls made physics run slower than wall time with no
trace evidence.

## Rule
Bound work per render frame, not elapsed simulation time. Carry short hitches
forward in a bounded accumulator, cap catch-up to avoid a spiral, and expose
both pending ticks and deliberately dropped time in telemetry. Test a frame
larger than the work cap, next-frame catch-up, long-stall bound, and invalid
`dt` handling.
