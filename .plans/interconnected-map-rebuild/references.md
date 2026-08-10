# Pipeline archaeology

This file records how the existing `.plans/` documents relate to the rebuild
plan. “Done” below means the plan claims completion; it does not override the
current code/tests evidence.

| Plan | Intended scope | What survives | Why it is not the final full-world plan |
|---|---|---|---|
| `convert-og-1-1-map` | First OG `1-1.map` filtering, transform, entities, and single-room colmesh | Early class/filter assumptions and spawn transform | Targets the small B-side and predates the canonical library/full A-side scope |
| `og-1-1-conversion` | Death brushes, cassette entity, conversion report for `1-1.map` | Hazard/entity policy ideas | Still single-room B-side work; no interconnected world |
| `og-map-normalizer` | Normalize OG classes into a general baker input | Historical reason to avoid class normalization | Superseded: normalization loses source semantics and creates a fragile chain |
| `og-map-baker-rewrite` | Direct OG-aware monolithic baker | Direct-map/class-aware design and explicit material policy | Its proposed monolith was superseded by the library-first implementation |
| `og-map-pipeline-v3` | Library-first IR, shared class registry, LVL/colmesh/T3DM/NAV artifacts | `ogmap_lib`, class registry, shared writers, quantized-BVH fix | Designed primarily around `1-1`; T3DM remains incomplete/deferred |
| `og-map-pipeline-final` | Consolidate the stable single-room pipeline | `tools/bake.py`, writers, reports, parity tests | Produces the small-room path; not the full A-side runtime |
| `t3dm-room-renderer` | GLB → T3DM visual renderer cutover | Future visual direction | Explicitly deferred and not required for collision-first traversal |
| `whole-interconnected-map` | Full A-side `1.map` grid chunks, map-pack, runtime, save, cassette tail | Map-pack concept, 2D world-XZ convention, runtime requirements | Marked done too early; host tests cover isolated transitions, not ROM traversal |
| `interconnected-map-fixup` | Repair wrong-axis chunks, boot spawn, stale colmesh reuse | Correct world-XZ `cell_of`, 47-room/1200 bake, start-spawn and clean-bake guards | Fixes partition/boot history but not the scene's competing active-room state |
| `open-world-conversion` | Per-chunk winding/ABI audit and telemetry | Host winding audit and diagnostic direction | Host collision already passes; the remaining failure is broader runtime/ownership integration |

## Evidence captured during review

- Source: `assets/og_converted/maps/1.map` — 706 entities, 1182 brushes,
  8545 source faces, 32 textures.
- Current visual bake: 47 cells at chunk size 1200; 7921 LVL faces and 30436
  LVL vertices; maximum current chunk is `cell_n01_n02` at 891 faces / 3412
  vertices.
- Current global colmesh prototype: 20824 vertices, 10596 triangles, 8191 BVH
  nodes, 383224 bytes on disk. This is the basis for the Inc 5 hardware memory
  gate.
- Current per-room colmesh inventory: 43 files; four visual rooms have no local
  colmesh. The no-local-colmesh condition is not a failure in the rebuild
  because static collision becomes global.
- Current manifest graph: 46 rooms are reachable from `cell_00_00`; the
  isolated `cell_04_23` is a single outlying `SpikeBlock` and must be explicitly
  classified rather than counted as traversable path.
- Host tests pass for map-pack generation/loading and the existing colmesh
  audit. They do not exercise the full `GameplayScene` path or actual emulator
  traversal.
- Current runtime evidence: `GameplayScene::Update` uses `impl_->room` for
  motor/camera/respawn/moving surfaces, while `Map::LoadSlot` owns a different
  per-room renderer and `GameplayScene::Render` draws the legacy renderer.

