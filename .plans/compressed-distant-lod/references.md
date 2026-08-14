# Compressed Distant LOD — references to learn from

## In-repo code to reuse (do not reinvent)

| Need | Where | Notes |
|---|---|---|
| Int16 vertex packing + pair padding | `src/user/gameplay/render/lvl_room_renderer.cpp:104-152` | `Load()` float32→int16 at `pos_scale`; `.dlod` skips this by baking pre-packed. |
| Run coalescing + RSPQ block capture | `src/user/gameplay/render/lvl_room_renderer.cpp:215-330`, `batch_coalesce.hpp` | `CoalesceBatches` + `SortFacesByMaterial`; `.dlod` is pre-grouped so no sort. |
| Per-direction selection (already written, dead) | `src/user/gameplay/render/lod_math.hpp:104-140` | `DirectionalMeshIndex` / `DirectionalIndexFromDelta` — host-testable; the plan's Inc 4 activates it. |
| Culling / drop / fog coupling | `src/user/gameplay/render/lod_math.hpp`, `docs/perf_budget.md` | `kDistantMaxDist2`, `CellInDistantFrustum`, `MapFarClipDistance` — unchanged. |
| Host-safe parse pattern (header-only + device glue) | `src/user/gameplay/render/batch_coalesce.hpp` | Pattern for `dlod_format.hpp` (pure) + `dlod_loader.cpp` (device). |
| LVL2 binary I/O (endianness, header) | `tools/lvl_format.py:1-44` | Big-endian convention the DLOD writer mirrors. |
| Bake stats/report | `tools/bake_interconnected_map.py:250-294` | Inc 1 extends the report dict with per-cell distant stats. |
| Pattern-A host test | `tests/distant_sort_contract.cpp`, `tests/renderer_memory_contract.cpp` | Template for `dlod_format_contract.cpp`, `directional_lod_contract.cpp`. |

## External reference (design source)

- **James Lambert, `lambertjamesd/n64brew2025`** — the overworld renderer this
  project's `arch.md` is based on:
  - `src/overworld/overworld_render.c` — distant LOD pass, per-direction
    `meshes[tile_dir]`, `overworld_lod_1_direction_index()` (§11-12 of
    `arch.md`).
  - `src/overworld/overworld_load.c` — loading LOD entries + children.
  - Direction switching rule: use the camera's own facing when the camera is
    very close to the tile (|delta| < 200 in its units) to avoid unstable
    selection around the center (arch.md §12) — the model for Inc 4's
    `kDirectionCloseThreshold`.

## Measurement references

- `docs/perf_budget.md` — phase budget, `[counters]`/`[distant-cells]`/
  `[memory]` semantics, Ares-timing caveat.
- `docs/room_artifact_contract.md:213` — global CMSH size (383 KB) used in the
  resident-memory math.
- `/memories/repo/perf-bottlenecks.md` — prior perf-fixup results (RSPQ block
  pool 300-450 KB, Inc 5 freed ~720 KB).
