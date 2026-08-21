# Bugfix: Black Pillar / Mottled Black-White Rendering Artifact

**Map:** Forsaken City A-side `1.map` (45 cells, `--chunk-size 1200`, scale 0.2 → 240u cells)
**Status:** UV + tile-wrap fix applied & rebuilt (ROM boots clean). Remaining mottled
black/white artifact is a **positional z-split (near/distant handoff)** issue — under
active investigation.
**Last updated:** 2026-08-20

---

## 1. Symptom

A large, **angle-dependent black vertical structure** ("black pillar") appears in the
Forsaken City map. It is triggered by **walking sideways**, which changes which cells
enter the 3×3 near ring (and therefore which cells the near vs. distant pass draws).

- **Before UV fix:** the pillar was **pure black**.
- **After UV + tile-wrap fix:** the pure-black pillar became a **mottled black/white
  pattern** on the **upper (farther) portion** of a building. The mottling starts at a
  specific **distance** (upper part of building = farther from camera), consistent with
  the near/distant z-split boundary.
- The mottled black/white is **NOT** the texture palette (which is brownish, 36 distinct
  entries) — it is the signature of a **depth conflict / double-draw at the
  near/distant boundary**.

User diagnosis (verbatim): *"this is the positional z-split issue, take a look at that
code."*

---

## 2. Rendering architecture (z-split) — the code under investigation

Two-pass renderer. Per-frame pass order in `OpenWorldRenderer::Render`
(`src/user/gameplay/render/open_world_renderer.cpp`):

1. **distant** (Z **off**, skybox drawn FIRST, then distant cells with fog)
2. **low-priority** (Z off — currently a **no-op**)
3. **high-priority / near** (Z **on**, textured 3×3 resident ring)

Key orchestration facts:

- The **near-draw set is computed ONCE** at the top of `Render` via
  `tile_streamer_->CollectNearDrawSet(near_cam, ...)` (iterates the resident ring through
  `CellAabbInNearCone`), then passed to the distant pass via
  `distant_->SetNearDrawSet(...)` so the distant pass **skips exactly those cells**
  (no double-draw, no mid-cell cut).
- `RenderDistant(cam)`: attaches the **DISTANT** projection, `skybox_->Draw(cam)` first,
  then `distant_->UpdateCamera` + `distant_->Render`.
- `RenderHighPriority(cam)`: restores the **NEAR** projection (5..800), then
  `tile_streamer_->DrawHighPriority`.
- `AttachCameraAtOriginViewport(viewport, cam)`: `t3d_viewport_set_projection(fov, near,
  far)` → `look_at(origin, target - pos, up)` → `attach`. **Camera-at-origin coupling is
  load-bearing** — model matrices are camera-relative, so the view must also be
  camera-at-origin. Switch order is mandatory (set_projection → look_at → attach).

### Depth-buffer state (from grep)

- `gameplay_scene.cpp:1015-1016`: `t3d_screen_clear_color(...)` +
  `t3d_screen_clear_depth()` — the framebuffer + depth are cleared **once per frame**
  (before the passes).
- `distant_world_renderer.cpp:228`: `rdpq_mode_zbuf(false, false)` — distant pass turns
  Z **off** (no test, no write).
- `distant_world_renderer.cpp:340`: `rdpq_mode_zbuf(true, true)` — distant pass turns Z
  **back on** at the end.
- `skybox.cpp:92` / `:108`: same off→on pattern around the skybox draw.
- **Open question:** does `rdpq_mode_zbuf(test, write)` control test and write
  independently? Does the distant pass (Z off) leave the depth buffer in a state that
  conflicts with the near pass (Z on)? The depth buffer is cleared once per frame before
  all passes, and the distant pass does NOT write depth (Z off), so the near pass should
  start from a clean depth buffer — **but this needs confirmation against the actual
  per-frame clear ordering and the near pass's own zbuf setup.**

### Camera math (`src/user/gameplay/render/pass_camera_math.hpp`)

