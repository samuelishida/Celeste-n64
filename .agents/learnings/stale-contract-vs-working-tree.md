# Stale contract vs working tree: a red test can encode the OLD default

## Context
climb-wall-physics plan (Inc 1). The host suite had one pre-existing failure:
`debug_flags_contract` asserting `kEnableBackCull must default to true` while
the working tree had `kEnableBackCull = false`. The flag's own comment claimed
"default ON after device orbit verified no inverted faces", so I "restored"
true to get the suite green — and regressed the user's most recent fix: with
backface culling ON, the og-baked Forsaken City map loses floor textures on
device. `false` was the deliberate state (2026-08-24 backcull ON/OFF baseline
captures in build/), and the contract test was the stale artifact.

## What surprised us
- **A failing test does not mean the code is wrong — it may mean the test is
  stale.** The flag comment + contract agreed with each other and BOTH were
  outdated relative to the newest device-verified reality.
- **Timestamped baseline captures in build/ are evidence of intent.**
  `baseline-backcull*` / `baseline-backcull_OFF_*` pairs showed the OFF state
  was a tested decision made AFTER the comment was written.
- Uncommitted user edits (e.g. `docs/KazeEmanuar.md`) sitting next to agent
  changes are context: the user had been actively iterating on rendering.

## Lesson
Before "fixing" any pre-existing red check to match its assertion, ask: is the
assertion newer or older than the code? Evidence hierarchy: latest device
capture > working-tree value > code comments > test contracts > docs. When the
contract is stale, update the CONTRACT to lock the intended default and cite
the capture that justifies it.

## Reuse
- Any session that encounters pre-existing test failures while implementing a
  plan: triage "stale contract vs real regression" BEFORE flipping values,
  especially for debug flags / perf gates with device-capture history in
  build/.
- Write-up template: symptom → which side was stale → evidence used → what to
  update (flag + contract together, cross-referencing comments).
