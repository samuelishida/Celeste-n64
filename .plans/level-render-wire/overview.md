# Level Render Wire

## What & why

The level pipeline now loads `1-1`, but the first textured ROM pass exposed a missing hardware boundary: two imported OG materials exceed N64 TMEM and crash on `rdpq_sprite_upload`. This plan keeps the first room focused on rendering infrastructure, not asset parity. It preserves the authored material names, generates TMEM-safe N64 derivatives for the room, validates them before ROM build, then finishes the textured `LevelRenderer` path.

## Increment DAG

```txt
Inc 1 done -> Inc 2 -> Inc 3 -> Inc 4
```

- Inc 1 — DFS + bake wiring (done, S) — depends on: none — unblocks: 2
- Inc 2 — TMEM-safe material derivatives (M) — depends on: 1 — unblocks: 3
- Inc 3 — Material budget validation (S) — depends on: 2 — unblocks: 4
- Inc 4 — Textured LevelRenderer (M) — depends on: 3 — unblocks: none

## Top 3 risks

- Oversized OG textures silently re-enter the room later; mitigate with a deterministic manifest-level TMEM budget check before rendering.
- Downscaled materials may lose source-art fidelity; accept for the first room because the current goal is proving the renderer, not OG parity.
- Per-face draws may miss the 33 ms frame target; measure in Ares after textures work, then batch by material if needed.

## Files

- [data-model.md](data-model.md) — generated artifact shape
- [plan.md](plan.md) — increment list
- [decisions.md](decisions.md) — architectural choices
- [verification.md](verification.md) — acceptance scenarios
