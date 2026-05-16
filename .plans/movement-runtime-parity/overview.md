# Movement Runtime Parity

## What & why

The current ROM feels better than the first prototype, but it still does not move like *Celeste 64* because the controller math was ported ahead of the physics runtime that gives that math its feel. This plan reopens the movement milestone and rebuilds the missing substrate in dependency order: source-derived parity traces, deterministic collision queries, a phased player loop, sweep/popout/ground-snap motion, then the higher-level locomotion, climb, surface, and camera behaviours that depend on that foundation. Scope stays focused on movement fidelity inside the graybox validation route on stock 4 MB hardware; final art conversion remains outside this plan.

## Increment DAG

- Inc 1 - Reference parity corpus (S) - depends on: none - unblocks: 5, 6
- Inc 2 - Physics query substrate (M) - depends on: none - unblocks: 4, 6, 7, 8
- Inc 3 - Player state + phased loop (M) - depends on: none - unblocks: 4
- Inc 4 - Source-like character motor (L) - depends on: 2, 3 - unblocks: 5, 6, 7, 8
- Inc 5 - Normal, dash, and skid parity (M) - depends on: 1, 4 - unblocks: 7, 9
- Inc 6 - Wall probe and climb parity (L) - depends on: 1, 2, 4 - unblocks: 9
- Inc 7 - Slopes, ledges, and platform carry (L) - depends on: 2, 4, 5 - unblocks: 9
- Inc 8 - Camera collision coupling (M) - depends on: 2, 4 - unblocks: 9
- Inc 9 - Tuning pass + hardware QA (M) - depends on: 5, 6, 7, 8 - unblocks: later room/content work

```text
1 -----> 5 ----------------\
 \-----> 6 -----------------+-> 9
2 ---> 4 ---> 6 ------------/
 \      \----> 7 ----------/
  \----------> 8 ---------/
3 ---> 4
```

## Top 3 risks

- A richer collision motor can eat frame budget on stock 4 MB hardware; mitigate with fixed-capacity structures, query counters, and profiler checks before advanced states land.
- Porting from source `+Z` up into this ROM's `+Y` up can create subtle false parity; mitigate with committed reference traces and semantic scenario names instead of tuning by impression alone.
- Re-porting climb and platform behaviour before the motor is stable would create churn; mitigate by keeping them downstream of the sweep/popout/ground-snap increment.

## Files

- [data-model.md](data-model.md) - runtime entities, query outputs, and parity fixture shape
- [plan.md](plan.md) - dependency-ordered implementation increments
- [decisions.md](decisions.md) - architecture choices and sourced assumptions
- [verification.md](verification.md) - end-to-end acceptance scenarios
