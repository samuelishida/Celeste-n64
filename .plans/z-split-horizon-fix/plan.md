# Z-Split Horizon Fix

## Context

The N64 renderer splits the world into a **near pass** (Z-on, detailed resident
ring) and a **distant pass** (Z-off, coarse per-cell LOD meshes, back-to-front).
On device the split is visually wrong: the near↔distant handoff does not sit at
the horizon. Distant coarse slabs appear **too close**, overlapping the detailed
near ring; the transition reads as a hard, non-horizon-aligned seam; and when
the camera faces the wrong direction the distant pass overdraws and VPS
collapses (observed 58→18).

There are **two coupled root causes**:

1. **Wrong cull values.** `distant_cam.near/far` (from `near.far*0.25*lod_scale`
   and `tile_size*1.4`) are compressed-LOD-space values fed into
   `CellInDistantFrustum`, which compares **world-space** cell-center distances.
   With `tile_size=240`, `near.far=800`: `near=50` (inside the resident ring),
   `far=336` (~1.4 cells out). The distant pass double-draws near-field cells and
   never reaches the real horizon.

2. **No separate distant projection.** `arch.md` §5 requires the distant pass to
   use a separate projection/viewport configuration. The codebase sets the
   viewport projection **once** at `gameplay_scene.cpp:826` with near=20/far=800
   and **never re-attaches it** for the distant pass. So even if the cull is
   fixed to keep far cells, those cells' vertices are projected through the
   near-pass perspective matrix (far=800) and **clipped beyond 800** — the
   horizon stays capped at 800 regardless of the cull range. Fog
   (`t3d_fog_set_range`) also operates in this projection's depth space, so the
   fog range must be re-derived when the projection changes.

The fix: give the distant pass both a correct **world-space cull range** AND a
**separate viewport projection** (near just past the ring, far = full map
diagonal), then restore the near projection before the high-priority pass.
Re-derive fog for the distant projection's depth space.

## Architectural decisions

- **Decision: give the distant pass its own viewport projection (arch.md §5).**
  Before `RenderDistant`, attach a viewport with the distant near/far; before
  `RenderHighPriority`, re-attach the near viewport (20..800). Both passes keep
  the camera-at-origin look-at (model matrices are camera-relative — see
  `gameplay_scene.cpp:830-840`). This is the load-bearing fix: without it,
  widening the cull far has no visual effect past 800. Alternatives rejected:
  keep the shared projection (the horizon stays capped at 800 — the bug remains).
- **Decision: world-space cull near/far.** `near = 1.5 × tile_size` (just past
  the resident ring's diagonal neighbors at `√2 × tile_size ≈ 339.4` for the
  Forsaken City grid). `far = MapFarClipDistance(world_bounds, margin)` = the
  full map diagonal from any camera position (`2 × map-center→corner × margin`),
  so a camera at a map corner still sees the opposite corner. The cull
  predicate and the projection use the **same** near/far values.
  **2D-vs-3D note:** `MapFarClipDistance` and `CellInDistantFrustum` use XZ-plane
  distance (`sqrt(dx²+dz²)`), but the perspective far plane clips by 3D
  camera-space depth. The camera is assumed near the map plane (Y small relative
  to XZ), so 2D≈3D. If the camera climbs high, add a vertical margin term to
  `far` or switch the cull to 3D distance.
- **Decision: fog re-derived for the distant projection, accounting for the
  `kFogMaxMinDistance` clamp.** `t3d_fog_set_range` operates in the RSP's
  projected depth space (perspective depth values), not world distance. The
  distant projection's depth space differs from the near one, so fog `min/max`
  must be re-derived as fractions of the distant far plane. **Caveat:**
  `MakeFog` clamps `min` to `kFogMaxMinDistance = 1000.0f` (`fog_math.hpp:21`).
  For Forsaken City (`distant_far ≈ 2700`), `far*0.4 ≈ 1080` exceeds 1000 and
  would be clamped to 1000 — so the re-derivation must either (a) raise
  `kFogMaxMinDistance` in Inc 3, or (b) pick a `min` fraction that stays under
  1000 (e.g. `far*0.35 ≈ 945`). The plan takes (a): raise the clamp to
  `distant_far` (or remove it) so the fog onset tracks the projection.
