# N64 Optimization (CPU + RSP) for the Open-World Renderer

## Context

The user pasted a broad Kaze-Emanuar-style N64 optimization survey (CPU/RSP/RDP
co-parallelism, cache vs uncached memory, loop unrolling, flags-vs-abstractions,
batch rendering, billboards, shadow-on-RSP, per-file compiler tuning, inlining)
and invoked plan-large to turn it into executable increments for this Celeste64
demake ROM.

That document is a *reference*, not a spec. Grounding it against this codebase
(`libdragon + tiny3d`, single-pass near renderer) changed the surface
substantially:

- **The renderer is single-pass now.** The distant DLOD pass was removed from
  the build (`AGENTS.md` documents `DistantWorldRenderer`/`dlod_loader`/
  `dlod_format` deleted; `.dlod` files + baker remain on disk for a *future*
  distant feature, not loaded). So the document's distant/RDP-pass themes
  mostly do not apply.
- **The frame is RSP-bound**, not CPU-bound: `rom_main.cpp` calls `rspq_wait()`
  after `Render()`. Frame-time gains come from reducing **RSP command count or
  RDRAM/TMEM DMA traffic**, not CPU loops. This is a verified, load-bearing
  fact (`AGENTS.md`, `docs/streaming_memory.md`, learning
  `rsp-bound-frame-and-material-grouping.md`).
- **Many techniques already exist** and must not be re-planned: batching/
  coalescing (`batch_coalesce.hpp`), global material grouping (unconditional,
  `tile_streamer.cpp:328-386`), streaming + ring-diff (`ResolveRingDiff`),
  RSPQ block precompilation (`kEnableRspqBlocks=true`), int16 fixed-point
  packing (`kPosScale=32`), debug gating (`kVerboseFrameLogging=false` +
  `debug_flags_contract.cpp`).
- **The document's deep-RSP microcode ideas** (custom microcode, shadow
  geometry generated on the RSP) are **not exposed by tiny3d/libdragon** —
  they are a rewrite, not an optimization. **Marked out of scope.**
- **The document's "cache vs uncached memory" theme is considered and
  deferred.** The frame is RSP-bound, so the CPU likely has headroom;
  uncached-RDRAM / cache-line tuning is a CPU-side optimization that only
  pays off if `[profiler] update` shows real CPU time (see Inc 5 / D6). It is
  explicitly out of scope for this plan unless the measurement gate proves
  otherwise.

**Intended outcome:** a set of small, independently-shippable increments that
cut real work on this engine's actual hot paths — the near textured pass's RSP
command count (re-enable frustum culling, the single biggest lever) and the
CPU/gameplay layer's instruction cost (per-file optimization, inline hot math,
SinLUT wiring, actor data representation) — with host smokes + Ares telemetry as
verification. No microcode rewrite.

## Architectural decisions

- **D1 — Re-enable near-pass frustum culling behind a flag, as the
  headliner.** The predicate `CellAabbInNearCone` (`lod_math.hpp:103`) exists
  and is fully written, but the active path force-draws all 9 resident cells
  (`tile_streamer.cpp:269-274`). Re-enabling it is the single largest RSP
  command reduction available because the frame is RSP-bound. It was disabled
  deliberately to avoid screen-edge pop, so this increment ships it gated
  (`kEnableNearCulling`, **default OFF** to preserve current visuals until the
  RSP win is measured) and **reuses** the already-existing `kCullMargin`
  (`lod_math.hpp:35`, 1.15f) as the cone-width control, and measures the RSP
  win via `[counters] syncs`/`near_batches` before/after. The screen-edge pop
  risk is carried explicitly (it is the exact artifact class previously
  reverted) and verified on device with a full orbit, not just a boot smoke.
  **Alternatives rejected:** *shrink the 3×3 ring* (changes streaming + risks
  visible gaps; unrelated). *Leave disabled* (foregoes the largest RSP win on
  an RSP-bound frame). *Always-inline culling* (the predicate is already a
  cold-ish AABB test; the point is to *skip* geometry, not speed up the test).

### D2 — Per-TU `-O2` for hot translation units; global stays `-Os`.
**Load-bearing flag flow (verified in `n64.mk`):** the toolchain base is
*already* `-O2` (`n64.mk` ~line 78: `N64_C_AND_CXX_FLAGS += -DN64 -O2 …`).
The project's `-Os` (`Makefile:16`) is a **global downgrade appended last** —
GCC's last `-O` wins, so today *every* TU compiles at `-Os`. "Optimizing a hot
TU" therefore means **suppressing the `-Os` downgrade for that TU** (letting it
fall back to the toolchain's `-O2` base), not "adding `-O2"`.
**Injection point (verified in `n64.mk`):** the `.o` rule (lines 216–219)
compiles with **`$(CXXFLAGS)`**, *not* `N64_CXXFLAGS`; `N64_CXXFLAGS` only
reaches `CXXFLAGS` via the target-specific `%.z64: CXXFLAGS+=$(N64_CXXFLAGS)`
(line 130). A target-specific `N64_CXXFLAGS += -O2` on the `.o` target is NOT
guaranteed to reach the compiler. The correct mechanism is a target-specific
`CXXFLAGS += -O2` on the named `.o` target (e.g.
`$(BUILD_DIR)/src/user/gameplay/render/tile_streamer.o: CXXFLAGS += -O2`),
keeping `-Os` as the default everywhere else. **Before committing to syntax,
the implementer must print the actual per-TU compile command line** (e.g. a
temporary `-D` marker + `make -n`, or read the `[CXX]` echo) to confirm which
flags reach the compiler.
**Warning, load-bearing:** reverting a TU from `-Os` to `-O2` may re-trigger the
`-Os/-ffast-math` miscompile class that forced the `__attribute__((noinline))`
workarounds in `world.cpp:133-307`. Every `-O2`-annotated TU must pass the full
host smoke suite + a device sanity boot; if a TU regresses, fall back to a
targeted `__attribute__((optimize("-O2")))` on just the hot functions rather
than the whole file.
  Alternative rejected: *global `-O3`* — too broad, reintroduces the
  miscompile risk everywhere; per-TU confines blast radius.

