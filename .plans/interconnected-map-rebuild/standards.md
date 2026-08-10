# Applicable guidance

- `AGENTS.md`: preserve gameplay/ROM separation, inspect coordinate-system
  boundaries, rebuild the ROM after N64-facing changes, and keep the public
  goal narrow enough to validate.
- `.agents/map-creation.md`: use the canonical Quake-to-world transform, keep
  entity IDs synchronized, respect fixed-point limits, and require all runtime
  artifacts.
- `.agents/common-mistakes/og-map-polygon-winding.md`: validate transformed
  polygon winding and degenerate faces against the runtime query, not only the
  writer's local convention.
- `.agents/common-mistakes/dfs-path-prefix.md`: keep manifest paths aligned with
  the `filesystem/` tree and verify DFS packing.
- `.agents/common-mistakes/missing-player-start-init.md`: distinguish initial
  spawn, room transition carry, and checkpoint respawn.
- `.agents/common-mistakes/camera-respawn-reset.md`: reset the camera only after
  the checkpoint room is active.

