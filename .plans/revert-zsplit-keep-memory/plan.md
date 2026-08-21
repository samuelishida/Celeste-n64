# Revert Z-Split, Keep Memory Optimizations

> **STATUS: COMPLETE.** All 5 increments implemented on branch
> `revert-zsplit-keep-memory`. Clean ROM build (zero warnings), 26 host tests
> pass, Ares on-device capture shows a single near pass
> (`[render-phases] high_priority=0.275 streaming=0.000`;
> `[counters] near_batches=302 texture_uploads=5 vert_loads=302 syncs=302`,
> no distant counters). Baseline: `build/baseline-revert-zsplit-manual.txt`.
> Note: Inc 4's `LvlRoomRenderer` distant-path cleanup was pulled forward into
> Inc 2 (required to keep the build green once `dlod_format.hpp` was deleted).

## Context

The current N64 renderer uses a **two-pass z-split architecture** (`OpenWorldRenderer`): a Z-off distant LOD pass for the horizon and a Z-on textured near pass for the resident ring. That architecture introduced the persistent visual corruption (missing ground, floating fragments, distant geometry in the sky). Experiments proved the near pass is clean; the distant pass is the corruption source. The pre-z-split good-era renderer (`8ea095c`) was visually correct but only rendered a tiny ring and performed poorly.

This plan reverts the renderer to a **single near pass** while preserving the streaming and memory optimizations that came after the z-split.

## Architectural decisions

- **Decision: delete the distant pass entirely (Option A).** Rationale: the distant pass is the corruption source; no distant pass means no z-split handoff, no Z-off back-to-front sort, no separate far plane, no fog/horizon transition, and no shared matrix machinery. Alternatives rejected: hybrid single-pass (keeps DLOD complexity and does not solve the underlying mid-frame matrix/projection fragility). Outcome: world ends at the resident ring edge; skybox remains as background.
- **Decision: keep the 9-cell resident ring.** Rationale: the good-era used 5 cells; the current 9-cell ring prevents diagonal pop-in during free camera orbit, which is already validated. Reverting ring size is independent and not needed to fix the bug.
- **Decision: preserve `TileStreamer` as the single-pass resident pool.** Rationale: it already owns the incremental `ResolveRingDiff` memory optimization, global material grouping, and the texturing path. We only remove the z-split-specific parts (near-draw set, distant skip, per-frame visibility mask).
- **Decision: keep `TexturedRoomRenderer` + global material grouping.** Rationale: this is the largest independent memory/perf win (~63→5 texture uploads at spawn); it does not require z-split and works in a single pass. Flat-color `LvlRoomRenderer::Draw` remains the compile-time fallback behind `kEnableTextures`.
- **Decision: keep camera-at-origin coupling.** Rationale: less churn and already validated on device; `LvlRoomRenderer` model matrices translate by `render_origin - camera_position` and the view uses `origin={0,0,0}`, `target = camera_target - camera_position`. Rolling this back is a separate refactor and not required to remove the z-split.
- **Decision: unified near projection stays, but only one pass uses it.** Rationale: the current `BuildPassCameras` already uses a single `unified_far` because mid-frame projection switching corrupted the RSP. After deleting the distant pass we simplify the camera math back to one `CameraDesc` with `near=5.0f` and `far=800.0f`. Decision: keep `far=800.0f` (matches good era) rather than the widened world-bounds far; the 9-cell resident ring fits comfortably inside 800 and it removes the extra CPU math/world-bounds dependency.
- **Decision: `LvlRoomRenderer` keeps heap batches/run coalescing/FreeBatches/FreeRuns, drops distant-only paths.** Rationale: the internal memory hygiene is independent of z-split; `DrawBlockOnly`, `DrawRunsDirect`, `SetExternalMatrixOwner`, and `no_block_` are only needed for the distant pass.

## Assumptions and answers from code