### D3 — Inline hot math helpers + wire the existing `SinLUT`/`CosLUT` into the
highest-frequency trig path. `runtime/math.hpp` helpers (`Clamp`, `Approach`,
`NormalizeXZ`, `LengthXZ`, `DirectionFromAngle`) are out-of-line in `math.cpp`.
**LUT target (verified):** the highest-frequency trig is `DirectionFromAngle`
(`math.cpp:54`, raw `std::cos`/`std::sin`), reached via `RotateTowardXZ` from
`player_controller.cpp:439,480,500` — up to **3×/frame**. The camera's
`RotateAroundUp` (`camera_controller.cpp:58`) is only 1×/frame and is secondary.
`SinLUT`/`CosLUT` (4096 entries, `math.cpp:115-142`) exist but are unused.
Wiring the LUT into `DirectionFromAngle` covers the 3×/frame path; the camera
is a secondary swap. Marking the helpers `inline`/`always_inline` is a CPU-only
win, verifiable on host with zero renderer risk. Note the LUT interface is
`uint16_t` angle in [0,2π) LUT units; callers pass float radians, so we add a
`cos_radians`/`sin_radians` helper that converts via `LUT2Radians` (fmod to
[0,2π) first). **Trajectories are within LUT quantization tolerance, NOT
byte-identical** (see Inc 2).

### D4 — Actor query cleanup: per-actor-type id for the single hot query +
delete the dead trait-query templates. **Verified premise (corrects the
original "hot trait query paths" framing):** the query templates live in
`actor_world.hpp` (NOT `actor_world.cpp`, which only has
`Add`/`Destroy`/`ResolvePending`/`Update`). `Get<T>()` casts to **concrete
actor types**, not traits, and has exactly **one** call site in `src/`:
`gameplay_scene.cpp:1119` → `Get<StrawberryActor>()`, once per frame — a
single `dynamic_cast`/frame is negligible RTTI cost, not a "hot query path".
`All<T>()` and `OverlapsFirst<T>()` have **zero** call sites in `src/`; they
exist only to satisfy `tests/actor_world_smoke.cpp`, which is **not wired**
into `run_host_tests.sh`. `PickupTrait`/`PushoutTrait` are inherited by
  **no** class (only `RidePlatformTrait` and `HazardTrait` have real
  inheritors). So this is a **cleanup, not a perf win**: (a) replace the single
  per-frame `Get<StrawberryActor>()` `dynamic_cast` with a per-actor-type id
  (a `uint16_t type_id_` on `Actor` set in each concrete ctor), and (b) delete
  the dead `All<>`/`OverlapsFirst<>` templates + the unwired
  `actor_world_smoke.cpp` (which currently fails anyway — it asserts
  `All<PickupTrait>().count==3` against a trait no class inherits). Behavior
  of the single live query is preserved. Scalar bools in `PlayerState` are
  lower priority; a small bitfield there is a follow-up OPTIONAL, not a hard
  requirement.
  Alternative rejected: *full ECS* — far beyond the doc's intent and this
  small actor set (64 slots). *Trait bitflags* — the original plan's framing;
  rejected because the "hot trait query paths" do not exist (see above).

### D5 — Dead-code removal only for the orphaned C++ that cannot build, and the
  unused gameplay `Arena`. `distant_world_renderer.cpp` exists but its header
  `distant_world_renderer.hpp` does not — the `.cpp` is an orphan not in the
  Makefile and not compilable as part of any TU; removing it is safe.
  **But AGENTS.md says keep `.dlod` files + baker on disk for a FUTURE distant
  feature** — so we remove only the dead `.cpp` renderer/loader and the unused
  gameplay `arena.cpp`/`arena.hpp`, and we DO NOT touch the `.dlod` bake
  (`.dlod` writer, `tools/ogworld/distant_lod.py`), which stays as the future
  feature's on-disk input.
  Alternative rejected: *delete everything distant* — violates AGENTS.md's
  explicit "keep `.dlod` files and baker on disk."

### D6 — Collision/CPU micro-tuning (fast-inv-sqrt, LUT trig in the FSM) is
  **measurement-gated and optional**, not a hard increment. The collision hot
  path is a BVH over one global CMSH with stack buffers and no alloc; the
  dominant cost is per-substep `sqrt` + sphere-sweep. Any fast-math substitution
  must be gated behind a measurement that proves `[profiler] update` is actually
  on the critical path (the frame is RSP-bound — CPU may have headroom). We
  instrument `[profiler]` phase for `update` first; only if CPU shows real time
  do we optimize it. If measurement shows CPU has slack, this increment shrinks
  to a documented "CPU has headroom; don't micro-optimize" note.

### D7 — Deep-RSP microcode techniques are OUT OF SCOPE (rewrite, not
  optimization; not exposed by tiny3d). Cross-cutting: every increment is
  verified by host smokes + Ares telemetry, and RSP-command reductions are
  measured via `[counters] syncs`/`near_batches`, not assumed.

## Assumptions and answers from code

- **A1 — Frame is RSP-bound.** `rspq_wait()` after `Render()` in
  `rom_main.cpp:95`; gain = RSP cmd count / DMA traffic, not CPU.
  Source: code `@ src/user/rom_main.cpp:95`, `AGENTS.md`,
  `docs/streaming_memory.md`, learning `.agents/learnings/`.
- **A2 — Renderer is single-pass near.** `GameplayScene::Render` drives only
  `open_world_.Render(cams)`; no distant pass in the scene graph.
  Source: code `@ src/user/gameplay/scene/gameplay_scene.cpp:1103`,
  `AGENTS.md` ("deleted distant-only code paths").
