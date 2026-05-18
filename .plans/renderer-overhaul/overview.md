# Renderer Overhaul

## What & why

The first textured room is proving that the present boundary is wrong, not merely unfinished. tiny3d already gives this project a mature static-model path, while our room currently bypasses it with a custom `.lvl` face stream that also doubles as collision data. The current bake output is not trustworthy enough to debug visually: `1-1.lvl` includes non-world brushes, duplicate vertices on 226 faces, collinear first-fan triangles on 202 faces, and polygon winding opposite the stored normal on all 402 faces. This plan first establishes ground truth, keeps LVL1 as a compatibility shell for gameplay, splits visible room geometry into `.t3dm`, and moves static room drawing onto tiny3d's native model path without pretending the source map is already render-ready.

## Increment DAG

```txt
Inc 1 diagnostics ───────> Inc 3 map/bake audit ───────────────┐
                                                               ├──> Inc 6 first-room render asset ──> Inc 7 cutover
Inc 2 artifact contract ─> Inc 4 native .t3dm renderer ─┐      │
                                                        └─> Inc 5 render-asset validation ─────────┘
```

- Inc 1 — Ground-truth diagnostics (M) — depends on: none — unblocks: 3
- Inc 2 — Artifact + LVL1 compatibility contract (S) — depends on: none — unblocks: 4, 5
- Inc 3 — Map + baker audit tooling (M) — depends on: 1 — unblocks: 6
- Inc 4 — Native `.t3dm` room renderer (M) — depends on: 2 — unblocks: 5, 6
- Inc 5 — Render-artifact validation (M) — depends on: 2, 4 — unblocks: 6
- Inc 6 — First-room render asset pipeline (L) — depends on: 3, 4, 5 — unblocks: 7
- Inc 7 — Scene cutover + legacy-path quarantine (M) — depends on: 6 — unblocks: none

## Top 3 risks

- The OG `.map` may encode game-specific brush semantics that are expensive to recover exactly; mitigate by keeping diagnostics first and allowing the first render room to be re-authored if the audit says parity is the wrong bargain.
- A renderer rewrite could hide an asset bug behind new abstraction; mitigate by requiring a known-good tiny3d fixture and a bake report before cutover.
- Replacing the custom room path with `.t3dm` could solve correctness but regress ROM size or frame time; mitigate by recording model stats, ROM delta, and Ares frame behavior before quarantining the fallback path.

## Files

- [data-model.md](data-model.md) — artifact/data contract
- [plan.md](plan.md) — increment list
- [decisions.md](decisions.md) — architectural choices
- [verification.md](verification.md) — acceptance scenarios
- [contracts.md](contracts.md) — runtime and build contracts