- **A1 — Good-era renderer was a single pass with `ChunkRingRenderer` + `LvlRoomRenderer`, no distant LOD.** Source: `git ls-tree -r --name-only 8ea095c` shows `chunk_ring_renderer.{hpp,cpp}` and no `tile_streamer`, `distant_world_renderer`, `open_world_renderer` wired into the build.
- **A2 — Current `TileStreamer` owns incremental ring diff and global material grouping.** Source: `src/user/gameplay/render/tile_streamer.hpp` (`ResolveRingDiff`, `kMaxRing=9`, `DrawHighPriority`); `tile_streamer.cpp` global material loop.
- **A3 — Current `OpenWorldRenderer::Render` is the z-split orchestrator.** Source: `src/user/gameplay/render/open_world_renderer.cpp` collects near-draw set, calls `RenderDistant`, then `RenderHighPriority`.
- **A4 — `gameplay_scene.cpp` builds `PassCameras`, attaches viewport once, and calls `open_world_.Render(cams)`.** Source: `src/user/gameplay/scene/gameplay_scene.cpp` `Render()` at L1058-L1220.
- **A5 — `DistantWorldRenderer` is entirely z-split-specific.** Source: `src/user/gameplay/render/distant_world_renderer.hpp` (DLOD table, shared matrix, near-draw skip set, fog, Z-off render).
- **A6 — `LvlRoomRenderer` has near and distant draw paths.** Source: `src/user/gameplay/render/lvl_room_renderer.hpp` (`Draw`, `DrawBlockOnly`, `DrawRunsDirect`, `SetExternalMatrixOwner`, `SetNoBlockMode`).
- **A7 — `pass_camera_math.hpp` derives two cameras and a `unified_far`.** Source: `src/user/gameplay/render/pass_camera_math.hpp` (`PassCameras`, `BuildPassCameras`, `MakeDistantCamera`).
- **A8 — Makefile lists z-split files.** Source: `Makefile` L218-L228 includes `open_world_renderer.cpp`, `tile_streamer.cpp`, `distant_world_renderer.cpp`, `dlod_loader.cpp`, `skybox.cpp`.

## Risks accepted

- **R1 — Horizon disappears; world ends at the ring edge.** Mitigation: this matches the good-era look; later work can add a simple skybox/gradient horizon. Accept for now.
- **R2 — Fog transition removed; distant geometry no longer fades.** Mitigation: no distant geometry is drawn, so no transition is needed. Accept.
- **R3 — Distant LOD asset pipeline becomes unused.** Mitigation: `.dlod` files remain on disk; the baker can be disabled or kept. Not a runtime risk. Accept.
- **R4 — Frame time may regress if the single near pass pushes more than before.** Mitigation: keep global material grouping and incremental ring diff; measure on device after each increment. If frame time is worse than z-split, revisit as a separate perf pass.
- **R5 — Removing `DistantWorldRenderer` breaks host tests and ROM telemetry.** Mitigation: delete or rewrite z-split-specific tests, update `rom_main.cpp` telemetry, and trim `RenderCounts`/`FrameProfiler` phases as part of the plan.
- **R7 — Compile breaks in files not listed in first draft.** Mitigation: explicitly include `render_budgets.hpp`, `n64/profiler.hpp`, `rom_main.cpp`, and `lvl_room_renderer.hpp` `#include "dlod_format.hpp"` in the edit list.
- **R8 — Z-fighting in the single widened-far near pass.** Mitigation: we are reverting to `far=800.0f`, which was validated in good era; if artifacts appear, revisit as a separate near-plane/far-plane tuning task.
- **R6 — Camera-at-origin coupling is preserved even though good era did not use it.** Mitigation: already validated; full decouple is out of scope.

## Increment DAG

- Inc 1 — Remove distant pass usage from orchestrator and callers (M) — depends on: none — unblocks: 2, 3, 4
- Inc 2 — Delete distant-pass-only files and helpers (M) — depends on: 1 — unblocks: 4, 5
- Inc 3 — Simplify camera math and gameplay_scene render loop (M) — depends on: 1 — unblocks: 5
- Inc 4 — Clean LvlRoomRenderer / TexturedRoomRenderer of distant-only paths (S) — depends on: 1, 2 — unblocks: 5
- Inc 5 — Update Makefile, tests, telemetry, docs, and device smoke (M) — depends on: 1, 2, 3, 4

## Increments

### Inc 1 — Remove distant pass usage from orchestrator and callers (M)