- **Decision: keep near ring at 9.** The bug is the split, not the ring.
- **Decision: `lod_scale` becomes dead in `MakeDistantCamera`** after the fix
  (the near plane no longer uses it). Retain the parameter in the signature for
  the single caller's stability but document it as retained for future
  compressed-coordinate projection work; do not remove to avoid churn.

## Assumptions and answers from code

- `tile_size = chunk_size * scale` in world units. Source: `gameplay_scene.cpp:909-914`
  and `mappack_loader.hpp` (`scale=0.2`, `chunk_size=1200` → `tile_size=240`).
- **The viewport projection is set exactly once**, at `gameplay_scene.cpp:826`
  (`t3d_viewport_set_projection(..., 20.0f, 800.0f)`), then `look_at` (camera-at-origin)
  at `:840`, then `attach` at `:850`. No renderer re-attaches it. Source: grep
  across `src/user/gameplay/` — only `gameplay_scene.cpp` calls these.
- `distant_cam.near/far` are consumed **only** by `CellInDistantFrustum` (cull).
  They are NOT GPU clip planes today. Source: `distant_world_renderer.cpp:132-151`.
- The distant mesh vertex packing is camera-relative + world-correct via
  `kLodScale` and the model matrix. Source: `lvl_room_renderer.cpp:216-283`
  (`SetCameraPosition` rebases by `render_origin - camera_pos`).
- The t3d viewport API: `set_projection` sets `matProj` + marks dirty; `look_at`
  recomputes `matCamProj` from the current `matProj` + view; `attach`
  **unconditionally** pushes both `matProj` and `matCamera` to the RSP (it does
  NOT consult the dirty flag). So the switch order is mandatory:
  `set_projection(distant near/far)` → `look_at(camera-at-origin)` → `attach`.
  Skipping `look_at` would push the new projection with the **stale** camera
  matrix (the dirty flag does not rescue you — `attach` always emits). Source:
  `tiny3d-main/src/t3d/t3d.c:498-620`.
- **Camera-at-origin coupling is load-bearing for the switch.** The near setup
  at `gameplay_scene.cpp:826-840` uses `view_origin = {0,0,0}` and
  `view_target = camera_target - camera_position` (NOT `camera_target` directly)
  because model matrices are camera-relative (`SetCameraPosition` rebases by
  `render_origin - camera_pos`). The distant pass must use the **same** coupling
  or geometry double-offsets and pops. The switch reconstructs this from the
  `CameraDesc` (which holds world-space `pos`/`target`):
  `view_origin = {0,0,0}`, `view_target = {cam.target - cam.pos}`,
  `up = {0,1,0}`. Source: `gameplay_scene.cpp:830-840`.
- The skybox is drawn inside `RenderDistant` (before `distant_->Render`), with
  Z-off and a 2000-unit cube. Under the distant projection it must still render
  (Z-off ignores depth); the skybox uses the model matrix only, no near/far
  dependence. Source: `skybox.cpp:85-118`.
- World AABB is on every `V2RoomSpec.world_aabb`; `map_runtime_.Spec()` exposes
  the room table. Source: `mappack_loader.hpp`, `map_runtime.cpp`.
- `MakeDistantCamera` / `BuildPassCameras` have a single production caller
  (`GameplayScene::Render`). Source: grep.

## Risks accepted

- **Distant projection overdraw.** Widening the projection far to the full map
  diagonal means more cells rasterize (not just survive culling). Mitigation:
  the horizontal cone cull still bounds in-cone cells; fog hides the far edge.
  Must stay ≤ 12 ms distant budget. If exceeded, reduce `far_margin` or add a
  distance² cutoff in `BuildDistantRenderListCulled`.
- **Viewport switch cost.** Two extra `set_projection` + `look_at` + `attach`
  calls per frame. These are cheap (matrix math + a few RSPQ commands) vs. the
  draw cost; accept.
- **Fog depth-space mismatch.** If fog is tuned to the wrong projection's depth
  space, the fade stops short or never reaches full opacity. Mitigation: Inc 2
  retunes fog explicitly for the distant projection and verifies on device.
- **Ring-boundary flicker** at `near ≈ 1.5×tile_size` from float rounding.
  Mitigation: `1.5×` is margin past `√2`; revisit with an epsilon only if a real
  flicker appears.

## Increment DAG

