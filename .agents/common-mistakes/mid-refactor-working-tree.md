# Mid-refactor working tree: one WIP file of a multi-file batch

## Symptom
`make test` fails with 10 player-linked smokes and the ROM build dies at
`src/user/gameplay/player/player_controller.o` with errors that look like a
new-code bug: `'ControllerInput' does not name a type`,
`'CollisionWorld' does not name a type`, `no declaration matches …`.

## Cause
The uncommitted working tree can carry a **stalled multi-file refactor** where
only ONE file was edited. In this state (`git status`):
- `player_controller.cpp` (uncommitted, 1622-line diff) was rewritten to a new
  API — `Step(const ControllerInput&, const CollisionWorld*)`, `TimerPhase`,
  `StartDash`, `StartDashJump`, `TickDash` — but:
  - `player_controller.hpp` is UNMODIFIED and still declares the old API
    (`Step(const int&, …)`, `TimerInputPhase`, old `StatePhase`/`MovePhase`);
  - the `ControllerInput` / `CollisionWorld` types exist NOWHERE in the tree;
  - the caller (`gameplay_scene.cpp`) still calls `TimerInputPhase` (old API).
- Result: no configuration of this tree compiles, and the break stops BOTH
  the host smoke suite and the ROM build — independent of any new increment's
  code.

## Diagnosis (do before blaming new code)
1. `git status --short` first — a modified file you did not touch is a WIP.
2. `git diff --stat <that file>` — a huge ± diff vs. your small one narrows it.
3. Stash-isolate only that file: `git stash push <file>` → re-run one failing
   test → `git stash pop`. If the failure changes shape (compile → link, or
   disappears), the isolated file is the culprit and it is NOT your increment.
4. Verify the new types/methods exist in a header (`rg -n "TypeName" src/user`)
   and that the caller uses the new API before touching anything.

## Prevention / handling
- Never "fix" the WIP as part of an unrelated increment (movement physics was
  explicitly out of scope here) — surface it, ask the owner, and record the
  blocker in the plan file's increment status.
- The tree has 20 local commits ahead of origin; WIP often lands between
  sessions. Re-check `git status` at the start of every plan increment.
