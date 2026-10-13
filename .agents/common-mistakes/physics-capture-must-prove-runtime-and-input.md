# Physics capture must prove runtime health and raw input

## Symptom

A scripted `[phys-test] result=PASS` coexists with a missing map, renderer
errors, stale build marker, or unintended climb entry.

## Cause

Treating one aggregate physics line as proof ignores which ROM ran, whether the
real map/render path loaded, and whether raw controller shoulders were held.
An autoplay route can exercise physics while the visual package is broken;
stuck Z/L/R can make manual climb observations invalid.

## Fix

Capture `[artifact]` SHA and `[build]` marker with startup, map, catalog,
renderer, input, and physics telemetry. Require map/collision/catalog/texture
success, nonzero near-render counters, zero raw shoulder/analog samples, and
per-metric route gates before calling a capture qualified. Use
`tools/capture_physics_autoplay.sh`; it refuses to compete with a live Ares.
Validate every recorded SHA as exactly 64 hexadecimal characters and compare
it with `sha256sum` of the captured ROM file. A copied or truncated hash can
make the verifier reject valid evidence or identify the wrong artifact.

Keep mutually exclusive mechanics in independent capture profiles. A terminal
PASS for the wall-jump route must not imply mantle coverage; each verifier
profile must require its own exit event and its profile marker in the result.