- Inc 1 — World-space cull near/far + host tests (M) — depends on: none — unblocks: 2
- Inc 2 — Distant viewport projection switch (M) — depends on: 1 — unblocks: 3
- Inc 3 — Fog re-derivation + device validation + tuning (S) — depends on: 2 — unblocks: none

## Increments

### Inc 1 — World-space cull near/far + host tests (M)

**Depends on:** none
**Unblocks:** 2
**Status:** done (22/22 host tests green incl. updated pass_camera_math + distant_cull_contract; ROM builds clean, no warnings)
**Done criteria:** the distant cull uses world-space `near` (just past the ring)
and `far` (full map diagonal). Host test `distant_cull_contract` updated to the
new values and green. ROM builds clean. **Not visually verifiable alone** — Inc 1
widens the cull but the projection is still capped at 800 until Inc 2, so cells
in `[800, far]` pass the cull but clip. Inc 1 ships on the strength of host tests
+ clean build; the visual fix lands in Inc 2.

Fix the cull-predicate values only. These are interdependent — changing `far`
alone while leaving `near=50` would cull-keep every cell from the near field to
the map extent, far worse than the bug.

#### Files to touch

##### src/user/gameplay/render/pass_camera_math.hpp
- What changes: rewrite `MakeDistantCamera` so `near`/`far` are world-space cull
  distances. `lod_scale` is retained in the signature but no longer affects
  `near` (documented as retained for future compressed-projection work).
- Function(s):
  ```cpp
  inline CameraDesc MakeDistantCamera(const CameraDesc& near,
                                      float tile_size, float lod_scale,
                                      const AABB* world_bounds,
                                      float near_margin = 1.5f,
                                      float far_margin = 1.15f) {
      CameraDesc c;
      c.fov_deg = near.fov_deg;
      c.near = tile_size * near_margin;              // world-space ring edge
      c.far  = MapFarClipDistance(world_bounds, far_margin); // full map diagonal
      c.pos = near.pos; c.target = near.target; c.up = near.up;
      return c;
  }
  ```
- Data shapes: `const AABB* world_bounds` (nullable → fallback far
  `tile_size * 16.0f`), `near_margin` (default 1.5), `far_margin` (default 1.15).
- Integration points: called by `BuildPassCameras`; thread `world_bounds` there.
- Error paths: null bounds / zero extent → fallback far; `tile_size <= 0` →
  invalid camera (`ValidateDistantCamera` catches it).

##### src/user/gameplay/render/lod_math.hpp
- What changes: add a host-testable map-extent helper (renamed from the review's
  `MapDiagonalRadius` to make the semantics clear — it returns the worst-case
  camera→cell distance, i.e. the full diagonal × margin, not a radius).
- Function(s):
  ```cpp
  // Worst-case camera→cell distance = full map diagonal × margin.
  // Used as the distant pass far clip (cull + projection). Covers a camera at
  // any map corner seeing the opposite corner.
  inline float MapFarClipDistance(const AABB* bounds, float margin = 1.15f) {
      if (!bounds) return 0.0f;
      const float cx = (bounds->min.x + bounds->max.x) * 0.5f;
      const float cz = (bounds->min.z + bounds->max.z) * 0.5f;
      const float dx = std::max(std::fabs(bounds->min.x - cx),
                                std::fabs(bounds->max.x - cx));
      const float dz = std::max(std::fabs(bounds->min.z - cz),
                                std::fabs(bounds->max.z - cz));
      return 2.0f * std::sqrt(dx*dx + dz*dz) * margin;  // diameter × margin
  }
  // Union of room AABBs (world XZ extent). Returns an empty AABB if count==0.
  inline AABB UnionRoomsAABB(const V2RoomSpec* rooms, int count) {
      AABB u = {{0,0,0},{0,0,0}};
      if (!rooms || count <= 0) return u;
      u = rooms[0].world_aabb;
      for (int i = 1; i < count; ++i) {
          u.min.x = std::min(u.min.x, rooms[i].world_aabb.min.x);
          u.min.z = std::min(u.min.z, rooms[i].world_aabb.min.z);
          u.max.x = std::max(u.max.x, rooms[i].world_aabb.max.x);
          u.max.z = std::max(u.max.z, rooms[i].world_aabb.max.z);
      }
      return u;
  }
  ```

