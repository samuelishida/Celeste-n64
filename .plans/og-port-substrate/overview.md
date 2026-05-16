# OG port substrate

## What & why

The original game is easier to read than to port because a lot of its behaviour lives below `Player.cs`: buffered virtual input, math/time helpers, a small state-machine layer, actor lifecycle queries, actor traits, actor-backed solids, and moving-platform rider rules. Our C++ side already has a strong motor and source-shaped query outputs, but it still bakes many of those services directly into one player loop and one `Room` struct. This plan adds the missing substrate in layers so future source ports become translation work instead of archaeology.

## Increment DAG

- Inc 1 — Port-gap ledger + trace fixtures (S) — depends on: none — unblocks: 2, 3, 7
- Inc 2 — Input compatibility layer (M) — depends on: 1 — unblocks: 7
- Inc 3 — Runtime helper layer (S) — depends on: 1 — unblocks: 4, 7
- Inc 4 — Actor/world lifecycle + traits (M) — depends on: 3 — unblocks: 5, 7
- Inc 5 — Actor-backed solid/query facade (M) — depends on: 4 — unblocks: 6, 7
- Inc 6 — Moving-solid rider bridge (M) — depends on: 5 — unblocks: 7
- Inc 7 — Player parity backfill on the new substrate (L) — depends on: 2, 3, 4, 5, 6 — unblocks: future actor ports

```text
1 ─┬─> 2 ───────────────────────┐
   ├─> 3 ─> 4 ─> 5 ─> 6 ────────┼─> 7
   └─────────────────────────────┘
```

## Top 3 risks

- Recreating too much of Foster would waste ROM budget; the mitigation is to port only the semantics already exercised by `Player.cs` and nearby actors, then stop.
- Replacing the current simple `Room` flow too early could destabilize a motor that is finally becoming trustworthy; keep the current motor boundary and migrate world/query ownership underneath it.
- The actor migration can quietly change frame timing; keep the explicit update contract and verify moving-platform behaviour before player backfill.

## Files

- [data-model.md](data-model.md) — in-memory compatibility contracts
- [plan.md](plan.md) — dependency-ordered increments
- [decisions.md](decisions.md) — architecture choices and source-backed assumptions
- [verification.md](verification.md) — acceptance scenarios
- [contracts.md](contracts.md) — proposed compatibility API surface