**Depends on:** (none)
**Unblocks:** Inc 2, Inc 3, Inc 4
**Done criteria:** `OpenWorldRenderer` no longer references `DistantWorldRenderer` in its interface or implementation; `Render()` only calls the near pass; `rom_main.cpp` no longer prints distant telemetry; build and host tests pass.

#### Files to touch

##### src/user/gameplay/render/open_world_renderer.hpp
- What changes: remove z-split stage enum/stage names, `RenderDistant`/`RenderLowPriority` declarations, `DistantWorldRenderer*` member, `PassCameras` dependency, distant counter fields.
- Function(s): `Render(const PassCameras&)` becomes `Render(const CameraDesc&)`. Remove `RenderDistant`, `RenderLowPriority`, `SetFog`, `Distant()`.
- Data shapes: `RenderCounters` keeps only `near_batches`, `texture_uploads`, `vert_loads`, `syncs`; drops `distant_cells`, `distant_batches`, `distant_vert_loads`, `distant_syncs`.
- Integration points: `GameplayScene::Render()` is the caller.
- Error paths: if `tile_streamer_` is null, `Render()` is a no-op (already guarded).

##### src/user/gameplay/render/open_world_renderer.cpp
- What changes: delete `RenderDistant` function body; delete `RenderLowPriority` stub; `Render()` only calls `tile_streamer_->DrawHighPriority(cam)`. Leave `DistantWorldRenderer*` member declared but stop using it so the file still compiles before Inc 2 deletes it.
- Function(s):
  - `void OpenWorldRenderer::RenderDistant(const CameraDesc&)` → delete.
  - `void OpenWorldRenderer::RenderLowPriority(const CameraDesc&)` → delete or leave as no-op.
  - `void OpenWorldRenderer::Render(const CameraDesc& cams)` → single phase.
  - `SetCenter()` → comment out / guard `distant_->StreamToCenter` and `distant_->SetCameraPosition` with `#if 0` or a feature flag, then remove in Inc 2.
  - `SetCameraPosition()` → remove `distant_->SetCameraPosition` call.
  - `SetFog()` → delete.
- Integration points: `GameplayScene::Render()` calls `open_world_.Render(cams)`.
- Error paths: no distant pointer deref; safe.

##### src/user/gameplay/render/render_budgets.hpp
- What changes: remove `distant_cells` field from `RenderCounts`; remove `kMaxDistantCellsPerFrame` cap; update `BudgetsExceeded`.
- Data shapes: `RenderCounts` loses `distant_cells`; `BudgetsExceeded` no longer checks it.

##### src/user/n64/profiler.hpp
- What changes: remove `kPhaseDistant` and `kPhaseLowPriority` enum values; rename remaining phases so indices stay contiguous (`kPhaseHighPriority` becomes `kPhaseWorld` or similar). Update any switch/if code that depends on the old enum values.
- Function(s): `FrameProfiler::Phase` enum.

##### src/user/rom_main.cpp
- What changes: remove `[render-phases]` distant and low_priority lines; remove `distant_cells`, `distant_batches`, `distant_vert_loads`, `distant_syncs` from `[counters]`; remove `[distant-cells]` block entirely.
- Function(s): telemetry print block.

#### Edge cases
- Skybox draw order: currently drawn inside `RenderDistant`. Move skybox draw to the start of `OpenWorldRenderer::Render()` before the near pass, or move it into `GameplayScene::Render()` directly. Decision: keep it in `OpenWorldRenderer::Render()` at the top.
- `BeginFrame`/`EndFrame` counters: keep; they profile the remaining near pass.

#### Verification
- Build: `./compile-rom.sh` succeeds.
- Host tests: `tests/run_host_tests.sh` passes (z-split tests will fail until Inc 5 removes them).
- Telemetry: `[render-phases]` no longer lists `distant` or `low_priority`; `[counters]` no longer lists distant fields.
- Manual: Ares smoke boots; visual corruption should be gone because the distant pass is not executed.

---

### Inc 2 — Delete distant-pass-only renderer code (M)

**Depends on:** Inc 1
**Unblocks:** Inc 4, Inc 5
**Done criteria:** `DistantWorldRenderer` files deleted; `dlod_loader` and `dlod_format` removed from runtime if not used elsewhere; no references remain.