##### src/user/gameplay/render/open_world_renderer.hpp
- What changes: `BuildPassCameras` forwards a `const AABB* world_bounds` (nullable,
  default nullptr) to `MakeDistantCamera`.
- Function(s):
  ```cpp
  inline PassCameras BuildPassCameras(..., float tile_size, float lod_scale,
                                      const AABB* world_bounds = nullptr) {
      ...
      p.distant_cam = MakeDistantCamera(p.near_cam, tile_size, lod_scale,
                                        world_bounds);
  }
  ```

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: union `map_runtime_.Spec().rooms` AABBs (via `UnionRoomsAABB`)
  and pass to `BuildPassCameras`. Only the `BuildPassCameras(...)` call at
  `:909` changes; nothing else.
- Error paths: `room_count == 0` → pass nullptr → fallback far.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: no logic change (it already reads `cam.near/far` for the cull).
  Update the comment at `:132-133` to state the cull range is world-space
  (ring edge → map diagonal).

##### tests/distant_cull_contract.cpp
- What changes: update `near_d`/`far_d` to world-space values. Add cases: a cell
  inside the ring diagonal (`dist < near`) is culled; a cell just past the ring
  edge is kept; a far cell near the map diagonal is kept; a cell beyond `far` is
  culled. **Also audit any `MakeDistantCamera` calls in `tests/pass_camera_math*`
  that pass `huge_lod`/`0.0f` lod_scale to assert invalid-range behavior** —
  after this fix `lod_scale` no longer affects `near`, so those cases may no
  longer test what they claim; update them to exercise `tile_size=0` instead.

#### Edge cases
- Null/zero world bounds → fallback far (never empty distant pass).
- Single-cell map → far ≈ 2×radius ≈ tile_size; distant pass may be empty.
- Camera at a map corner → `far = 2×radius×margin` still covers the far diagonal.

#### Verification
- Run: `g++ -std=c++17 -Isrc/user tests/distant_cull_contract.cpp -o /tmp/dc && /tmp/dc`,
  `./tests/run_host_tests.sh`, `./compile-rom.sh`.
- Tests to add/update: `distant_cull_contract.cpp`.
- Done: host tests green; cull range is world-space ring→map-diagonal. ROM builds.

### Inc 2 — Distant viewport projection switch (M)

**Depends on:** 1 (uses the new `distant_cam.near/far` as actual clip planes)
**Unblocks:** 3
**Status:** done (ROM builds clean; Ares visual smoke shows distant geometry
rendering — 19k metal-color hits in a captured frame; game runs at normal idle
animation rate; the 0.1 fps profiler reading is the Ares defocus-pause wall-clock
artifact, not a regression)
**Done criteria:** the distant pass draws under its own projection
(near=ring edge, far=map diagonal); the near pass restores the 20..800
projection. Ares visual smoke shows the horizon reaching the map extent (not
capped at 800), no near-field distant slabs, no VPS collapse. No new host test
(N64-only change).

This is the load-bearing fix for the visual bug. Without it, Inc 1's widened
cull keeps far cells but they clip at the near projection's 800 far plane.

#### Files to touch

##### src/user/gameplay/render/open_world_renderer.hpp
- What changes: `OpenWorldRenderer` gains a `T3DViewport*` member (set from
  `GameplayScene`) so it can switch projections mid-frame. Add `SetViewport()`.
  The header stays host-safe by forward-declaring `T3DViewport` (it's an opaque
  pointer; the `.cpp` includes `<t3d/t3d.h>`).
- Function(s):
  ```cpp
  void SetViewport(T3DViewport* viewport);  // set once from GameplayScene
  ```
- Data shapes: add `T3DViewport* viewport_ = nullptr;` member.
- Integration points: `GameplayScene::Render` calls `SetViewport(&impl_->viewport)`
  before `Render(cams)`.

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: `RenderDistant` switches the viewport to the distant projection
  (camera-at-origin look_at, same as near), then `RenderHighPriority` restores
  the near projection. The skybox draws under the distant projection (Z-off,
  no near/far dependence — safe).
