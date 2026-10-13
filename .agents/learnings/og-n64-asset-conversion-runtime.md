# OG-to-N64 asset conversion runtime

## Context

Implemented manifest-driven OG model/texture conversion, tiny3d animation
runtime, and staged WAV64 event playback for the Celeste 64 demake.

## Hardest decision

N64 RDP repeat requires power-of-two tile dimensions. Rectangular world texture
profiles therefore preserve repeat per axis only when that axis is power-of-two;
non-POT axes emit explicit clamp metadata and renderer texparms, avoiding boot
asserts while retaining source-to-output UV scale.

## Alternatives rejected

- Padding rectangular tiles to 32x32 — repeats padding pixels and changes authored
  pattern; per-axis clamp is deterministic and visible in `.matmeta`.
- Parsing FMOD banks — source checkout has no licensed/exported sample boundary;
  keep WAV export manual and fail-closed until approved files exist.
- Silent cube fallback for player — required animated model load now reports fatal
  and draws no substitute, exposing packaging/path failures.

## Least confident

Ares proved player model/clip loading and stable ~30 fps profiler output, but no
visual frame capture or long 3600-frame memory sample was retained. Audio remains
unqualified until external dash/music WAV exports are supplied.

## Reuse

Read when changing `tools/asset_manifest.py`, `.matmeta` wrap policy,
`TexturedRoomRenderer`, tiny3d stream packaging, or `AudioService` event activation.

## 2026-09-24 static-player follow-up

### Hardest decision

Keep legacy model preparation unskinned while making only `static_player`
manifest entries invoke Blender. Otherwise ordinary unskinned props were sent
through animated qualification and failed on missing skeleton joints.

### Alternatives rejected

- Force every model through static-player preparation — breaks existing
  unskinned prop conversion; legacy `static_model` entries remain direct GLB →
  T3DM.
- Leave old `player.*.sdata`/`player.animmap` in `filesystem/mdl` — `mkdfs`
  packs the whole directory, so stale sidecars remain active despite manifest
  `package=false`; move them outside DFS staging.

### Least confident

Ares proved both static ROM paths load and provided stable profiler/memory
telemetry, but this shell had no capturable display; visual spawn/movement
confirmation still needs interactive screenshot review.

### Reuse

Read before changing static player packaging, `StaticModel::Load`, or
reactivating future animation artifacts.