- `CameraDesc`: fov_deg=45, near=5, far=800 (near-pass defaults).
- `MakeNearCamera(fov, near_plane, far_plane, pos, target, up)`.
- `MakeDistantCamera(near, tile_size, lod_scale, world_bounds, near_margin=1.5·√2≈2.12,
  far_margin=1.15)`:
  - `c.near = tile_size * near_margin` → **≈ 508u** for a 240u cell (ring far edge).
  - `c.far = MapFarClipDistance(world_bounds, far_margin)` → full map diagonal.
  - fallback `far = tile_size * 16` if bounds null/zero.
- `ValidateDistantCamera`: `far > near && near > 0`.
- The distant near plane sits at the ring FAR EDGE so there is (by design) no
  gap/overlap between the near ring and the distant pass; the square ring boundary is
  meant to be hidden inside the fog ramp (fog onset ~370u).

### Cell geometry

- chunk size 1200, scale 0.2 → **cell size 240u**.
- Near ring = 3×3 cells. Corner cells at ~508u (1.5 × 240 × √2).
- Near clip far = 800. Distant near ≈ 508u, far = map diagonal. Fog onset ~370u.
- **Axis convention (load-bearing):** grid is 2D in WORLD XZ; +Y is up;
  world_z = depth = −map_y.

### The near-ring cull test (`CellAabbInNearCone`, `lod_math.hpp:230`)

- A resident cell is drawn by the near pass **iff its AABB intersects the camera cone**
  (horizontal FOV widened to 4:3 aspect, depth range near..far).
- Tests the **4 XZ corners** of the cell AABB; keeps the cell if **ANY** corner is within
  the cone AND within the depth range.
- **No grid-index cut** (the old grid-index gate was removed because it cut geometry at
  cell boundaries during rotation).
- `DrawHighPriority` (tile_streamer.cpp:268) draws **exactly** the resident cells passing
  this test (Pass 1 flat fallback, Pass 2 textured). With
  `kEnableGlobalMaterialGrouping` ON: per-frame triple list (material, cell, first_run,
  run_count), sorted by material, per material: `UploadMaterial(mat)` (sprite ONCE) +
  `DrawMaterialRun(...)`.
- `CollectNearDrawSet` (tile_streamer.cpp:251) uses the **same** `CellAabbInNearCone`
  test → the near-draw set and the distant-skip set are computed from the same predicate
  (one source of truth). **So the two passes are disjoint in cell-space** — a given cell
  is drawn by at most one pass. This rules out a *naive* whole-cell double-draw.

### Distant pass (`distant_world_renderer.cpp`)

- `Render(cam)`: computes hfov from vfov + 4:3; builds a culled + distance-ordered render
  list via `BuildDistantRenderListCulled(camera_pos, cam.target, entries, ..., hfov_deg,
  cam.near, cam.far, kCullMargin, kDistantMaxDist2)`.