- Function(s):
  ```cpp
  void OpenWorldRenderer::RenderDistant(const CameraDesc& cam) {
      if (viewport_) {
          // Distant projection: near/far from the distant camera (world-space).
          // Camera-at-origin look_at matches the camera-relative model matrices
          // (same coupling as the near pass — see gameplay_scene.cpp:830-840).
          t3d_viewport_set_projection(viewport_, T3D_DEG_TO_RAD(cam.fov_deg),
                                      cam.near, cam.far);
          const T3DVec3 origin = {{0,0,0}};
          const T3DVec3 target = {{cam.target.x - cam.pos.x,
                                   cam.target.y - cam.pos.y,
                                   cam.target.z - cam.pos.z}};
          const T3DVec3 up = {{0.0f, 1.0f, 0.0f}};
          t3d_viewport_look_at(viewport_, &origin, &target, &up);
          t3d_viewport_attach(viewport_);
      }
      skybox_->Draw(cam);
      distant_->UpdateCamera(cam.pos, cam);
      distant_->Render(cam);
  }
  void OpenWorldRenderer::RenderHighPriority(const CameraDesc& cam) {
      if (viewport_) {
          // Restore near projection (20..800). Camera-at-origin look_at.
          t3d_viewport_set_projection(viewport_, T3D_DEG_TO_RAD(cam.fov_deg),
                                      cam.near, cam.far);
          const T3DVec3 origin = {{0,0,0}};
          const T3DVec3 target = {{cam.target.x - cam.pos.x,
                                   cam.target.y - cam.pos.y,
                                   cam.target.z - cam.pos.z}};
          const T3DVec3 up = {{0.0f, 1.0f, 0.0f}};
          t3d_viewport_look_at(viewport_, &origin, &target, &up);
          t3d_viewport_attach(viewport_);
      }
      tile_streamer_->DrawHighPriority(cam);
  }
  ```
- Integration points: `Render` calls these in order; `RenderLowPriority` runs
  under the distant projection (acceptable — it's a no-op today; revisit when it
  draws water).
- Error paths: `viewport_ == nullptr` → skip the switch (host tests / fallback).
  `cam.near >= cam.far` → `ValidateDistantCamera` already guards; skip the distant
  projection switch (draw nothing distant).

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: after creating `cams`, call `impl_->open_world_.SetViewport(&impl_->viewport)`
  once (can be at init or per-frame before `Render`). The existing
  `t3d_viewport_set_projection(..., 20.0f, 800.0f)` + `look_at` + `attach` at
  `:826-850` stays as the initial near setup; `RenderHighPriority` re-establishes
  it after the distant pass.
- Integration points: the `open_world_.Render(cams)` call at `:917`.
- Note: `RenderLowPriority` currently runs between distant and high-priority
  under the distant projection. It's a no-op today; consider having it
  defensively re-attach the near projection even as a no-op so the first real
  use (water) doesn't silently render under the wrong clip planes. At minimum
  document this in the code comment.

#### Edge cases
- `viewport_ == nullptr` (host tests) → no projection switch; cull still works.
- Distant `near >= far` (tiny map) → `ValidateDistantCamera` false → skip distant
  projection (no distant draw).
- Skybox under distant projection: Z-off, 2000-unit cube — renders fine (no depth
  clip dependence).

#### Verification
- Run: `./compile-rom.sh` (no new host test — N64-only change).
- Ares visual smoke: boot `madeline_cube_rom.z64`; confirm the horizon reaches
  the map extent (distant cells visible past 800 world units), no near-field
  distant slabs, no VPS collapse on a 360° turn.
- Done: horizon reaches map extent; near pass still renders the ring correctly.

### Inc 3 — Fog re-derivation + device validation + tuning (S)

**Depends on:** 2 (the distant projection is now attached)
**Unblocks:** none
**Status:** done (ROM builds clean; 22/22 host tests green incl. updated
fog_math; Ares device smoke shows distant geometry rendering + game animating
normally; the 0.0 fps profiler reading is the Ares defocus-pause wall-clock
artifact, not a regression)
**Done criteria:** fog fades the distant horizon to atmosphere across the full
map under the distant projection; on-device (Ares) the horizon is a smooth fog
fade with no seam, no near-field distant slabs, no VPS collapse; distant
phase ≤ 12 ms.

#### Files to touch

##### src/user/gameplay/render/fog_math.hpp
- What changes: raise `kFogMaxMinDistance` so the fog onset can track the
  distant far plane. Currently 1000; for `distant_far ≈ 2700`, `far*0.4 ≈ 1080`
  would clamp. Set `kFogMaxMinDistance = 4000.0f` (or remove the clamp and rely
  on the `min < max` validation). Document that the clamp existed to prevent
  RDP issues with very high fog starts; keep a sane upper bound.