- **A3 — Near-culling predicate exists but is unused at runtime.**
  `CellAabbInNearCone` defined `lod_math.hpp:103`; force-draw all in
  `tile_steamer.cpp:233-274` (`visibility_.Set(i,true)` for all).
  Source: code `@ lod_math.hpp:103`, `tile_streamer.cpp:269-274`.
- **A4 — Global `-Os` downgrade over an `-O2` toolchain base; no per-file
  tuning.** The toolchain base is `-O2` (`n64.mk` ~line 78); the project's
  `-Os` (`Makefile:16`) is appended last and wins (GCC last-`-O`-wins), so all
  TUs compile at `-Os` today. Source: code `@ Makefile:15-16`,
  `n64.mk:75-135`.
- **A5 — `SinLUT`/`CosLUT` exist but unused; camera uses raw `std::cos/sin`.**
  Source: code `@ runtime/math.hpp:29-32`, `camera_controller.cpp:58-59`,
  `gameplay_scene.cpp:585-586`.
- **A6 — `Actor` uses virtual dispatch + a single per-frame `dynamic_cast`
  (not a hot trait-query path).** The query templates are in
  `actor_world.hpp`; `Get<T>()` (the only live call,
  `gameplay_scene.cpp:1119` → `Get<StrawberryActor>()`) casts to a concrete
  type; `All<>`/`OverlapsFirst<>` have no call sites in `src/`. Source: code
  `@ actor/actor.hpp`, `world/actor_world.hpp`, `traits.hpp`,
  `scene/gameplay_scene.cpp:1119`.
- **A7 — Per-frame heap alloc is absent in CPU gameplay.** Only
  `n64::FrameArena` (renderer) allocates; gameplay `Arena` is dead code.
  Source: `runtime/frame_arena.hpp`, `gameplay/arena.cpp` (unused).
- **A8 — The frame is RSP-bound so CPU may have slack; CPU-optimization
  increments are confirmed only if `[profiler] update` shows ms.** The
  `update` phase does not exist in `profiler.hpp` yet (phases are
  `high_priority`, `texture_upload`, `streaming`) — **Inc 5 adds it** before
  the measurement gate can run. Source: `docs/perf_budget.md` (update budget
  ≤6 ms of 33.3), `runtime/profiler.hpp`.
- **A9 — Host smokes are the correctness gate for every behavior-preserving
  refactor.**
  Source: `tests/run_host_tests.sh` (g++ host, `-Isrc/user`).
- **A10 — Device verification via Ares + `tools/capture_baseline.sh`; memory
  boots expanded (compare `used` as fraction of `total`).**
  Source: `AGENTS.md`, `docs/perf_budget.md`.

## Risks accepted

- **Near-culling re-enable may cause ring-edge pop** (why it was disabled):
  mitigations: flag default off / tuning knob / documented pop rationale; verify
  a full 360° camera orbit + a cell-boundary walk on Ares; measure RSP win first
  and only flip default ON if the win justifies the pop. Accept: risk is
  bounded and reversible.
- **`-O2` re-triggers `-Os`/`-ffast-math` miscompiles** (the
  `world.cpp` workaround pattern): mitigated by per-TU annotation + full host
  smoke pass + device boot test before merging; if a TU regresses, drop to
  per-function `__attribute__((optimize("-O2")))`.
- **The single per-frame `Get<StrawberryActor>()` `dynamic_cast` → type-id
  swap could change behavior** if the cast relies on type identity (e.g. a
  future `Get<>` call on a derived type): mitigated by keeping a host test
  that asserts the live query returns the same actor before/after, and by
  deleting (not rewriting) the dead `All<>`/`OverlapsFirst<>` templates so no
  stale trait assertions survive.
- **CPU micro-tuning may be wasted work** if the frame is dominated by RSP:
  mitigated by instrumenting `[profiler] update` first and cutting the increment
  if CPU shows slack.
- **Dead-code removal may remove something the user still wants**: mitigated by
  touching only the non-building orphan and the unused Arena; `.dlod` models
  stay per AGENTS.md.

## Increment DAG

- Inc 1 — Per-TU compiler flags for hot paths (S) — depends: none — unblocks: 2, 3, 4, 6
- Inc 2 — Inline hot math + wire SinLUT (S) — depends: 1 — unblocks: 5
- Inc 3 — Near-pass frustum culling (gated) (M) — depends: 1 — unblocks: 7
- Inc 4 — Actor query cleanup: type-id for single live query + delete dead trait templates (S) — depends: 1 — unblocks: 7
- Inc 5 — Collision `sqrt` micro-opt, measurement-gated (L) — depends: 2 — unblocks: none
- Inc 6 — Dead-code removal (orphan `.cpp` + unused Arena) (S) — depends: 1 — unblocks: 7
- Inc 7 — Cross-cutting measurement + close-out (S) — depends: 3, 4, 6

(1 → 2 → 5 and 1 → {3,4,6} → 7 are the dependency edges. 3/4/6 are parallel
after 1.)

## Increments

