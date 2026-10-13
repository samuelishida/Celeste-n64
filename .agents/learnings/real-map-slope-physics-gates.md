# Real-map slope physics gates

## Context
Added a CMSH-backed slope regression because synthetic-normal tests did not
prove movement stayed on a real walkable ramp.

## Hardest decision
Treat a grounded but zero-distance uphill replay as blocked, not as evidence of
correct uphill slowdown. Search stable two-way ramp corridors and require both
ground-normal continuity and nonzero displacement before comparing speeds.

## Alternatives rejected
- Keep synthetic `ground_normal` fixtures as the only slope gate — they bypass
  mesh support, ledge assist, and adjacent wall collisions.
- Tune slope constants after a blocked run — the failure was route selection,
  while the successful real-map track matched OG ordering and expected speed.

## Least confident
Current gate measures one stable 22-degree CMSH ramp over 12 ticks. It does not
cover the full slope-angle range or establish subjective controller feel.

## Reuse
Use `tests/real_map_slope_smoke.cpp` and its visible `[real-map-slope]` metrics
when changing slope modulation, floor queries, or ledge assist. Keep current-ROM
manual slope play open until a human confirms feel.
