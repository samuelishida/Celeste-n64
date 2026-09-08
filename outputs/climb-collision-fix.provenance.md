# Provenance — climb-collision-fix

## Artifacts
- `autoresearch.md` — session state (target, metric, benchmark, iteration log).
- `autoresearch.jsonl` — 6 iteration records (baseline + 5 fixes, incl. the
  reverted signed-pushout approach).
- `autoresearch.sh` — the benchmark command (compiles + runs the probe).
- `CHANGELOG.md` — milestone entry.
- `outputs/climb-collision-fix.md` — the final brief (this artifact's source).

## Benchmark commands
- Probe: `bash autoresearch.sh` → `PASS: climb_bug_probe` (16/16).
- Full regression: `make test` → `=== 57 passed, 0 failed ===`.
- ROM build: `./compile-rom.sh` → exit 0, `madeline_cube_rom.z64`.
- Device: `timeout 300 bash tools/capture_baseline.sh madeline_cube_rom.z64
  150 climbfix` → raw log `build/raw-baseline-climbfix-20260907-140725.txt.log`.

## Raw evidence paths
- `build/raw-baseline-climbfix-20260907-140725.txt.log` — device capture
  (167× near_batches=56, [memory] used=5,822,032, no exceptions).
- `/tmp/climb_bug_probe` — the probe binary (16/16 PASS).
- `tests/climb_bug_probe.cpp` — the probe source (the deterministic
  benchmark).

## Iteration decisions
1. Baseline 6/16 → root cause: straight-up climb movement on sloped walls.
2. wallUp movement → overhang sustains; down-push remains.
3. SweepMoveClimb + entry snap no-cap → overhang clean; lean-away ejected.
4. Signed pushout (WRONG — pushed bodies through walls) → REVERTED.
5. Front-side gate + corner lateral gate + pre-normalization check → 16/16.
6. OG pushout semantics revert → step-down snap fixed; 57/57; device clean.

## Source URLs
None — this was a codebase investigation (the OG Celeste64 source in
`Celeste64-og/Source/` is the reference spec: Player.cs:1476-1480 climb
movement, Player.cs:642-718 SweepTestMove/Popout, World.cs:519-527 wall
query/pushout).

## Verification status
**BLOCKED (partial)** — the bundled researcher/verifier/reviewer subagents
could not run (session not under tmux; spawns fail with "Subagents require
tmux"). The loop ran in lead-owned direct mode with the probe as the
deterministic benchmark. Host verification (probe 16/16, make test 57/57,
ROM build) and device verification (baseline-identical counters, flat
memory, no exceptions) are complete; a manual play test in Ares is the
remaining feel check.