- Function(s): the constant only.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: re-derive the fog `min/max` for the distant projection's depth
  space. Fog operates in RSP projected depth (not world distance), so the range
  must be fractions of the distant far plane. Replace the current
  `MakeFog(300.0f, 1200.0f, ...)` with values derived from the distant far,
  e.g. `MakeFog(distant_far * 0.4f, distant_far * 0.9f, ...)`. Tune on device.
  **The fog value is static-by-design** (derived from static world_bounds); if
  `MapFarClipDistance` ever becomes dynamic (moving maps), move the fog setup
  into the per-frame distant render with the current `distant_cam.far`.
- Integration points: the `MakeFog(...)` call at `gameplay_scene.cpp:399`.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: tune `kCullMargin` (1.15) if the cone is too tight/loose at the
  new far extent. No structural change unless tuning fails.

##### docs/perf_budget.md
- What changes: update the distant-phase tuning notes: far plane = full map
  diagonal (separate projection), near plane = ring edge, fog = projection-depth
  fractions.

#### Edge cases
- Fog stops short of the horizon → increase `max` fraction (toward 1.0).
- Fog never reaches full opacity → ensure `max` < distant `far` (fog completes
  before the clip).
- Distant overdraw > 12 ms → add a distance² cutoff in
  `BuildDistantRenderListCulled` or reduce `far_margin`.

#### Verification
- Run: boot `madeline_cube_rom.z64` in Ares (per `AGENTS.md` launch command).
- Walk the map; confirm: no seam at the ring boundary, distant horizon fades to
  fog across the full map, no near-field distant slabs, 30 fps, no VPS collapse
  on a 360° turn. Cross-check on Mupen64Plus.
- Done: visual smoke passes + `distant` phase ≤ 12 ms per `docs/perf_budget.md`.

## Cross-cutting verification

- Every increment re-runs `./tests/run_host_tests.sh` (all host tests green) and
  `./compile-rom.sh` (clean build, no warnings).
- Ares visual smoke after Inc 2: horizon reaches the map extent (past 800), no
  near-field distant slabs, no popping on a 360° turn.
- Full device walk (after Inc 3): start at `cell_00_00`, walk the whole map;
  confirm no seam, no Z-fighting at the ring boundary, no popping, 30 fps.
- Ares timing is a proxy — cross-check on Mupen64Plus or real hardware before
  closing.

## Standards / common-mistakes referenced

- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: none new (no new
  rom:/ paths).
- `.agents/common-mistakes/missing-player-start-init.md` — applies to: none.
- `.agents/common-mistakes/camera-respawn-reset.md` — applies to: the distant
  projection is re-attached every frame from the current camera, so it stays
  correct across respawn (no fixed camera origin assumed).

## Open questions (CONSIDER from review)

- Derive `near` from ring geometry (`√2 × tile_size` from `kMaxRing`/`tile_size`)
  instead of the `1.5f` magic constant, so it stays correct if the ring changes.
- Derive the null-bounds far fallback (`tile_size * 16.0f`) from `tile_size ×
  max expected cells` rather than a magic constant.
- Add a small epsilon for hysteresis at the near boundary if a real ring-cell
  flicker appears in device validation.
- `RenderLowPriority` runs under the distant projection today (no-op); have it
  defensively re-attach the near projection so the first real use (water)
  doesn't render under the wrong clip planes.
- Whether to give the distant pass a buffered viewport
  (`t3d_viewport_create_buffered`) to avoid RSP race conditions on the matrix
  switch; the unbuffered shared viewport is fine for now (single-buffer sync).
- The `RenderHighPriority` projection restore is intra-frame only (the top of
  the next frame re-attaches near at `:826`); document so a future reorder
  doesn't drop it.

## Out of scope

- Expanding the near resident ring (`kMaxRing`).
- Changing the distant vertex packing / `kLodScale`.
- Adding a skybox texture (fog + flat dome remain).
- Per-direction distant meshes (all four slots still share one mesh).
- Compressed-coordinate distant projection (arch.md §6 `lod_scale` world
  transform) — the distant pass uses world-space coordinates with a widened
  far plane; the compressed-coordinate approach is future work if int16
  precision fails at the map diagonal.