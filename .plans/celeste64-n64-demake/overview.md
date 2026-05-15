# Celeste 64 N64 Demake

## What & why

Build the first native Nintendo 64 port-validation phase for *Celeste 64: Fragments of the Mountain* on stock 4 MB hardware. The C# game becomes the behavioural reference; the N64 build is a new C codebase that preserves movement feel, room logic, collectibles, menus, and story flow while using semantic placeholder geometry instead of final converted art. This plan deliberately stops before the full asset-conversion phase so the project can prove frame pacing, memory use, controller feel, room loading, and gameplay structure first.

## Increment DAG

- Inc 1 - Target spike (S) - depends on: none - unblocks: 2, 3, 4
- Inc 2 - Asset semantics + placeholder catalog (S) - depends on: 1 - unblocks: 5, 7
- Inc 3 - Runtime foundation (M) - depends on: 1 - unblocks: 4, 5, 7
- Inc 4 - Movement vertical slice (M) - depends on: 1, 3 - unblocks: 7, 8
- Inc 5 - Graybox world/render pipeline (M) - depends on: 2, 3 - unblocks: 7, 8
- Inc 6 - Save/debug foundation (S) - depends on: 3 - unblocks: 8
- Inc 7 - Gameplay systems in graybox (L) - depends on: 2, 3, 4, 5 - unblocks: 8
- Inc 8 - Playable prototype + hardware QA (M) - depends on: 4, 5, 6, 7 - unblocks: later asset-conversion plan

```text
1 -> 2 -> 5 ----\
 \-> 3 -> 4 ----+-> 7 -> 8
       \-> 6 --------^
```

## Top 3 risks

- Placeholder geometry can accidentally hide future asset-pressure problems; mitigate by recording each source asset's semantic role now and keeping the stock 4 MB graybox budget visible instead of pretending placeholders are final.
- Player feel can drift during the rewrite because the original controller is a large bespoke state machine; mitigate with recorded movement test scenes and parity captures before broad actor porting.
- The port can become over-scoped before it proves itself; mitigate by ending this plan at a hardware-tested graybox prototype and moving final asset conversion into a later dedicated plan.

## Files

- [data-model.md](data-model.md) - save layout and prototype runtime data formats
- [plan.md](plan.md) - dependency-ordered implementation increments
- [decisions.md](decisions.md) - architecture choices and assumptions
- [verification.md](verification.md) - acceptance scenarios for the prototype phase
- [inc-2-notes.md](inc-2-notes.md) - source asset meanings and placeholder geometry