- Z off (`rdpq_mode_zbuf(false, false)`), fog configured, sorted **back-to-front**
  (farthest first — painter's algorithm).
- ONE shared camera-relative matrix for the whole pass (verts packed against
  `shared_origin_` at `kLodScale`, matrix scale = 1/kLodScale).
- Per cell: **skips cells in the near-draw set** (`in_near_set` check), selects a
  directional mesh (`DirectionalMeshIndex`), draws via `mesh->DrawRunsDirect()`.

---

## 3. Root cause PART 1 — UV scale mismatch (FIXED)

**Cause:** LVL2 UVs are in **unwrapped TEXEL units** (1 unit = 1 texel, world-scale), but
the near textured renderer packed them as if they were 0..1 repeat units with
`u * 1024.0f`. tiny3d ST/UV packing is **s10.5** where **1 texel = 32 units**, so the
correct packing is `u * 32.0f`. The `* 1024` was **32× too large**, overflowing int16 on
~100% of UV components → garbage texel coords → black samples.

**Evidence (from actual data):**
- `filesystem/lvl/forsyken-city/cell_00_00.lvl`: u range −440..632, v range −544..576.
  Samples confirm **u = x × 5** (x=6.4→u=32, x=19.2→u=96). One 32-texel sprite = 6.4
  world units.
- `tools/ogmap_lib/texture_mapping.py::compute_uv` returns `u = vdot(point, axis_u)/scale_u
  + shift_u` on Quake-space points with unit axes → **world/texel units** (the
  "texture-repeat units" docstring is WRONG).
- `tools/writers/lvl_writer.py::build_lvl_faces` computes UVs BEFORE the game-space
  transform.

**Fix (applied, `src/user/gameplay/render/textured_room_renderer.cpp`):**
- `Load()`: `p.stA[0] = static_cast<int16_t>(va.u * 32.0f);` (was `* 1024.0f`) — 4 lines
  (stA/stB × s/t). Comment corrected to explain texel units + s10.5 + wrap.

---

## 4. Root cause PART 2 — tile clamp (FIXED)

**Cause:** Even with `* 32`, extreme unwrapped UV values (v up to ±20224 texels) overflow
int16. This is harmless **only if** the RDP wraps ST modulo the tile size. But the tile
was **CLAMPED**: the sprite has no embedded texparms and the upload passed `NULL` →
all-zero tileparms → `rdpq_set_tile` sets CLAMP on both axes (`_carg(parms->s.clamp |
(parms->s.mask == 0), 0x1, 9)`).

**Fix (applied, `textured_room_renderer.cpp`):**
- Added `kWrapTexparms` constexpr: `{ 0, 0, {0.0f, 0, REPEAT_INFINITE, false},
  {0.0f, 0, REPEAT_INFINITE, false} }` (forces tile wrap on both S and T).
- Wired `&kWrapTexparms` into **both** `rdpq_sprite_upload` calls:
  - line 475 (`EmitRunState`)
  - line 542 (`EmitBatchCommands`)

**Wrap math safety (confirmed):** tile = 32 texels = 1024 in s10.5; int16 wraps mod
65536; 65536 = 64×1024, so `(true_ST mod 65536) mod 1024 == true_ST mod 1024`. So IF the
tile wraps, the `* 32` fix is complete even for extreme UVs.

---

## 5. What has been RULED OUT (with evidence — do NOT re-litigate)

| Candidate | Why ruled out |
|---|---|
| Framebuffer clear | Sky blue (88,163,221) |
| Skybox | Sky blue |
| Fog | Light blue-grey |
| `material_color` | Non-black for all ids (0=0x968773FF, 1=0xEBF0FAFF, 2=0x6E6455FF, 3=0x788291FF, 4=0x5F5A55FF, default white) |
| Missing-sprite blackness | nullptr → flat fallback |
| Near flat-fallback combiner | PRIM×SHADE, white vertex color → non-black |
| Near textured combiner | TEX0×PRIM, white prim → non-black |
| Sprite size | 32×32 confirmed |
| Sprite palette | Brownish, 36 distinct entries (NOT pure black/white) |
| Naive whole-cell double-draw | Near-draw set and distant-skip set use the SAME `CellAabbInNearCone` predicate → disjoint in cell-space |

**Sprite palette detail** (`filesystem/tex/floor_dirty_concrete.sprite`): 32×32,
flags=0x82 (EXT + CI8), header 10 bytes `0020 0020 0082 0202 39d1`, data len 2174.
Palette (256×2 bytes CI16) at data[0:512]: first 8 = 0x3a11, 0x4211, 0x39d1, 0x39d1,
0x318d, 0x39d1, 0x4211, 0x420f; min/max 0x1..0x4a0f; **36 distinct brownish entries**.
Pixels at data[512:512+1024], idx range 1..209, 31 distinct.

---

## 6. Remaining issue — the z-split (ACTIVE)

After the UV + wrap fix, the pure-black pillar became a **mottled black/white pattern**
on the **upper (farther) portion** of a building, starting at a specific distance
consistent with the near/distant handoff.

**Key facts established:**
- The two passes are **disjoint in cell-space** (same cone predicate) → not a naive
  whole-cell double-draw.
- Depth buffer is cleared **once per frame** before all passes; distant pass is Z-off
  (no depth write); near pass is Z-on.
- The mottling is **black/white**, not the brownish palette → signature of a depth
  conflict or a pass-handoff / clip artifact, NOT a UV/palette issue.

**Open questions to resolve (the actual investigation):**
1. **Depth-buffer state across passes:** Does the distant pass (Z off) leave the depth
   buffer in a state that conflicts with the near pass (Z on)? Confirm
   `rdpq_mode_zbuf(test, write)` semantics in tiny3d and the exact per-frame clear
   ordering. Does the near pass re-enable zbuf correctly?
2. **Near far-clip (800) cutting a ring cell:** Does the near pass far-clip (800) cut any
   near-ring cell (corner cells at ~508u, but tall buildings extend well beyond the cell
   AABB in Y), leaving a gap that the distant pass fills — at a different depth — causing
   a conflict? The cone test uses the cell **AABB** (which includes full building height),
   but the near **clip plane** (far=800) is a hard depth cut that the AABB test does NOT
   account for.
3. **AABB-vs-clip mismatch:** `CellAabbInNearCone` keeps a cell if any XZ corner is in
   the cone + depth range, but it does **not** check whether the cell's **top** (high Y)
   is within the near far-plane (800). A tall building in a near-ring cell can have its
   upper geometry clipped by the near far-plane (800) while the cell is still "in the
   near set" (so the distant pass skips it) → the upper portion is drawn by NEITHER pass
   or by the near pass only up to 800u, with the distant pass skipping the whole cell.
   This is the most likely source of a **distance-dependent** artifact on the **upper**
   part of a building.
4. **Cone test depth range:** `CellAabbInNearCone` uses `near_d=5, far_d=800` (the near
   camera). Confirm the distant pass's cull (`BuildDistantRenderListCulled` with
   `cam.near≈508, cam.far=map diagonal`) and the near pass's clip (5..800) don't leave a
   **band** (508..800) where a cell is in the near set (distant skips it) but its
   geometry beyond 800 is clipped by the near pass.

