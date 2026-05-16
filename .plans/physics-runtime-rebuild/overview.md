# Physics Runtime Rebuild

## What & why

The current movement branch copied many visible player rules from *Celeste 64*, but the runtime underneath them is still not source-equivalent and is now failing on the ROM path despite host tests passing. This plan replaces the patched prototype physics stack with a measured rebuild: first create an actual C# reference oracle and ROM diagnostics, then lock query/motor contracts, then rebuild sweep/popout/late-contact behavior before re-porting locomotion, climb, platform, and camera behavior on top. Scope is the whole gameplay physics system for the graybox prototype on stock 4 MB hardware; asset conversion stays out of scope.

## Increment DAG

- Inc 1 - Real reference oracle + ROM telemetry (M) - depends on: none - unblocks: 4, 5, 6, 9
- Inc 2 - Runtime contracts + geometry fixtures (M) - depends on: none - unblocks: 3, 4
- Inc 3 - Source-shaped world queries (L) - depends on: 2 - unblocks: 4, 6, 7, 8
- Inc 4 - Single-owner player motor (L) - depends on: 1, 2, 3 - unblocks: 5, 6, 7, 8
- Inc 5 - Core locomotion parity (L) - depends on: 1, 4 - unblocks: 9
- Inc 6 - Wall and climb parity (L) - depends on: 1, 3, 4 - unblocks: 9
- Inc 7 - Slopes, ledges, and platform carry (L) - depends on: 3, 4 - unblocks: 9
- Inc 8 - Camera, respawn, and scene integration (M) - depends on: 3, 4 - unblocks: 9
- Inc 9 - Cutover, cleanup, and hardware signoff (M) - depends on: 5, 6, 7, 8 - unblocks: later room/content work

```text
1 --------> 4 ---> 5 --------\
 \          \--> 6 ----------+--> 9
  \          \-> 7 ----------/
2 ---> 3 ----\-> 8 ---------/
 \----> 4
```

## Top 3 risks

- The current parity corpus is modeled from constants, not captured from the C# runtime, so it can prove the wrong code "correct"; mitigate by making real capture the first dependency.
- Host tests already missed a ROM failure path; mitigate with low-cost on-ROM diagnostics and hardware acceptance before final signoff.
- A source-faithful query layer is richer than the current planes-and-boxes world and may pressure frame time or memory; mitigate with fixed-capacity data, counters, and stock 4 MB budgets from the first rebuild increment.

## Files

- [data-model.md](data-model.md) - runtime contracts and fixture schema
- [plan.md](plan.md) - dependency-ordered repair increments
- [decisions.md](decisions.md) - architectural choices and sourced assumptions
- [verification.md](verification.md) - acceptance scenarios