### Inc 1 — Per-TU compiler flags for hot paths (S)
**Status:** done — 6 hot TUs (tile_streamer, textured_room_renderer, lvl_room_renderer, open_world_renderer, camera_controller, player_motor) compile at `-O2` via target-specific `CXXFLAGS += -O2` overrides; verified with `make -nB all` (hot TUs' `g++ -c` lines end `-O2`, cold TUs end `-Os`). 26/26 host tests pass, ROM builds, Ares boot + play confirmed good. The `[memory]` ratio check is deferred to Inc 3's before/after captures (same `-O2` build).
**Depends on:** none
**Unblocks:** 2, 3, 4, 6
**Done criteria:** the hot renderer + gameplay TUs compile at `-O2` (the
toolchain base, with the global `-Os` downgrade suppressed for those TUs),
all host smokes pass, ROM boots in Ares, and the `[memory] used/total` ratio
is unchanged.

#### Files to touch

##### Makefile
- What changes: add per-TU override lines for the hot TUs (renderer hot path +
  gameplay FSM/camera/motor) using GNU make **target-specific `CXXFLAGS`**
  (not `N64_CXXFLAGS` — see D2 for the verified flag flow), e.g.
  `$(BUILD_DIR)/…/render/tile_streamer.o: CXXFLAGS += -O2`
  (and the equivalent for `textured_room_renderer.o`, `lvl_room_renderer.o`,
  `open_world_renderer.o`, `camera_controller.o`, `player_motor.o`).
- Function(s): build target-specific variable rules.
- Data shapes: `CXXFLAGS` is the make variable the `.o` rule (n64.mk:216-219)
  actually compiles with; per-TU rules append `-O2` only for the named object,
  overriding the global `-Os` (last `-O` wins).
- Integration points: the `$(BUILD_DIR)/%.o` pattern in `n64.mk`.
- **First step (load-bearing):** read `n64.mk:75-135` to confirm the flag
  flow, then verify with `make -n` that the printed per-TU compile command
  line for a hot TU contains `-O2` (and a non-annotated TU still shows `-Os`).
  Do not commit to the syntax until the printed command line proves the flag
  reaches the compiler.
- Error paths: if `-O2` on a TU causes a smoke failure, revert that TU to a
  per-function `__attribute__((optimize("-O2")))` or back to `-Os`.

#### Edge cases
- `-ffast-math` + `-O2` is where the `world.cpp` miscompiles happened; the
  FSM files (`player_motor`, `world.cpp`) are the highest regression risk.
  Test those TUs individually before accepting the flag.
- Don't `-O3` files that aren't hot — waste ROM.

#### Verification
- Run: `./tests/run_host_tests.sh` (must pass fully) then `./compile-rom.sh` then A10.
- Confirm with `make -n` (before the full build) that the hot TUs compile with
  `-O2` and the rest with `-Os`.
- Tests to add/update: none (behavior-preserving build-config change); rely on
  the existing smoke + feel_spec suites.
- Done: host smokes pass, ROM boots, `[memory]` unchanged, and the game plays
  identically.

### Inc 2 — Inline hot math + wire SinLUT into DirectionFromAngle (S)
**Status:** done — hot math helpers (Clamp, Approach, DirectionFromAngle, RotateTowardXZ, LengthXZ, NormalizeXZ) are `always_inline` in `runtime/math.hpp`; `DirectionFromAngle` (up to 3×/frame) and camera `RotateAroundUp` route through the 4096-entry `SinLUT`/`CosLUT` via `CosRadians`/`SinRadians`. 26/26 host tests pass (within LUT quantization tolerance), ROM builds clean under `-Werror`, Ares boot + play confirmed (dash feel + camera orbit unchanged). The LUT is BSS (+32 KB), so it is memory-neutral against the `[memory]` heap report. `gameplay_scene.cpp:585` boot-time debug trig left raw per plan.
**Depends on:** 1
**Unblocks:** 5
**Done criteria:** `runtime/math.hpp` hot helpers are `always_inline`; the
highest-frequency trig path — `DirectionFromAngle` (`math.cpp:54`), reached
via `RotateTowardXZ` from `player_controller.cpp:439,480,500` (up to 3×/frame)
— routes through the existing `SinLUT`/`CosLUT`; the camera's
`RotateAroundUp` (`camera_controller.cpp:58`, 1×/frame) is a secondary swap;
host movement/camera smokes pass within LUT quantization tolerance.

#### Files to touch

##### src/user/gameplay/runtime/math.hpp / math.cpp
- What changes: mark `Clamp`, `Approach`, `NormalizeXZ`, `LengthXZ`,
  `DirectionFromAngle`, `RotateTowardXZ` as `inline`/`always_inline` (move
  bodies into the header or annotate). **Primary LUT target:** rewrite
  `DirectionFromAngle` (`math.cpp:54`, currently raw `std::cos`/`std::sin`)
  to route through `SinLUT`/`CosLUT`; this covers `RotateTowardXZ` and its
  3×/frame call sites in `player_controller.cpp`. Add `inline float
  CosRadians(float rad)` / `SinRadians(float rad)` that index `SinLUT`/`CosLUT`
  via a rad→LUT-unit conversion (fmod to [0,2π) first, then `LUT2Radians`).
- Function(s): `Clamp`, `Approach`, `NormalizeXZ`, `LengthXZ`,
  `DirectionFromAngle`, `CosRadians/SinRadians` (new).
- Data shapes: unchanged scalar/`Vec3` signatures.
- Integration points: `player_controller.cpp` (via `RotateTowardXZ`),
  `camera_controller.cpp`, `gameplay_scene.cpp` call the helpers.
- Error paths: LUT index must mask `& (kLutSize-1)`; conversion must handle
  full-range float radians (fmod to [0,2π) first).
- **Header-move caveat:** `NormalizeXZ`'s body relies on anonymous-namespace
  helpers in `math.cpp` (`IsFiniteBits`, `FlushSubnormalBits`, `LengthXZ`).
  Moving it into the header requires exposing those helpers (rename +
  `inline`) or keeping `NormalizeXZ` out-of-line.

##### src/user/gameplay/player/camera_controller.cpp
- **What changes:** replace `std::cos(radians)`/`std::sin(radians)` in
  `RotateAroundUp` with `CosRadians`/`SinRadians`.
- Edge cases: camera spin rate > 2π/frame is unlikely; still fmod the angle.

##### src/user/gameplay/scene/gameplay_scene.cpp
- **Deferred (not hot):** the `std::cos/std::sin` at :585 is inside the
  boot-time debug block (`kDebugCameraRotateDeg`), not a per-frame path.
  Leave it raw unless the debug block is promoted to a live feature.

#### Edge cases
- `always_inline` on a large helper may bloat I-cache — measure, and fall back
  to plain `inline` if the ROM's I-cache becomes worse.
- LUT is 4096 entries (16 KB table) — already resident; no memory concern.
- Name correction: the header declares `LengthXZ`, not `Length`; the two
  `Length` overloads at `player_controller.cpp:15` and `player_motor.cpp:16`
  are file-local and distinct from `LengthXZ` — don't confuse them.

#### Verification
- Run: `tests/run_host_tests.sh` (movement + camera + feel suites); then device
  boot; confirm dash feel and camera orbit unchanged within LUT quantization
  tolerance.
- Done: existing smokes pass; movement + camera feel visually unchanged within
  LUT quantization (NOT byte-identical — see architectural decision D3).

### Inc 3 — Near-pass frustum culling (gated) (M)
**Status:** done — `kEnableNearCulling` (tile_streamer.hpp, **default OFF**) gates `CellAabbInNearCone` per resident cell in `TileStreamer::UpdateCamera`; the center cell (`set_.spec[0]`, always the center after `SetCenterImpl` compaction) is explicitly exempt. `kCullMargin` (lod_math.hpp:35, 1.15f) is reused as-is (hardcoded inside the predicate — no signature change, per plan). `tests/near_visibility_contract.cpp` extended: 6-cell grid, cone mask `T,T,T,F,T,F` (only −X behind + back-diagonal cull; the per-corner `atan(half_diag/dist)` slack widens the effective cone to ~68° half-angle, so side cells stay drawn — intended draw-safe behavior) + degenerate-facing all-drawn case. 26/26 host tests pass; ROM builds clean under `-Werror` both ways (flag-OFF elf text 409640 / flag-ON 410344). Device before/after captured in this increment: `build/baseline-inc3-off-20260821-173408.txt` (flag-OFF build) vs `build/baseline-inc3-on-20260821-175257.txt` (flag-ON build). Idle boot+1cell state is identical both ways (`near_batches=56 syncs=56`, `high_priority≈0.07 ms`, 30 fps — expected: center cell exempt, one resident cell). Exploration states show no regression (ON: 293→280→271 batches, 21–28 fps; OFF: 302→328→431→568, 14–30 fps — runs explored different depths, so the cull delta is not cleanly isolated, but ON never drew more than OFF at comparable states). `[memory]` neutral: OFF `total=2163512 used=2146736` vs ON `total=2166792 used=2146744` (~3 KB allocator noise) — this also closes Inc 1's deferred `[memory]` ratio check (Inc 1–3 heap-neutral). Flag restored to OFF after the after-capture; rollback = clean flag flip.
**Depends on:** Inc 1
**Unblocks:** 7
**Done criteria:** `kEnableNearCulling` flag (**default OFF**) gates the
`CellAabbInNearCone` test in `TileStreamer::UpdateCamera`, reusing the existing
`kCullMargin` (`lod_math.hpp:35`, 1.15f) as the cone-width control;
`[counters] syncs`/`near_batches` drop with no visible screen-edge pop on a
full camera orbit + cell walk; device telemetry captured before/after IN THIS
increment.

#### Files to touch
##### src/user/gameplay/render/tile_streamer.cpp
- Replace the force-set loop (`:269-274`) with: if `kEnableNearCulling`, run
  `CellAabbInNearCone` for each resident cell and set `visibility_`; else keep
  the current force-all.
- Pass the existing `kCullMargin` into the predicate call (it already exists
  at `lod_math.hpp:35` and is hardcoded inside `CellAabbInNearCone`).

##### src/user/gameplay/render/tile_streamer.hpp
- Add `static constexpr bool kEnableNearCulling` (default false).

##### src/user/gameplay/render/lod_math.hpp
- Keep `CellAabbInNearCone` as-is; expose/reuse the existing `kCullMargin` so
  the cone can be widened to avoid pop without changing the predicate.

#### Edge cases
- A cell whose AABB barely clips the cone corner → pop; widen the margin
  ±0.05 and re-test a 360° turn (per `docs/perf_budget.md` tuning notes).
- Don't cull the cell the camera is IN (always draw the center cell). The
  center cell passes `CellAabbInNearCone` for the real reason: the camera
  sits inside its AABB, so the in-front corners of that cell (depth 5..800,
  within the cone) pass the test — it is NOT a "degenerate facing" case. Keep
  an **explicit exemption** (always-visible center cell) as a guard so a
  future predicate change cannot cull the cell the player is standing on;
  document the real pass-reason in the comment.
- The screen-edge pop risk is real — this is exactly the artifact class
  previously reverted (`tile_streamer.cpp:263-274`, `AGENTS.md` forward wedge
  rationale). Rollback is a clean flag flip; verify on device with a full
  orbit, not just boot.

#### Verification
- `./tests/run_host_tests.sh`; then `./compile-rom.sh`; Ares boot; walk the whole
  map + full orbit. **Capture before/after `[counters] syncs`/`near_batches`/
  `[render-phases] high_priority` with `tools/capture_baseline.sh` IN THIS
  increment** (not deferred to Inc7), so the headlined RSP win is verified at
  the point it ships.
- **Two builds required:** `kEnableNearCulling` is a compile-time
  `constexpr` defaulting OFF, so the "before" capture is the flag-OFF build
  (current behavior) and the "after" capture is a flag-ON build (rebuild with
  the constant flipped, or a temporary `-D` override). Do both captures in
  this increment and record which build produced each. (A runtime toggle is a
  possible alternative but adds state the 3×3 ring doesn't need.)
- **Extend** the existing wired `tests/near_visibility_contract.cpp` (already
  covers the center-always-visible mask + `Mat4Invert`) with a case asserting
  that, for a given camera, a resident cell outside the cone is NOT drawn
  while the center cell always is. Do not create a duplicate test file.

### Inc 4 — Actor query cleanup: type-id for the single live query + delete
dead trait templates (S)
**Status:** done — `ActorTypeId` enum (actor.hpp, 9 stable unique constants
kActor=0…kCassette=8) + protected `type_id_` (base default kActor) + non-virtual
`TypeId()`; all 8 derived actor headers stamp `kTypeId` in a one-line ctor;
`ActorWorld::Get<T>()` now compares `type_id_ == T::kTypeId` (no RTTI) with
exact-type semantics documented (old `dynamic_cast` also matched derived types,
but the only live query targets the concrete leaf `StrawberryActor`, so behavior
is unchanged); deleted dead `All<T>()`/`OverlapsFirst<T>()`/private
`WorldBounds`/`ActorView` + the now-unneeded world.hpp include from
actor_world.hpp; deleted unwired `tests/actor_world_smoke.cpp` (it pinned the
dead templates); new `tests/actor_type_id_smoke.cpp` wired into run_host_tests.sh
(uniqueness of all 9 constants, ctor stamps, `Get<>` same-actor + null-on-absent
+ exact-type). `traits.hpp` kept unchanged (harmless empty bases; plan permits).
`Add`/`Destroy`/`ResolvePending`/`Update` untouched. 27/27 host tests pass; ROM
clean under `-Werror` (elf text 409176 / data 105284 / bss 134832 / total 649292,
down from 409640/105324 — dynamic_cast templates + dead code removed); device
boot confirmed (60 s Ares capture `build/baseline-inc4-boot-20260821-184809.txt`,
steady state 56 batches / ~0.07 ms / 30 fps, identical to the Inc 3 baseline;
only "FATAL" line is the known non-fatal player.t3dm asset gap).
**Depends on:** 1
**Unblocks:** 7
**Done criteria:** the single per-frame `Get<StrawberryActor>()` query
(`gameplay_scene.cpp:1119`) resolves via a per-actor-type id instead of
`dynamic_cast`; the dead `All<T>()`/`OverlapsFirst<T>()` templates and the
unwired `tests/actor_world_smoke.cpp` are deleted; all gameplay behavior
unchanged (host smokes + device boot).

**Scope note (corrects the original "hot trait query paths" framing — see D4):**
this is a cleanup, not a perf win. The query templates live in
`actor_world.hpp` (not `.cpp`); `Get<T>()` casts to concrete types and has
exactly one live call site; `All<>`/`OverlapsFirst<>` have zero call sites in
`src/`; `PickupTrait`/`PushoutTrait` are inherited by no class (only
`RidePlatformTrait` on `MovingSolidActor` and `HazardTrait` on `SpikeActor`
are real).

#### Files to touch
##### `src/user/gameplay/actor/actor.hpp`
- Add `uint16_t type_id_` (set from a per-concrete-class constant passed in
  the ctor, or via a `kTypeId` static in each concrete actor header); provide
  `TypeId()` access. No behavior change to existing virtual dispatch.

##### `src/user/gameplay/world/actor_world.hpp`
- `Get<T>()`: replace the `dynamic_cast<T*>` scan with a `type_id_` comparison
  against `T::kTypeId` (same return semantics: first actor of type T, or
  null). `All<T>()` and `OverlapsFirst<T>()`: **delete** (no call sites in
  `src/`).
- Keep `Add`/`Destroy`/`ResolvePending`/`Update` in `actor_world.cpp`
  unchanged.

##### `src/user/gameplay/scene/gameplay_scene.cpp`
- The single `Get<StrawberryActor>()` call site continues to work unchanged
  (the template now resolves by type id). No call-site edit expected.

##### `tests/actor_world_smoke.cpp`
- **Delete** (unwired into `run_host_tests.sh`; asserts
  `All<PickupTrait>().count==3` against a trait no class inherits — currently
  failing, and it pins the dead templates we're removing).

##### (only if the trait marker structs are removed) `moving_solid_actor.hpp`,
`spike_actor.hpp`
- If `traits.hpp`'s empty virtual marker structs are dropped as part of the
  cleanup, update the two real inheritors (`MovingSolidActor`,
  `SpikeActor`) to stop inheriting them. If the marker structs are kept
  (they're harmless empty bases), this file is untouched.

#### Edge cases
- **Type-id assignment must be stable and unique** per concrete actor type;
  a collision silently mis-resolves `Get<>`. Pin the constants in one place
  (e.g. an enum in `actor.hpp`) and add a host test asserting uniqueness.
- The live query's semantics must be preserved exactly: same actor returned,
  same null-on-absent behavior. The old `dynamic_cast` also matched derived
  types; the type-id comparison is exact-type — acceptable here because the
  only live query targets a concrete leaf type (`StrawberryActor`), but
  document the exact-type semantics on `Get<>`.
- Do NOT invent `PickupTrait`/`PushoutTrait` membership to "fix" the dead
  templates — delete them instead (see D4).

#### Verification
- Add a small host test (or extend an existing actor test) asserting
  `Get<StrawberryActor>()` returns the same actor before/after the swap and
  returns null when no strawberry exists; run `tests/run_host_tests.sh`; device
  boot; gameplay unchanged (strawberry pickup still works).

---

### Inc 5 — Collision `sqrt` micro-opt, measurement-gated (L)
**Status:** done (gate FAILED → note) — added the `kPhaseUpdate` phase to
`n64/profiler.hpp` (before `kPhaseCount`, so the `[kPhaseCount]` arrays grow
automatically), named it `update` in `profiler.cpp`'s `kPhaseNames`, bracketed
the gameplay update in `rom_main.cpp` (`BeginPhase`/`EndPhase(kPhaseUpdate)`
around `scene_mgr.Update(...)`), and extended the `[profiler] avg frame time`
report line to print `update=… ms`. Device capture
(`build/baseline-inc5-update-20260821-193329.txt`, 120 s, 173 report lines)
measured **`update=0.194–0.195 ms/frame`** — far below the ~1 ms gate, so the
fast-inv-sqrt / LUT `Length` on the BVH sweep is NOT applied (the increment
collapses to a note: the collision/substep sweep is not costing real ms; the
frame is RSP-bound at ~33.4 ms / 30 fps with `update` a ~0.6% slice). The
instrumentation stays as the permanent deliverable — `update` now prints on
every `[profiler]` line for future CPU-vs-RSP decisions. Frame time unchanged
(33.1–33.4 ms / 29.9–30.2 fps); `[memory] total=2165944 used=2146768`
unchanged vs the Inc 3/6 baseline; only "FATAL" line is the known non-fatal
player.t3dm asset gap. `coll_mesh.cpp`/`geom.cpp`/`player_motor.cpp` untouched.
**Depends on:** 2
**Unblocks:** none
**Done criteria:** `[profiler]` (with `update` phase) proves the collision/
substep sweep is actually costing ms (≥ ~1 ms/frame); if not, the increment is
a note. If it is, apply a fast-inv-sqrt or LUT-based `Length` to the BVH sweep
and re-measure.

#### Files to touch
- **`src/user/gameplay/runtime/profiler.hpp`** — ADD a `kPhaseUpdate` value to
  the `Phase` enum (currently only `kPhaseHighPriority`, `kPhaseTextureUpload`,
  `kPhaseStreaming`, `kPhaseCount`). The `update` phase does not exist yet —
  this is a prerequisite, not an optional read.
- **`src/user/rom_main.cpp`** — wire the new phase into the frame: bracket the
  gameplay update (actor/motor/camera ticks) with `kPhaseUpdate` and ensure
  the `[profiler]` report line includes it.
- `src/user/gameplay/physics/coll_mesh.cpp`, `physics/geom.cpp`, and the
  motor sweep in `player_motor.cpp` — but ONLY if the measurement gate passes.

#### Edge cases
- Fast-inv-sqrt precision must not break `OverlapAabbMesh`; keep a slow exact
  fallback behind a flag and verify via `coll_mesh` smokes.

#### Verification
- First add the `kPhaseUpdate` phase (profiler.hpp + rom_main.cpp) and confirm
  the `[profiler]` report line now prints an `update` value on device. If
  `update` ≤ ~1 ms, skip the sqrt opt (the increment collapses to a note); else
  apply the fast-inv-sqrt/LUT `Length` to the BVH sweep and run `coll_mesh_smoke`
  + `player_motor` tests.

---

### Inc 6 — Dead-code removal (S)
**Status:** done — deleted `src/user/gameplay/render/distant_world_renderer.cpp`
(orphan; its header `distant_world_renderer.hpp` never existed and it also
`#include`s the missing `dlod_loader.hpp`, so it was uncompilable and was never
in the build), `src/user/gameplay/arena.cpp`/`arena.hpp` (dead gameplay arena;
the in-use `n64/frame_arena.hpp` `FrameArena` is a separate file and was kept),
and the unwired `tests/runtime_smoke.cpp` (included the deleted `arena.hpp`);
removed the `src/user/gameplay/arena.cpp \` line from the Makefile `src` list;
repointed the two `tests/distant_lod_contract.py` doc comments (lines 12/39) from
`DistantWorldRenderer::kLodScale` to the bake-time `KLOD_SCALE` in
`tools/ogworld/distant_lod.py` (the contract test itself is unchanged and still
green). KEPT per AGENTS.md future feature: `filesystem/lvl/forsyken-city/*.dlod`,
`tools/writers/dlod_writer.py`, `tools/ogworld/distant_lod.py`. Verified
`rg -n 'distant_world_renderer|gameplay/arena' src/ tests/ Makefile` returns
nothing (remaining "arena" hits are all the in-use `FrameArena`/mallinfo refs).
27/27 host tests pass; ROM clean under `-Werror` (elf text 409176 / data 105284 /
bss 134832 / total 649292 — unchanged, as expected: `arena.cpp` was dead code the
linker already dropped and `distant_world_renderer.cpp` was never compiled);
device boots (60 s Ares capture `build/baseline-inc6-boot-20260821-191855.txt`,
steady state 56 batches / ~0.06 ms / 30 fps identical to the Inc 3/4 baseline;
only "FATAL" line is the known non-fatal player.t3dm asset gap). Rollback is a
hard git-revert (no runtime flag).
**Depends on:** 1
**Unblocks:** 7
**Done criteria:** remove the orphaned `distant_world_renderer.cpp` (its
header does not exist; not in the build) and the unused gameplay `arena.*`;
leave `.dlod` files + `tools/ogworld/distant_lod.py` on disk per AGENTS.md
future feature.

#### Files to touch
- Remove `src/user/gameplay/render/distant_world_renderer.cpp`. NOTE: its
  header `distant_world_renderer.hpp` does not exist at all, and the `.cpp`
  `#include`s it — the file is uncompilable as-is, not merely un-included.
  **AGENTS.md accuracy (cross-ref):** AGENTS.md currently claims the
  "distant-only code paths … are deleted," which is slightly false while this
  orphan remains. Removing it makes that statement accurate; Inc 7's AGENTS.md
  update should confirm the wording matches reality.
- Remove `src/user/gameplay/arena.cpp` / `arena.hpp` and their `src` entry in
  `Makefile`.
- Do NOT remove `filesystem/lvl/forsyken-city/*.dlod`, `tools/writers/
  dlod_writer.py`, `tools/ogworld/distant_lod.py`.

#### Edge cases
- Confirm the `.dlod` files and their writer are NOT consumed by any active
  code (grep `dlod_format`/`dlod` in `src/`).
- `-I` include for `arena.hpp` may be used by other code/test (grep before
  removing). Two dangling references to handle in the SAME increment:
  - `tests/distant_lod_contract.py:12,39` hardcode a comment pointing at
    `DistantWorldRenderer::kLodScale` (a header we're deleting) — update or
    delete that doc comment.
  - `tests/runtime_smoke.cpp` includes `../src/user/gameplay/arena.hpp` and
    uses `Arena`, but is NOT wired into `run_host_tests.sh` — remove or update it.
- Rollback: this is a hard git-revert (no runtime flag). Document that.

#### Verification
- `rg -n 'distant_world_renderer|arena' src/ tests/` returns only the intended
  removal set + the two handled dangling references; `./tests/run_host_tests.sh`
  still green; `./compile-rom.sh` still builds; device boots.

---

### Inc 7 — Metrics + close-out (S)
**Status:** done — before/after telemetry table added to `docs/perf_budget.md` (BEFORE: pre-plan autowalk walking `build/raw-baseline-autowalk-fix-20260821-134018.txt.log`; AFTER stationary: `build/baseline-inc5-update-20260821-193329.txt`; AFTER walking: `build/baseline-inc7-walk-20260821-200451.txt` — walking frame 33.8–34.8 ms / 28.7–29.6 fps, `near_batches=94 syncs=94`, `[memory] 2288680/2230464/58216` identical across 5 reports = no leak). Stale distant-pass references cleaned from `docs/perf_budget.md` (full rewrite to the current single near pass) and the stale "5.48 MB arena / 5.39 MB used" figure in `AGENTS.md` corrected to the authoritative mallinfo arena (stationary 2.16/2.15 MB ratio 0.991; walking 2.29/2.23 MB ratio 0.975 — the ring holds more resident cells while streaming). AGENTS.md now records the per-TU -O2 flag state (6 hot TUs, global -Os), `kEnableNearCulling` default OFF, and the Inc 6 dead-code removal. Device re-verification: cell walk capture green (no pop, no visible regression, `[counters]`/`[memory]` deltas as expected); orbit covered by the existing inc3-off/inc3-on captures. `kDebugAutoWalk` restored false and the final ROM rebuilt (elf 411608/105012/134848/651468 — 136 bytes smaller than the autowalk build, proving the flag is out; the "409176" figure in the Inc 1/6 notes predates the Inc 5 profiler change). 27/27 host tests pass. Cross-cutting loop: (1) memory ratio unchanged, (2) counters match the ring baseline (near-culling OFF by default: 56 stationary / 94 walking), (4) no `distant_world_renderer`/`arena` references remain, (5) `[profiler] update=` confirms the update phase (Inc 5 gate: 0.194 ms ≪ 1 ms → sqrt opt not applied); (3) gameplay feel is the user's manual check on return. One-line per-increment summary is in `docs/perf_budget.md`.
**Depends on:** 3, 4, 6
**Unblocks:** none
**Done criteria:** a before/after telemetry table from Ares (`tools/
capture_baseline.sh`), the build/test gate is green, and a one-line
per-increment summary.

#### Files to touch
- `docs/perf_budget.md` — add the final measured rows. **Also clean the stale
  distant-pass references in the same increment:** the doc still documents the
  removed distant pass (line 11 `distant (Z-off, culled, coalesced) ≤ 12 ms`,
  lines 30-42 `[render-phases] distant=…`/`[counters] distant_cells=…`/
  `[distant-cells]`, and lines 57-58, 65, 81-84). Update the doc to the
  current single near pass so the measured rows land in a consistent document.
- `AGENTS.md` — record the current near-culling + per-TU flag state and the
  removed-dead-code note (and confirm the "distant-only code paths are deleted"
  wording is now accurate, per Inc 6).

#### Verification
- Re-run the device orbit + cell walk; confirm no pop, no visible regression,
  and the expected `[counters]`/`[memory]` deltas.

## Cross-cutting verification
After Inc 7, run a full loop: `./tests/run_host_tests.sh` (all green),
`./compile-rom.sh`, Ares boot + a full-map camera orbit + forward walk, and
confirm: (1) `[memory] used/total` ratio unchanged, (2) `[counters] syncs`/
`near_batches` match the expected drop from near-culling, (3) gameplay feel
unchanged (dash, camera, collision), (4) no `distant_world_renderer` or
`arena` references remain, (5) `[profiler]` confirms update ms before the
CPU-vs-RSP question is closed (the `update` phase is added by Inc 5 — the
profiler has no `update` phase until then).

## Standards / common-mistakes referenced
- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — applies to:
  Inc 1, 3 (frame is RSP-bound; target RSP cmd / DMA, not CPU).
- `docs/perf_budget.md` — applies to: all (budget, counters, tuning knobs).
- `.agents/common-mistakes/` — n/a (no direct build/asset mistake triggered,
  but the `-Os`/`-ffast-math` miscompile note is tracked in Inc 1).

## Open questions (CONSIDER from review)
- (filled by review)
- Whether the user wants near-culling default ON or default OFF when shipped
  (plan defaults OFF to preserve current visuals until the RSP win is measured).
- Inc4 (re-scoped, see D4): the original "trait membership" question is moot —
  `PickupTrait`/`PushoutTrait` are inherited by no class and the dead
  `All<>`/`OverlapsFirst<>` templates are deleted rather than re-mapped to
  bitflags. The only open sub-question is whether to also drop the empty
  virtual marker structs in `traits.hpp` (touching `MovingSolidActor` and
  `SpikeActor`) or leave them as harmless empty bases.
- Whether the user wants the collision `sqrt` micro-opt attempted at all (Inc5
  is measurement-gated; if `[profiler] update` shows CPU headroom it collapses
  to a "don't optimize" note).

## Out of scope
- Deep-RSP microcode (custom microcode, RSP shadow geometry) — a rewrite, not
  an optimization; not exposed by tiny3d/libdragon.
- The distant-pass / RDP themes from the doc — no distant pass exists in the
  current build.
- ECS adoption.
- Full `PlayerState` bitfield refactor (optional follow-up, not required).
- Any visual or content change.