**Most likely root-cause hypothesis (to verify):** The near-ring membership is decided by
a **2D XZ AABB-cone** test that ignores the near pass's **far clip plane (800)** and the
building's **height**. A tall building in a near-ring cell whose top extends past 800u is
clipped by the near pass, but the cell is still in the near-draw set so the distant pass
skips it entirely → the upper portion is missing/conflicting. Because ring membership
changes as you walk sideways, the artifact is angle/movement-dependent.

---

## 7. Files to read next (investigation queue)

- `src/user/gameplay/render/distant_world_renderer.cpp` (full) — `BuildDistantRenderListCulled`
  cull math, exact zbuf on/off, how the near-draw set skip is applied.
- `src/user/gameplay/render/tile_streamer.cpp` — `DrawHighPriority` depth flags, and
  whether the near pass sets `rdpq_mode_zbuf(true, true)` explicitly.
- `src/user/gameplay/render/textured_room_renderer.cpp` — confirm the near pass's
  `T3D_FLAG_DEPTH` and any explicit zbuf mode; whether near geometry beyond 800u is
  clipped.
- `src/user/gameplay/scene/gameplay_scene.cpp` (~1015) — the per-frame clear ordering and
  the near-pass drawflags setup.
- tiny3d `rdpq_mode_zbuf` semantics (test vs write) in
  `/tmp/n64-bootstrap/libdragon/include/rdpq.h` / tiny3d sources.

---

## 8. Build / verify workflow

- **Rebuild ROM:** `./compile-rom.sh` (per AGENTS.md: rebuild after N64-facing changes).
- **Boot-verify:** Ares (sole device emulator).
  ```sh
  LD_LIBRARY_PATH=/snap/ares-emulator/current/usr/lib/x86_64-linux-gnu \
    /snap/ares-emulator/current/usr/bin/ares --no-file-prompt madeline_cube_rom.z64
  ```
- **Telemetry capture:** `tools/capture_baseline.sh <rom> <seconds> <tag>` (launches Ares
  in background, polls USB-serial stdout, kills when done).
- **Host smoke test** (does NOT cover the textured renderer): see AGENTS.md
  "Local smoke test" for the g++ command.
- **Verify the fix on-device:** walk sideways in Forsaken City and confirm the mottled
  black/white pillar is gone.

---

## 9. Constraints / working rules (from AGENTS.md)

- Preserve the gameplay/ROM separation.
- Prefer small, testable changes over giant engine rewrites.
- Rebuild the ROM after N64-facing changes.
- Re-run the host smoke test after gameplay changes.
- When controls/geometry feel wrong, inspect coordinate-system boundaries first.
- Target hardware: N64 with Expansion Pak (8 MB RDRAM); `assert_memory_expanded()` at boot.