#### Files to touch

##### src/user/gameplay/render/distant_world_renderer.hpp
- What changes: delete file.

##### src/user/gameplay/render/distant_world_renderer.cpp
- What changes: delete file.

##### src/user/gameplay/render/dlod_loader.hpp / dlod_loader.cpp
- What changes: delete files if `LvlRoomRenderer::LoadFromDlod` is also removed in Inc 4. If kept for future Option B, gate behind a compile flag and remove from Makefile until needed.
- Decision for this plan: delete; no distant pass means no `.dlod` loading at runtime.

##### src/user/gameplay/render/dlod_format.hpp
- What changes: delete file (or keep as shared bake/runtime header only if bake tools still use it; check `tools/` references).

##### src/user/gameplay/render/lvl_room_renderer.hpp
- What changes: remove `#include "gameplay/render/dlod_format.hpp"` when `LoadFromDlod` is removed in Inc 4. If the include is still needed for types used by other functions, move the include to `.cpp`.

#### Edge cases
- Any other code referencing `DistantWorldRenderer`, `dlod_*`, or `DistantLodEntry` must be updated. Search with `rg "DistantWorldRenderer|dlod_|DistantLod" src/ tests/`.
- `open_world_renderer.hpp` forward declaration and member removed in Inc 1; verify no dangling `distant_->` calls remain.

#### Verification
- `rg "DistantWorldRenderer|dlod_loader|DistantLodEntry" src/ tests/` returns empty.
- Build succeeds.

---

### Inc 3 — Simplify camera math and gameplay_scene render loop (M)

**Depends on:** Inc 1
**Unblocks:** Inc 5
**Done criteria:** `GameplayScene::Render()` builds a single `CameraDesc`, attaches one viewport, and draws the near pass directly. `pass_camera_math.hpp` no longer exposes two-pass cameras.

#### Files to touch

##### src/user/gameplay/render/pass_camera_math.hpp
- What changes: remove `PassCameras` struct and `BuildPassCameras` inline function; remove `MakeDistantCamera` and `ValidateDistantCamera`. Keep `CameraDesc` and `MakeNearCamera`.
- Function(s):
  - `MakeNearCamera(...)` → remains the single camera builder.
  - `MakeDistantCamera(...)` → delete.
  - `BuildPassCameras(...)` → delete.
- Data shapes: `CameraDesc` unchanged.
- Integration points: called only from `GameplayScene::Render()`.

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: replace `BuildPassCameras` + `PassCameras` with a single `CameraDesc`/`MakeNearCamera`; remove `inv_view_proj` derivation and `open_world_.UpdateCamera(...)` if per-frame visibility is dropped; keep `SetCameraPosition` for the ring; keep `t3d_viewport_set_projection` with `near=5.0f` and `far=800.0f` (or world-bounds-derived far).
- Function(s): `GameplayScene::Render()`.
- Data shapes: `CameraDesc cams` instead of `PassCameras cams`.
- Integration points: calls `impl_->open_world_.Render(cams)` (now single phase).
- Error paths: if `use_map_pack_` is false, keep legacy `impl_->room_renderer.Draw()`.

#### Edge cases
- Per-frame visibility (`UpdateCamera`) is z-split-specific. With a fixed 9-cell ring we no longer need `inv_view_proj` or `CellAabbInNearCone`. Decision: drop the CPU frustum work and let `TileStreamer` draw its 9 residents. If this causes over-draw concerns, measure first; the ring is small.
- `TileStreamer::DrawHighPriority` currently uses `CellAabbInNearCone` to cull residents per frame. For a single 9-cell pass, either keep the cull (cheap) or make it draw all 9 residents. Decision: keep the cheap AABB cull for now, but remove the handoff mask (`CollectNearDrawSet`/`SetNearDrawSet`); the function still uses `camera_dir` for wedge selection only if we keep `ResolveForwardWedge`.
- If we keep the widened far plane (`cams.unified_far`), we avoid clipping at the ring edge but render more depth range. Accept; good era used far=800 and clipped cleanly.

