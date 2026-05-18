# First Room Rebuild

## What & why

The current imported `1-1` path is doing two jobs badly: it is visually incoherent in ROM and it is not trustworthy collision input. Rather than spend the first real room budget recovering source-map fidelity, this plan creates a project-owned room that serves the same purpose as Milestone 2: a compact movement test with a dash gap, climb wall, collectible route, and respawn challenge. Gameplay truth comes from a clean authored map; visible room geometry comes from a paired render asset; tests keep the two honest at named traversal anchors.

## Increment DAG

```txt
Inc 1 ─┬─> Inc 2 ─┐
       └─> Inc 3 ─┴─> Inc 4 ─> Inc 5 ─> Inc 6
                         ^
                         └─ external gate: renderer-overhaul Inc 7 complete before Inc 5
```

- Inc 1 — Room brief + source contract (S) — depends on: none — unblocks: 2, 3
- Inc 2 — Gameplay source + collision artifacts (M) — depends on: 1 — unblocks: 4
- Inc 3 — Render source + `.t3dm` artifact (M) — depends on: 1 — unblocks: 4
- Inc 4 — Cross-artifact acceptance fixtures (M) — depends on: 2, 3 — unblocks: 5
- Inc 5 — Runtime cutover to `first-room` (M) — depends on: 4 + renderer-overhaul Inc 7 — unblocks: 6
- Inc 6 — Retire imported-room dependency (S) — depends on: 5 — unblocks: none

## Top 3 risks

- Paired gameplay/render sources can drift; mitigate with named anchors plus host checks that fail when the room stops agreeing with itself.
- The content can be ready before the active `.t3dm` room path is finished; mitigate by keeping Incs 1–4 content-only and gating runtime cutover on the renderer plan.
- A room that looks better but teaches the wrong movement becomes expensive decoration; mitigate by freezing the Milestone 2 traversal beats before authoring geometry.

## Files

- [data-model.md](data-model.md) — authored sources, generated artifacts, migration shape
- [plan.md](plan.md) — increment list
- [decisions.md](decisions.md) — architectural choices and sourced assumptions
- [verification.md](verification.md) — acceptance scenarios
