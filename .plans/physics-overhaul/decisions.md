# Decisions & assumptions

## D1: Physics is a sibling layer; tiny3d stays render-only

- **Context:** tiny3d ships zero collision API (verified: only AABB usage is
  culling, with explicit author warning at `tiny3d-main/src/t3d/t3dmath.h:472`:
  *"should only be used where this is acceptable (e.g. culling) and not for
  collision detection"*). Render BVH is opaque (no public ray query).
- **Decision:** new `src/user/gameplay/physics/` module. No upstream tiny3d
  changes. No reuse of `t3d_model_bvh_*`.
- **Consequences:** duplicate BVH (one in `.t3dm` for render culling, one in
  `.colmesh` for collision). Accepted — ~150 KB per level.
- **Alternatives rejected:** *Fork tiny3d to expose BVH ray query.* Lost
  because: couples our timeline to upstream, opaque struct layout, render BVH
  is object-level granularity (too coarse for swept-sphere narrow phase).

## D2: `.colmesh` is a sidecar, not a `.t3dm` chunk

- **Context:** user-confirmed via question gate. Two competing options were
  in-chunk vs sidecar.
- **Decision:** separate `.colmesh` file per level.
- **Consequences:** two artifacts to ship per level; importer pipeline grows
  a new tool (`tools/colmesh_bake/`). Easier to evolve format independently.
- **Alternatives rejected:** *Embed as `T3D_CHUNK_TYPE_COLLISION` in `.t3dm`.*
  Lost because: couples to upstream tiny3d format versioning; mixing render
  + collision in one file makes hot-reload of either harder.

## D3: `fm_vec3_t` is the canonical hot-path vec type

- **Context:** user-confirmed. Newer tiny3d examples (`24_hdr_bloom`) already
  use `fm_vec3_t` from libdragon fast math. Existing project code uses local
  `Vec3` (see `physics_contracts.hpp`).
- **Decision:** physics hot path uses `fm_vec3_t`. Project `Vec3` and
  `T3DVec3` are bridged by `vec_bridge.hpp` (layout-compat asserts +
  `reinterpret_cast`).
- **Consequences:** RSP-aware ops available. One bridge header to maintain.
- **Alternatives rejected:** *Keep project `Vec3`.* Lost because: misses
  libdragon fast-math intrinsics on the hottest functions.

## D4: 60 Hz fixed step with render interpolation

- **Context:** user-confirmed. OG Celeste 64 ticks at 60. N64 cannot render at
  60 reliably; render-rate is decoupled.
- **Decision:** accumulator pattern. Sample input once per render frame, run
  N motor ticks (cap 5/frame to avoid death spiral), render interpolates
  between `prev_position` and `position` using fractional alpha.
- **Consequences:** `PlayerState` gains `prev_position`. Render code reads
  interpolated transform.
- **Alternatives rejected:** *Variable dt.* Lost: tunnelling at swept-sphere
  speeds, non-deterministic replay. *30 Hz.* Lost: parity with reference
  game feel.

## D5: Triangle mesh, not heightfield or convex hulls

- **Context:** levels are authored as glTF meshes; OG Celeste 64 uses tri
  mesh.
- **Decision:** triangle soup + BVH. No convex decomposition.
- **Consequences:** narrow phase is per-tri (Möller–Trumbore / Ericson 5.2.7);
  no SAT or GJK. Simpler, well-fit to character-vs-static-world.

## D6: Material flags live on triangles, not on a separate "surface" entity

- **Context:** bit-packing 5 flags into `uint16_t material` is cheap; querying
  "is climbable?" after a hit is one AND.
- **Decision:** bake material from glTF material-name suffix; store as
  `uint16_t` per triangle.
- **Consequences:** changing a wall's climbability requires re-bake. Accepted.
- **Alternatives rejected:** *Per-mesh material plus mesh-id-per-tri.* Lost:
  one extra indirection per query; same end size.

## D7: Per-PR cutover with dual substrate during migration

- **Context:** big-bang substrate swap risks weeks of broken gameplay.
- **Decision:** legacy `Collider[]` and new `CollMesh*` coexist on `Room`
  until Inc 7. Query funcs branch on `Room::use_collmesh`.
- **Consequences:** temporary code bloat in `world.cpp`. Removed in Inc 7.

## Assumptions resolved from code

- Existing `PlayerMotor` API surface is the right boundary for the rewrite.
  Source: code @ `src/user/gameplay/player/player_motor.hpp:18-32`. Keeping
  `MotorInput`/`MotorResult` lets us swap the substrate without touching
  state-machine callers.
- Coordinate adapter (src→port, +Z up → +Y up) already exists and works.
  Source: code @ `src/user/gameplay/physics_contracts.hpp:33-46`. Reuse;
  do not duplicate.
- `Room::kMaxColliders = 512` is a hard cap that levels are currently hitting.
  Source: code @ `src/user/gameplay/world/world.hpp:108`. Triangle mesh
  removes this cap entirely.
- `GroundHit`, `WallHit`, `CeilingHit` are the stable query return shapes
  consumed by player, camera, and climb code. Source: code @
  `src/user/gameplay/world/world.hpp:56-86`. Inc 4 preserves them exactly.
- libdragon fast math `fm_vec3_t` is available. Source: code @
  `tiny3d-main/examples/24_hdr_bloom/src/actors/base.h:15` confirms type
  presence in current libdragon.
- Importer reuses vendored BVH builder. Source: code @
  `tiny3d-main/tools/gltf_importer/src/lib/bvh/v2/`. SAH binned builder
  already linked into `gltf_importer` build; lift it into `colmesh_bake`.

## Open questions (from review)

- **BVH skip-pointer encoding reference.** The right-child = `idx +
  left_or_first` scheme is documented inline in `data-model.md §2` with
  traversal pseudocode, but is non-standard. Implementer should cross-check
  against Wald (2007) "On fast Construction of SAH-based Bounding Volume
  Hierarchies" or the vendored `tools/gltf_importer/src/lib/bvh/v2/` output
  format before writing the runtime traversal.
- **Quantized AABB dequantization cost.** ~72 K int16→float dequant ops per
  1 000-ray batch may be measurable on r4300. If profiling shows it hot,
  consider: (a) cache dequantized node AABBs in RDRAM once at load (8× size
  blow-up), or (b) constrain `quant_scale` to power-of-two so dequant is a
  shift.
- **DFS packaging.** Inc 2's Makefile rule must add `.colmesh` files to the
  DFS image alongside `.t3dm`. Verify `compile-rom.sh` honours new files
  before Inc 6b.
- **Climbable moving platforms.** A face that is both `0x0008 climbable`
  and listed in `SurfaceLink[]` is supported (material and ownership are
  independent lookups). Inc 6 motor MUST check both, not short-circuit on
  the first hit.
- **Thread safety.** Project is single-threaded gameplay loop (libdragon
  pattern; verified by reading `gameplay_scene.cpp`). `CollMesh` is
  immutable post-load. No locks required. Recorded here to close the
  question.