#### Verification
- Host tests: camera-space math tests still pass if they test `MakeNearCamera` only.
- Build: `./compile-rom.sh` succeeds.
- Device: Ares boots, camera orbit works, no mid-frame projection artifacts.

---

### Inc 4 — Clean LvlRoomRenderer / TexturedRoomRenderer of distant-only paths (S)

**Depends on:** Inc 1, Inc 2
**Unblocks:** Inc 5
**Done criteria:** `LvlRoomRenderer` no longer has `DrawBlockOnly`, `DrawRunsDirect`, `SetExternalMatrixOwner`, `SetNoBlockMode`, `uses_external_matrix_`, or `no_block_`. `LoadFromDlod` removed if no longer used.

#### Files to touch

##### src/user/gameplay/render/lvl_room_renderer.hpp
- What changes: remove declarations and private flags for distant-only paths.
- Function(s): delete `DrawBlockOnly`, `DrawRunsDirect`, `SetExternalMatrixOwner`, `SetNoBlockMode`.
- Data shapes: delete `bool uses_external_matrix_`, `bool no_block_`.
- Integration points: `TileStreamer` calls `Draw()` / `TexturedRoomRenderer` API.

##### src/user/gameplay/render/lvl_room_renderer.cpp
- What changes: delete implementations of the above; remove the `kLodScale` constant if only used by `LoadFromDlod`; remove the `if (uses_external_matrix_) return;` guard in `SetCameraPosition`; simplify `BuildRunsAndBlock` condition to `if (kEnableRspqBlocks && ...)` (no `no_block_`).
- Function(s): `DrawBlockOnly`, `DrawRunsDirect`, `SetExternalMatrixOwner`, `SetNoBlockMode` deleted.
- Integration points: `Draw()` remains the near-pass path; `EmitRunCommands`/`EmitBatchCommands` still used by `Draw()`.

##### src/user/gameplay/render/textured_room_renderer.hpp / .cpp
- What changes: almost nothing; keep material grouping API. If `block_` capture was only for a fallback, keep or remove based on whether `kEnableGlobalMaterialGrouping` stays.
- Decision: keep the per-material grouping API and direct emission path; it is the single-pass perf win.

#### Edge cases
- `LoadFromDlod` may be referenced by tests. Delete the tests or rewrite them as generic LVL loading tests in Inc 5.
- `#include "gameplay/render/dlod_format.hpp"` in `lvl_room_renderer.hpp` must be removed in this increment (or at latest in Inc 2) so Inc 2 file deletion does not break the build.
- `BuildRunsAndBlock` simplification must not reintroduce the old per-cell block allocation if `kEnableGlobalMaterialGrouping` is on; verify `kEnableRspqBlocks` is false for the near pass.

#### Verification
- Build succeeds.
- Host tests for LvlRoomRenderer still pass.

---

### Inc 5 — Update Makefile, tests, docs, and device smoke (M)

**Depends on:** Inc 1, Inc 2, Inc 3, Inc 4
**Unblocks:** (none — final)
**Done criteria:** Makefile no longer compiles deleted files; z-split-specific tests removed/rewritten; `AGENTS.md`/docs updated; Ares smoke confirms clean visuals.

#### Files to touch

##### Makefile
- What changes: remove `src/user/gameplay/render/open_world_renderer.cpp`, `tile_streamer.cpp`, `distant_world_renderer.cpp`, `dlod_loader.cpp`, `skybox.cpp` if those files are fully deleted. Note: `open_world_renderer.cpp` and `tile_streamer.cpp` stay but `distant_world_renderer.cpp` and `dlod_loader.cpp` go. `skybox.cpp` stays if we keep the skybox.
- Function(s): `src` list.

