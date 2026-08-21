# Tile streamer resident-set compaction (in-place overwrite bug)

## Context
Fixed two co-occurring defects — black/missing geometry block on every cell
transition + mid-map crash — in `TileStreamer::SetCenterImpl`
(`src/user/gameplay/render/tile_streamer.cpp`). Commit `013d57f`, branch
`revert-zsplit-keep-memory`. Regression test:
`tests/tile_streamer_compaction_smoke.cpp`.

## Hardest decision
Not the fix (small) but the diagnosis: the two symptoms looked like separate
render bugs. The judgment call was to trust the Ares telemetry (kept cells
being reloaded on every transition) over the render-math surface, and to model
the compaction algorithm in a host test with a *faithful* buggy-path model
before touching the ROM. The bug: the in-place compaction loop wrote
`set_.spec[n] = s` while `set_.IndexOf(s)` searched the same live array. New
center = `ring[0]` overwrote slot 0 (old center's slot); when the loop later
reached the old center (a kept neighbor), `IndexOf` failed → cell silently
dropped. One drop explains both defects: missing geometry AND an orphaned
renderer (heap leak → OOM crash on the ~90 KB free heap).

## Alternatives rejected
- Re-investigating the render-math surface (position packing, UV wrap, vertex
  loads, matrix, camera) — already exonerated by exhaustive host tests; the
  telemetry pointed at the streaming path, not the math.
- Treating the two symptoms as separate bugs (render fix + memory fix) —
  telemetry unified them into one root cause.
- A minimal patch re-searching after compaction — the in-place write order is
  fundamentally unsafe; the fix snapshots the old state
  (`old_spec[]`/`old_flat[]`/`old_tex[]`/`old_last_used[]`) and looks up kept
  cells against the immutable snapshot.
- A host-test model that assigns fresh renderers to not-found cells — the real
  code only keeps not-found cells present in `diff.load[]` (else silently
  dropped); the model must reproduce that gate or it won't reproduce the bug.

## Least confident
- The orphaned-renderer free path: I'm confident the cell was dropped, but the
  exact leak site (dropped cell's `TexturedRoomRenderer` never freed, and
  whether the LRU eviction path also leaks) was not independently verified on
  device. Watch `[memory] used` across many transitions — it must stay flat.
- Validation used sideways autowalk; every direction/edge combo on device was
  not stress-tested (host test covers a 4x3 grid walk incl. map edge).

## Reuse
- Any code that compacts/rewrites parallel arrays in place while searching
  them — snapshot first, then look up against the snapshot.
- Future streaming/residency work (ring changes, LRU, distant-horizon revival):
  apply the same pattern; keep `tests/tile_streamer_compaction_smoke.cpp`
  green.
- Diagnostic pattern: when "missing geometry" + "crash after walking" co-occur,
  check streaming telemetry (kept cells reloaded?) before touching render math.
