# Deep Research Plan: Celeste64 Controls & Physics → N64 Port

**Slug:** `celeste64-controls-physics-n64-port`
**Date:** 2026-09-07
**Mode:** lead-owned direct research (degraded — subagent spawn and web_search are blocked in this session; only `web_fetch` is available for online refs, plus repo-local reads)

## Topic

Analyze what we have (the current N64 demake port in `src/user/gameplay/`, the docs in `docs/`, and the reference checkout `Celeste64-og/`), cross-check against online references for the original Celeste64 control/physics behavior, and produce:

1. A **control and physics spec** — the authoritative statement of how the player should move/feel on the N64 (input mapping, movement constants, state machine, collision, camera).
2. A **clean-port plan** — a DAG of increments to close the gap between the current port and the spec, in the repo's `.plans/` convention.

## Key questions

- **Q1 (Controls):** What is the OG Celeste64 control scheme (buttons, sticks, camera-relative input, deadzones, buffering), and what is the current N64 mapping (`input_system.cpp`)? What gaps exist (virtual buttons/sticks, buffering, camera-relative mapping)?
- **Q2 (Physics):** What are the OG movement constants and state machine (ground/air accel, friction, jump/coyote/buffer, dash, skid, climb, walljump, spring, feather), and how faithful is the current `MoveService` single-mover port? What is missing or deviating (documented deviations M1/M2, etc.)?
- **Q3 (Collision):** How does OG `SweepTestMove` + `Popout` + ground/ceiling/wall checks work, and how does the port's `MoveService` compare? What edge cases remain (sloped walls, thin walls, moving solids, riders)?
- **Q4 (Camera):** OG camera behavior (follow, orbit, zoom, FOV) vs the port's `CameraController` — what's faithful, what deviates?
- **Q5 (Port plan):** What increments are needed to close the gap cleanly, in dependency order, with host-smoke + device-capture verification per increment?

## Evidence needed

- Repo-local: `docs/controller_spec.md`, `docs/movement_spec.md`, `docs/og_port_gap_matrix.md`, `docs/physics_feel_acceptance.md`, `docs/feel_fixture_taxonomy.md`, `src/user/gameplay/player/*`, `src/user/gameplay/input/input_system.cpp`, `src/user/gameplay/physics/*`, `tests/*` (climb probes, move_service_smoke, feel fixtures).
- Reference: `Celeste64-og/Source/Actors/Player.cs`, `Celeste64-og/Source/Scenes/World.cs`, `Celeste64-og/Source/Graphics/Camera.cs`, `Celeste64-og/Source/Data/Controls.cs` + `ControlsConfig.cs`, `Celeste64-og/Source/Game.cs`.
- Online (via `web_fetch` only): the EXOK/Celeste64 GitHub repo (Player.cs, Controls.cs, ControlsConfig.cs raw URLs), any community writeups of Celeste64 controls/physics, libdragon input docs if needed.

## Scale decision

**Lead-owned direct research.** This is a repo-grounded analysis (the primary evidence is local: our port + the OG checkout), with online refs as secondary confirmation. Subagent spawn is blocked (session not under tmux) and web_search is blocked (no credentials), so the lead performs all search/fetch/read directly. No researcher subagents are allocated.

## Task ledger

| # | Task | Owner | Status |
| --- | --- | --- | --- |
| 1 | Recon repo docs + port code (controls, movement, collision, camera) | lead | done |
| 2 | Read OG reference (Player.cs, World.cs, Camera.cs, Controls.cs) | lead | done |
| 3 | Fetch online refs (EXOK/Celeste64 GitHub raw files, community writeups) | lead | done |
| 4 | Write research notes to `outputs/.drafts/celeste64-controls-physics-n64-port-research-direct.md` | lead | done |
| 5 | Draft spec + port plan to `outputs/.drafts/celeste64-controls-physics-n64-port-draft.md` | lead | done |
| 6 | Cite (inline links + Sources) → `-cited.md` | lead | done |
| 7 | Self-review → `-verification.md` (FATAL/MAJOR/MINOR) | lead | done |
| 8 | Deliver `outputs/celeste64-controls-physics-n64-port.md` + `.provenance.md` | lead | done |

## Verification log

- [x] All 5 required artifacts exist on disk (verified via `wc -c` + `diff`)
- [x] Every critical claim maps to a source (repo path, OG file, or verified URL)
- [x] No invented sources/figures; inferences marked as inferences (Foster VirtualStick internals)
- [x] Provenance lists sources accepted/rejected and verification status (PASS WITH NOTES)

## Decision log

- **2026-09-07:** Mode = lead-owned direct (subagents + web_search blocked; only web_fetch for online). Scale = direct research (repo-grounded analysis, not a broad survey).
- **2026-09-07:** Deliverable = spec + port plan (the user asked for both "formulate the control and physics spec" and "formulate a plan to cleanly port").