##### tests/run_host_tests.sh
- What changes: drop z-split-specific tests: `distant_cull_contract.cpp`, `distant_overlap_contract.cpp`, `distant_near_deadzone_contract.cpp`, `distant_cellstats_contract.cpp`, `distant_pass_order.cpp`, `distant_shared_matrix_contract.cpp`, `distant_sort_contract.cpp`, `distant_streaming_contract.cpp`, `distant_decimation_contract.py`, `distant_dedup_contract.cpp`, `distant_distance_contract.cpp`, `distant_lod_contract.py`, `distant_no_block_smoke.cpp`, `dlod_format_contract.cpp`, `dlod_format_contract.py`, `frame_order_contract.cpp`, `fog_math.cpp`, `pass_camera_math.cpp`, `render_counters_contract.cpp` if it only tests distant counters, `near_visibility_contract.cpp` if it tests z-split visibility, `tile_visibility_contract.cpp` if only used by z-split, `directional_lod_contract.cpp` if only DLOD.
- Keep: `tile_streamer_diff_smoke.cpp`, `tile_streamer_smoke.cpp`, `material_sort_contract.cpp`, `near_global_sort_smoke.cpp`, `renderer_memory_contract.cpp`, `camera_space_math.cpp`, `batch_coalesce_contract.cpp`, `skybox_transform.cpp`, `debug_visualization_contract.cpp`, `debug_flags_contract.cpp`, `lod_math.cpp` (generic math).

##### AGENTS.md / docs
- What changes: update renderer description from two-pass z-split to single near pass; note distant pass removed; update control/map-runtime notes as needed.

#### Edge cases
- Some tests may test shared utilities (`Mat4`, `CameraDesc`). Keep them if they still apply to single-pass camera math.
- `skybox_transform.cpp` may still be valid; keep if skybox remains.

#### Verification
- `./compile-rom.sh` succeeds with zero warnings.
- `tests/run_host_tests.sh` passes.
- Ares smoke: boot, walk across seams, orbit camera — confirm no fragments, no missing ground, no distant geometry artifacts. Capture new `build/baseline-revert-zsplit-*.txt`.

## Cross-cutting verification

After all increments:
1. Build host suite and ROM.
2. Launch Ares with `tools/capture_baseline.sh` or the CLI command from `AGENTS.md`.
3. Verify `[render-phases]` no longer lists a distant pass; only near batches/texture uploads appear.
4. Verify `[memory]` total/used are within the 8 MB budget and comparable to the pre-revert memory-opt baseline.
5. Walk across the Forsaken City seams; confirm geometry is continuous within the resident ring.
6. Orbit the camera freely; confirm no diagonal pop-in with the 9-cell ring.

## Standards / common-mistakes referenced

- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — keep global material grouping; frame is RSP-bound; target RSP/DMA traffic.
- `.agents/common-mistakes/camera-respawn-reset.md` — not directly applicable, but camera/view state changes still need careful validation.

## Open questions (CONSIDER from review)

- Should `TileStreamer` keep the forward-wedge `ResolveForwardWedge` or revert to the fixed 3×3 `ResolveDistanceRing`? → **DECIDED (post-fix): keep `ResolveForwardWedge` but make it a forward-prioritized 3×3 ring.** It loads all Chebyshev-1 neighbors of the active cell (matching `kMaxRing`), using the camera direction only to sort cells so the most forward are placed first. This avoids the screen-edge gaps caused by an aggressive forward half-space cut (`dot > 0` dropping side/back cells) while still favoring visible cells. `ResolveDistanceRing` remains a test-only alias (zero direction → no prioritization).
- Should the far plane return to 800 or stay widened to world-bounds? 800 matches good era; widened far avoids edge clipping on large maps. → **Decision: return to `far=800.0f`.**
- Do we keep `skybox.cpp` in the build? Yes, as background; it is no longer tied to the distant pass. → **Confirmed keep.**
- Do we keep `open_world_renderer.cpp/.hpp` as a thin `TileStreamer` wrapper or inline it into `GameplayScene`? Keeping it preserves the gameplay/ROM separation and testability; inlining is future cleanup. → **Confirmed keep as wrapper.**
- Should we create a git branch before starting because files are deleted? → **Decision: yes, branch `revert-zsplit-keep-memory` from `main` before Inc 2 deletes files.**

## Out of scope

- Re-adding a distant horizon later (would be a new plan).
- Rolling back camera-at-origin to world-space view.
- Changing the resident ring size (9 stays).
- Removing `TexturedRoomRenderer` or global material grouping.
- Bake-side DLOD pipeline changes; `.dlod` files and baker remain for future use.
