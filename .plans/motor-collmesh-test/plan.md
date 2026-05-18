# Fix player_motor_collmesh_test: Golden Regression

## Summary

`player_motor_collmesh_test.cpp` runs `RunTrace` twice against the same room (colmesh loaded both times) and compares the two identical result arrays — 100% agreement is guaranteed regardless of physics behavior. The test saves a `motor_baseline_trace_v1.bin` fixture but never compares against it. Restructure: one `RunTrace` call, load the existing baseline fixture and compare tick-by-tick (flags must match exactly; position within tolerance), write the baseline on first run if it doesn't exist. This makes the test catch motor regressions rather than proving arithmetic is reflexive.

## Files to touch

### tests/player_motor_collmesh_test.cpp

Remove the second `static TickCapture mesh[200]` and `RunTrace(room, trace, 200, mesh)` call. Also remove the 75%/80% threshold gate (lines 258–265) — it is incompatible with a golden regression.

Replace the `legacy[]`/`mesh[]` comparison loop with a baseline comparison: load `motor_baseline_trace_v1.bin`; if absent, write the current run as the baseline and exit 0 with `"baseline written — rerun to validate"`. If present, compare each tick: `grounded` and `wall_contact` must match exactly; x/z position within 0.1 units per axis for ticks 0–79 (same window as the existing code — y-position can diverge after landing due to accumulated float differences). Fail on the first non-matching tick with a message identifying the tick index and which fields differ.

The baseline save block at lines 176–185 already writes `"BASE"` + `uint16_t count` + `TickCapture[200]`. Add a matching `ReadBaseline` that reads `TickCapture` structs — **not** `TraceEntry` (a different struct, defined at line 21). Model `WriteBaseline`/`ReadBaseline` on the existing save block, not on `WriteTrace`/`ReadTrace` (which use `TraceEntry` and `MTRC` magic).

## Edge cases

- Baseline absent on first run: write it, exit 0 (`baseline written`). CI will fail on the *next* run if behavior changed, which is the correct first-run seed behavior.
- Baseline tick count mismatch (e.g. fixture from an older run with different trace length): print a warning naming expected vs. actual count, then overwrite and exit 0 (`baseline regenerated`).
- Y-position comparison skipped after tick 80: vertical trajectory diverges due to accumulated float differences between geometry representations; x/z are checked for ticks 0–79 only.

## Verification

- Run: `g++ -std=c++17 -Isrc/user tests/player_motor_collmesh_test.cpp <required TUs> -o /tmp/motor_test && /tmp/motor_test` (first run seeds baseline, second run validates).
- Done when: GIVEN `filesystem/lvl/1-1.lvl` and its colmesh exist, WHEN the binary is run twice in the same working directory, THEN the first run exits 0 with `baseline written` and the second run exits 0 with `[inc6] PASS`.
- Done when (regression): GIVEN a baseline exists and a motor change is applied, WHEN the binary runs, THEN it exits 1 with a diff message identifying the first failing tick.

## Decisions and assumptions

- Decision: position tolerance 0.1 units per axis (x/z only, ticks 0–79). Source: code @ `tests/player_motor_collmesh_test.cpp:207` (existing xz check uses 0.1; kept at the same validated threshold).
- Decision: exact match for `grounded` and `wall_contact` flags (no threshold). Source: user-confirmed golden regression intent — flags regressing is always a bug.
- Decision: baseline-absent first run seeds and exits 0 (not exits 1). Source: standard golden-test pattern; CI regenerates the fixture rather than failing on a missing file.
- Decision: keep `WriteBaseline`/`ReadBaseline` names distinct from `WriteTrace`/`ReadTrace` to avoid confusion. Source: code @ `tests/player_motor_collmesh_test.cpp:28–50` (existing trace I/O uses `MTRC` magic + `TraceEntry`; baseline uses `BASE` magic + `TickCapture`).
- Assumption: motor is deterministic given identical input trace and starting state on the same host binary. A failed second run without code changes indicates an uninitialized-memory or float-precision issue, not a regression. Source: default — not verified empirically; flag if the second run fails unexpectedly.

## Estimated scope

S

## Open questions (CONSIDER from review)

- The `wall_climbable` check (~line 239) sets `room.player_start.y -= 20.0f` after the run that already set it to `air_start` (`+20`). Net offset is zero by coincidence; fragile if the air_start setup changes.
- CI golden baseline: the baseline file written by first run must be committed to the repo (or uploaded as a persistent artifact) so it survives between CI runs. The plan assumes the fixture is already committed (`tests/fixtures/motor_baseline_trace_v1.bin` exists in working tree); if CI always starts from a clean checkout, the two-run seed pattern fails silently.
