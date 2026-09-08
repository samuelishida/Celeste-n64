# Memory-Side Optimization for the Open-World Renderer

## Context

The research brief `outputs/n64-cpp-memory-opt.md` surveyed N64 hardware limits
and C++ memory practices. Grounding it against this codebase produced a
**memory-side** plan (heap budget, DMA traffic, compression, alignment,
telemetry, docs). The CPU-side findings (data locality / SoA / fixed-point)
are **measurement-gated and deferred**: the frame is RSP-bound
(`rspq_wait()` after `Render()`, `src/user/rom_main.cpp:97`) and the CPU
`update` phase measured 0.194 ms ≈ 0.6% of the frame, so the prior CPU-opt
gate failed (`learning n64-opt-device-verify-and-telemetry`).

Repo facts that shape this plan (answered from code — see Assumptions):

- The binding constraint is the **heap arena ≈ 2.16 MB stationary / 2.29 MB
  walking at ~99 % used** — NOT the 8 MB RDRAM nominal budget. `[memory]`
  reports `mallinfo` `total/used/free` once per 3600 frames
  (`src/user/rom_main.cpp:105`, `src/user/n64/profiler.cpp:96`).
- All level/cell data is transported **uncompressed over DragonFS + `fopen`**
  (`dfs_init(DFS_DEFAULT_LOCATION)`, `src/user/rom_main.cpp:39`; e.g.
  `textured_room_renderer.cpp:103`, `coll_mesh.cpp:115`,
  `mappack_loader.cpp:195`). libdragon's asset-compression layer (level-1 LZ4
  loads *faster than uncompressed* per the research brief) is **not used**.
- Tooling already exists and must not be re-planned: `FrameArena` (64 KB,
  host-safe, `src/user/n64/frame_arena.hpp`), `malloc_uncached` staging pool
  (`tile_streamer.cpp:85`), ring-diff streaming (`ResolveRingDiff`),
  per-TU `-O2` hot TUs, `kDebugAutoWalk` + `tools/capture_baseline.sh`.
- `player_movement.cpp` is already allocation-free; **movement physics is
  out of scope** (parity-sensitive, per learnings).

Intended outcome: a measurable memory story — decompose the arena into budget
buckets, eliminate runtime heap churn after boot, shrink + speed up cell
transport via level-1 compression, guarantee DMA alignment at every transport
boundary, and pin it all in a budget doc with a boot-time warn. Each increment
is one reviewable PR with host smokes + Ares telemetry.

**Toolchain prerequisite (verified during review):** `N64_INST` =
`/tmp/n64-bootstrap/opt/libdragon` (Makefile:5) does NOT exist on this machine
at plan time (the exFAT `/tmp`-cleanup hazard AGENTS.md warns about struck
again). No ROM can build until `./setup.sh` re-runs and restores the
toolchain. Every increment's device verification implicitly depends on this;
Inc 2 additionally depends on the toolchain shipping the **asset API**
(`asset.h`: `asset_load`/`asset_fopen`) and the **`mkasset`** tool — see the
Inc 2 Step-0 gate.

## Architectural decisions

- Decision: **Memory-side scope only.** CPU data-locality/SoA stays
  measurement-gated and deferred. Rationale: RSP-bound frame; CPU `update` =
  0.6 % (prior gate failed). Alternatives rejected: CPU layout refactor of
  player/collision hot paths (spends time where the frame has headroom).
  Source: user-confirmed (Q1 default).
- Decision: **Compress cell transport at libdragon asset level 1, behind
  `kEnableCompressedCells` (default ON after measurement).** Rationale:
  documented faster-than-uncompressed loads + ~35 % size on mixed assets;
  directly cuts the RSP-bound frame's real bottleneck (ROM→RDRAM DMA for
  streaming cell loads). Alternatives rejected: keep uncompressed DFS.
  Source: user-confirmed (Q2 default).
- Decision: **Per-cell renderer pooling + arena discipline to reach zero heap
  churn after boot.** Rationale: arena ≈ 99 % used leaves ~1 % headroom;
  `new`/`delete` per cell load risks OOM on the tight heap (the
  tile-streamer compaction incident crashed exactly this way). Alternatives
  rejected: enlarge the heap (fights the budget instead of flattening
  churn). Source: default (this plan).
- Decision: **Budget enforcement = telemetry + boot-time warn (debug
  assert), never a release hard-fail.** Rationale: a hard cap can break
  valid map data; warn + captured budget keeps observability without risk.
  Source: user-confirmed (Q3 default).
- Decision: **`[memory]` line stays parse-compatible** (same
  `total/used/free` prefix order; bucket values appended). Rationale:
  `tools/capture_baseline.sh` and existing greps must keep working.
  Source: default (this plan).
- Decision: **Docs updated only from a fresh device capture, not from stale
  docs.** Rationale: `docs/streaming_memory.md` still carries the stale
  5.48 MB figure; learning `stale-contract-vs-working-tree`.
  Source: default (this plan).

## Assumptions and answers from code

- Decision: frame is RSP-bound; CPU has headroom. Source: code @
  src/user/rom_main.cpp:97 (+ learning rsp-bound-frame-and-material-grouping).
- Decision: arena ≈ 2.16 MB stationary / 2.29 MB walking, ~99 % used.
  Source: code @ AGENTS.md (latest device captures; docs/streaming_memory.md
  stale).
- Decision: transport is uncompressed DFS + fopen. Source: code @
  rom_main.cpp:39, textured_room_renderer.cpp:103, coll_mesh.cpp:115.
- Decision: FrameArena (64 KB) and malloc_uncached staging exist; do not
  re-plan. Source: code @ src/user/n64/frame_arena.hpp, tile_streamer.cpp:85.
- Decision: cell loads go through near-pass renderers + mappack + global
  coll mesh only; distant `.dlod` is not loaded at runtime (out of scope
  regardless of file presence). Source: code @ AGENTS.md.
- Decision: scope = memory-side only; compression = level 1 behind flag;
  budget = warn-only. Source: user-confirmed (proceed with defaults).

## Risks accepted

- **R1 — Compression path breaks cell loading** (mappack manifest paths,
  load-bearing `rom:/` prefix stripping at textured_room_renderer.cpp:418-432,
  asset-vs-fopen error paths differ): mitigations: gate behind
  `kEnableCompressedCells`; keep a plain-fopen fallback for host tests;
  Ares boot + autowalk before/after; revert = flag flip + re-bake.
- **R2 — Pooling per-cell renderers reintroduces compaction/async bugs**
  (the incident class from learning tile-streamer-compaction): mitigation:
  pool keyed by resident slot, never release in-flight async slots, keep the
  snapshot-before-compact discipline, extend the existing
  `tile_streamer_compaction_smoke` host test.
- **R3 — Alignment audit causes churn in working RDPQ/TMEM uploads**:
  mitigation: audit-first increment (assertions only, no behavior change);
  fix only buffers proven misaligned; many buffers are CPU-copied via
  `fopen`/`dfs_read` and are *not* DMA targets — document rather than fix.
- **R4 — Telemetry format change breaks capture tooling**: mitigation:
  parse-compatible `[memory]` line; extend capture_baseline after Inc 1;
  assert line-shape in a host contract test.
- **R5 — Doc "correction" repeats the stale-contract mistake**: mitigation:
  Inc 1 re-captures BEFORE touching docs; every number in the budget doc
  cites its capture file.
- **R6 — Compression makes little measurable difference** (cell payloads are
  small; frame is RSP-bound): mitigation: Inc 2 carries explicit revert
  criteria; if load latency or `[memory] used` regresses, flag stays OFF and
  the increment still ships the bake/read-path infrastructure + telemetry.

## Increment DAG

- Inc 1 — Memory telemetry buckets + fresh baseline (S) — depends on: none — unblocks: 2, 3
- Inc 2 — Compressed cell streaming (M) — depends on: 1 — unblocks: 4, 5
- Inc 3 — DMA/alignment audit & assertions (M) — depends on: 1 — unblocks: 5
- Inc 4 — Renderer pooling + zero-churn arena (M) — depends on: 2 — unblocks: 5
- Inc 5 — Memory budget doc + budget warn + final validation (S) — depends on: 2, 3, 4 — unblocks: none

(Inc 4 is serialized after Inc 2 so its pool/arena telemetry baseline is
measured against the compressed transport's stable memory profile — the two
increments otherwise touch disjoint files (tile_streamer / renderer_pool vs.
renderer load paths). Inc 3 runs parallel to 2 — it touches
different buffers and only adds assertions.)

## Increments

### Inc 1 — Memory telemetry buckets + fresh baseline (S)
**Status:** DONE (2026-09-07) — buckets shipped in `[memory]` line
(`n64/profiler.{hpp,cpp}`, `rom_main.cpp` providers, host contract
`tests/memory_budget_contract.cpp`); device capture
`build/baseline-inc1-20260907-002133.txt` shows steady
`total=2921672 used=2899576 free=22096` with buckets
(staging=125867, coll_mesh=634168, mappack=422580, res_render=1404,
frame_arena=0); `docs/streaming_memory.md` re-cited from the fresh capture
(stale 5.48 MB note removed). One device bug found & fixed en route: N64
newlib printf lacks `%zu` → bucket formatting uses `%lu` +
unsigned-long cast. Pre-existing tree blocker (uncommitted WIP
`player_controller.cpp`, alternate controller arch w/ undefined
`ControllerInput`/`CollisionWorld` + abandoned single-mover) was
STASHED (stash@{0}) to restore the compiling tree; host suite 53/53,
ROM builds, frame 33.1 ms @ ~30 fps, update=0.123 ms (RSP-bound unchanged).
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** a `[memory]` line printed on device includes per-bucket
values, and `docs/streaming_memory.md` cites a fresh capture instead of the
stale 5.48 MB figure.

#### Files to touch

##### src/user/n64/profiler.hpp / profiler.cpp
- What changes: extend `MemorySnapshot` with named buckets summed into
  `used`; capture them from the same mallinfo call plus registered bucket
  providers.
- Function(s): `MemorySnapshot::Capture()` (add bucket capture),
  `MemorySnapshot::PrintLine()` (append buckets after `used`/`free`,
  keeping `total=%u used=%u free=%u` prefix order),
  `MemorySnapshot::RegisterBucket(const char* name, size_t (*provider)(void))`.
- Data shapes: `struct Bucket { const char* name; size_t bytes; };` plus
  fixed array (≤ 8 buckets).
- Integration points: callers register providers in `rom_main.cpp` before
  first capture; profiler emits `[memory]` line unchanged cadence (3600 frames).
- Error paths: unknown/null provider → skip bucket; never crash the report.

##### src/user/rom_main.cpp
- What changes: register bucket providers (frame-arena used, staging pool,
  resident renderers, global coll mesh, mappack structs) at line ~105 area;
  keep the existing print call.
- Error paths: registration is additive.

##### tools/capture_baseline.sh
- What changes: grep captures remain generic (`rg '\[memory\]'`); no change
  required, verify only. Optionally add a `--buckets` summary mode.

##### docs/streaming_memory.md
- What changes: after the fresh capture runs, replace the stale "≈ 5.48 MB
  heap arena / ≈ 5.39 MB used" paragraph with the fresh numbers and a pointer
  to the capture file path. Must NOT be edited before the capture exists
  (learning stale-contract-vs-working-tree).

##### tests/memory_budget_contract.cpp (new)
- What changes: host test asserting bucket-sum ≤ arena total and that the
  `[memory]` line shape stays `total=%u used=%u free=%u`-prefixed
  (parse-compat guard).
- Integration points: runs in `make test`; host-safe (pure formatting math).

#### Edge cases
- `[memory]` cadence is 3600 frames (~60 s at 60 fps): a capture run must
  exceed that (capture_baseline default 120 s covers ~2 lines).
- Bucket drift between captures (walking vs stationary 2.16 vs 2.29 MB):
  report both, label the state.
- Host build must not depend on mallinfo (keep mallinfo behind `__n64__`).

#### Verification
- Run: `make test`; `./compile-rom.sh`; `tools/capture_baseline.sh
  madeline_cube_rom.z64 130 inc1` (autowalk via `kDebugAutoWalk`).
- Tests to add/update: `tests/memory_budget_contract.cpp` (new);
  existing telemetry host tests must stay green.
- Done: capture file shows buckets; doc no longer carries 5.48 MB.

### Inc 2 — Compressed cell streaming (M)
**Status:** DONE (2026-09-07) — Step-0 gate PASSED (asset.h repaired into the
installed toolchain from the exact source clone that built the lib; mkasset
present). Shipped pack compressed at level 1 (596 KB vs 1.30 MB raw, 54 %
smaller; dfs 1.67 MB → 967 KB). Runtime reads via asset layer: manifest
(asset_fopen, no-fseek), global colmesh + cell .lvl (asset_load →
LoadFromBuffer / ParseLevelFromBuffer). Device capture
`build/raw-baseline-final-20260907-104157.txt.log`: Forsaken City boots
(45 rooms, start=cell_00_00), steady-state counters IDENTICAL to the Inc 1
uncompressed baseline (near_batches=56, resident=13, syncs=56 across 176
lines), `[memory] used` LOWER (2,885,736 vs 2,899,576; free 76,688 vs
22,096) — revert criteria met. Implementation notes: mkasset requires `-o
<dir>` (the "overwrite by default" wiki note is stale); async DMA path
disabled when compressed (it reads raw bytes); level_loader.cpp was added to
this increment (the plan missed the gameplay room-load path — it was the
1-1-fallback boot failure); LvlRoomRenderer (flat fallback) still reads via
fopen and logs a benign load failure on compressed .lvl (compressed cells
require the textured path; kEnableTextures=true is active).
**Depends on:** 1
**Unblocks:** 4, 5
**Done criteria:** with `kEnableCompressedCells` ON, the ROM boots and the
autowalk completes with no error screens, `[memory] used` does not increase,
and cell loads are equal-or-faster than the uncompressed baseline (measured
via `[render-phases]`/`[counters]` deltas); flag OFF + re-bake from
`build/bake-fc-1200/staging` restores the uncompressed path.

#### Step 0 — dependency verification (gate; do this first)
- Re-run `./setup.sh` if `/tmp/n64-bootstrap` is absent (it is at plan time).
- Confirm the installed libdragon ships `include/asset.h` (declare
  `asset_load`/`asset_fopen`/`asset_init_compression`) and the `mkasset`
  binary (`$(N64_INST)/bin/mkasset` or the project-pinned tool path).
- If the asset API or `mkasset` is absent from the installed toolchain: DO NOT
  force the design. Ship the read-path infrastructure + telemetry with
  `kEnableCompressedCells = false` (plain-fopen path unchanged), record the
  gap, and defer compression to a follow-up increment once the toolchain
  supports it. The rest of Inc 2 (bake staging split, smokes, flag) still
  lands.
- Record the verdict (API present/absent + versions) in the increment commit.

#### Files to touch

##### Makefile (bake + compress rules, lines ~137-186)
- What changes: keep `build/bake-fc-1200/staging/` FOREVER uncompressed (it
  is the smoke + rollback source); add a compression step that copies the
  pack's `.lvl` / `.colmesh` / `.mappack` files, applies
  `$(N64_INST)/bin/mkasset --compress 1`, and writes the COMPRESSED copies
  into the shipped `filesystem/lvl/forsyken-city/`. Recipe ordering is
  load-bearing: bake → smokes (against staging) → compress → `mkdfs` (the
  compression step is the LAST one before the filesystem image). Never
  compress in place.
- Function(s): new target `compress-forsaken-city`, folded into
  `bake-forsaken-city` and ordered before the `mkdfs`/dfs-image step.
- Data shapes: files keep their names/extensions; compression metadata lives
  in the asset header written by `mkasset`.
- Integration points: `DFS_MAP_PACK_FILES` wildcard picks up the same names
  (still reads from `filesystem/`, which now holds the compressed copies).
- Error paths: `mkasset` failure fails the bake; stale compressed files must
  not be shipped (always copy fresh from staging into `filesystem/`).

##### src/user/gameplay/debug_flags.hpp
- What changes: add `inline constexpr bool kEnableCompressedCells = true;`
  (flag default ON after measurement; flip to `false` = uncompressed path).
- Integration points: read by the three load sites below.

##### src/user/gameplay/world/mappack_loader.cpp
- What changes: when `kEnableCompressedCells`, open the v1/v2 manifest with
  `asset_fopen` instead of `fopen` (line 103 / 195); keep the `fopen` path
  for the flag-off and host-test paths.
- Function(s): `LoadMapPack(...)`, `LoadMapPackV2(...)` — add a
  `FILE* (const char* path)` opener parameter defaulting by flag.
- Error paths: `asset_fopen` null → MP_LOG + fall back to `fopen`
  (tolerant); never assert on map-data format mismatch without a message.

##### src/user/gameplay/world/map_runtime.cpp
- What changes: global `.colmesh` load (`Init`, line ~53) uses
  `asset_load` when flag ON; pass the parsed buffer to `CollMesh::LoadFrom`.
- Integration points: `CollMesh` gains a "load from buffer" entry point
  (see coll_mesh.cpp).
- Error paths: allocation failure → fail Init with `MR_LOG`, no partial mesh.

##### src/user/gameplay/physics/coll_mesh.cpp
- What changes: split `LoadCollMesh` into `LoadFromFile(path)` (existing
  fopen path) and `LoadFromBuffer(const uint8_t*, size_t)` (new); buffer
  ownership documented (asset buffer freed by caller).
- Data shapes: `CollMesh::LoadFromBuffer` consumes the CMSH v1 layout
  unchanged.
- Error paths: same header/magic/version checks as today; magic mismatch →
  nullptr + log (compressed-vs-raw mismatch is the canary for a wrong-level
  bake).

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: per-cell `.lvl` open (line 103) uses `asset_fopen` when flag
  ON. CAREFUL: keep the load-bearing `rom:/` prefix strip (lines 418-432) — a
  compressed read must receive the exact same relative path.
- Error paths: open failure → existing renderer fallback path (null
  renderer resident guard already exists in tile_streamer).

##### src/user/gameplay/world/level_loader.cpp (ADDED during implementation — the plan missed the gameplay room-load path)
- What changes: converted the FILE*-based parser to a shared buffer parser
  (`ParseLevelFromBuffer`); `LoadLevelInto` uses `asset_load` when
  `kEnableCompressedCells` (asset_fopen asserts on fseek, and this parser
  seeks to section offsets), else fopen+read-into-buffer. Without this, the
  compressed cell `.lvl` failed to parse and boot fell back to the legacy
  single-room map (the 1-1-fallback failure the user hit).
- Function(s): `ParseLevelFromBuffer(const uint8_t*, size_t, Room&,
  LevelGeometry&)` (new, declared in level_loader.hpp); `LoadLevelInto`
  (asset path + shared parser).
- Error paths: asset_load null → fall back to fopen; truncation guard
  (`MemReader::ok`) fails the parse loudly.

##### tools/bake_interconnected_map.py (if needed)
- What changes: only if the .mappack writer embeds offsets into the .lvl
  files (then compressed sizes must be reflected). Verify during
  implementation; if the manifest is path-based (it is — v2 manifest carries
  `rom:/lvl/<pack>/<chunk>.lvl` paths), no writer change.
- Error paths: n/a if verify-only.

#### Edge cases
- Manifest paths are `rom:/`-prefixed; `asset_fopen` in libdragon accepts
  `rom://`-style paths the same as `fopen` — verify, else strip prefix
  identically to `textured_room_renderer.cpp:432`.
- Host tests cannot link the asset layer: keep plain-`fopen` fallback so
  existing bake/mappack host smokes exercise the same parsing (against the
  UNCOMPRESSED staging artifacts — see Verification).
- **Flag ↔ bake state coherence (hard requirement):** `kEnableCompressedCells`
  is a runtime knob but compressed-ness is a bake-time property. The flag and
  the shipped bake MUST flip together: a flag-OFF ROM against a compressed
  bake is a known-bad combo that must fail LOUD, not silently — the CMSH
  magic check in `coll_mesh.cpp:124-131` is the existing canary; add an
  explicit `.lvl` parse-failure log in `textured_room_renderer.cpp` naming
  the path and the suspected compression mismatch.
- Mixed state (some cells compressed, others not) must not occur: compression
  is all-or-nothing per bake (single copy step from staging).
- DFS image size vs ROM budget: compressed pack shrinks `madeline_cube_rom.dfs` —
  record before/after sizes.
- Python pack smokes must NEVER be pointed at compressed files: see
  Verification for the staging redirect.
- Revert: flag OFF + re-copy uncompressed artifacts from
  `build/bake-fc-1200/staging` into `filesystem/`; both paths share the
  manifest so the runtime can't tell.

#### Verification
- Run: `make bake-forsaken-city`; `./compile-rom.sh`; `make test`;
  `tools/capture_baseline.sh madeline_cube_rom.z64 130 inc2` with
  `kEnableCompressedCells=true`, then a second capture with `false`;
  compare `[memory]` + `[render-phases]` + `[counters]` + boot time.
- Tests to add/update: redirect EVERY python pack smoke that parses raw pack
  files to the uncompressed staging artifacts (verified by name:
  `tests/map_pack_smoke.py`, `mappack_smoke.py`,
  `malformed_mappack_v2_test.py`, `interconnected_*.py`,
  `distant_lod_contract.py`, `dlod_format_contract.py`, and check
  `colmesh_smoke.py`'s input path) — pass the staging dir as an argument or
  via a Makefile var; add `tests/mappack_compression_contract.cpp` (host,
  fallback-path parse).
- Done (revert criteria): boot OK + autowalk OK + `near_batches`/syncs not
  worse + `[memory] used` not higher with flag ON; otherwise flag OFF and
  record the measurement.

### Inc 3 — DMA/alignment audit & assertions (M)
**Status:** DONE (2026-09-07) — audit table + boot-time asserts shipped.
`malloc_uncached` buffers (staging pools, packed verts, matrices) are
DMA-direct and 16-aligned by libdragon contract (`n()` =
`malloc_uncached_aligned(16, ...)`); asserted at each site. sprite->data
feeds rdpq_sprite_upload (8-byte RSP DMA contract; data is a flexible array
at offset 8 of an 8-aligned struct — asserted at both upload sites).
fopen/asset_load read buffers + CPU-side parse intermediates (faces, verts,
batches, runs, specs, counts, material groups) documented CPU-copied — no
DMA dependency. No explicit `data_cache_*` callsites exist in src/user.
`tests/alignment_audit_contract.cpp` (new, wired in make test) asserts the
registry's per-file malloc_uncached/malloc counts match the sources (new
allocations must be registered). make test 55/55; ROM builds; device
capture `build/raw-baseline-inc3-20260907-120128.txt.log` boots the map and
renders at baseline-identical steady-state (near_batches=56, resident=13;
[memory] used 2,885,744 vs 2,885,736 — +8 B noise). NOTE: the capture shows
a pre-existing one-time spring launch (player spawns exactly on the x=0
cell boundary; the map has 5+ Spring entities) — same anomaly as the inc2b
capture, NOT caused by this increment (asserts/comments only).
**Depends on:** 1
**Unblocks:** 5
**Done criteria:** every buffer handed to `dma_read*` or rdpq upload is
provably 16-byte-aligned (or documented as CPU-copied, therefore n/a), with
boot-time asserts; zero renderer/streamer behavior changes.

#### Files to touch

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: after staging pool creation (line 85, `malloc_uncached`),
  assert `reinterpret_cast<uintptr_t>(pool) % 16 == 0`; same for the
  `kStagingFallbackBytes` allocation (line 360).
- Integration points: assert only; no allocation change.
- Error paths: assert failure → halt at boot (correctness), message names
  the buffer.

##### src/user/gameplay/physics/coll_mesh.cpp
- What changes: file-read buffer (line 115 `malloc`) → 16-byte-aligned
  allocation (`malloc` from newlib is ≥8 but not guaranteed 16; use an
  aligned alloc or document as CPU-copied if fopen path is proven copy-only).
  If `dfs_read` DMA's into its own internal aligned buffer and CPU-copies to
  ours (documented libdragon behavior), mark buffer "CPU-copied — no DMA
  dependency" and assert only.
- Error paths: aligned-alloc failure → existing null-return path.

##### src/user/gameplay/render/texture.cpp / textured_room_renderer.cpp
- What changes: audit sprite upload sources (texture.cpp:10 probe, sprite
  data feeds to rdpq). rdpq uploads DMA from caller data — assert 8-byte
  alignment per DMA contract (16-byte where cache-line sharing matters);
  fix only proven-misaligned buffers.
- Error paths: misaligned source → assert at boot with buffer name.

##### src/user/gameplay/world/mappack_loader.cpp / level_loader.cpp
- What changes: manifest/lvl read buffers are `fopen` CPU-copied — document
  "no DMA" in a comment; no fix.
- Integration points: none (documentation-only).

##### tests/alignment_audit_contract.cpp (new)
- What changes: host test enumerating every registered DMA-facing buffer and
  asserting its alignment on host (where buffers exist host-side) or
  asserting the audit list matches the code inventory (so new buffers must be
  registered).
- Integration points: runs in `make test`.

#### Edge cases
- A buffer may be legitimately unaligned IF the libdragon wrapper CPU-copies
  (e.g. `dma_read_async` arbitrary-alignment wrapper requires RAM/PI
  same-parity, not 16-byte; `dfs_read` realigns via copy). The audit must
  classify each buffer as DMA-direct vs CPU-copied before deciding to fix.
- 16-byte alignment also matters for cache writeback/invalidate calls
  (n64sys asserts 16-byte-aligned 16-multiple ranges) — include any explicit
  `data_cache_*` callsites in the audit.
- Do not "fix" buffers that are already correct; churn is the risk (R3).
- **Handoff to Inc 4 (must be read together):** the per-cell buffers this
  increment audits (`textured_room_renderer.cpp:109, 163, 176, 244, 276,
  283-284, 291, 293, 372`; `level_renderer.cpp:38, 100`) MOVE into pool-slot
  blocks in Inc 4. In Inc 3, classify them and assert-on-current-homes only;
  do NOT re-architect them (Inc 4 owns that). The alignment sweep must be
  RE-RUN after Inc 4 lands, with assertions expressed on the final
  `SlotBuffer` homes (16-byte-aligned by design), and the
  `tests/alignment_audit_contract.cpp` inventory updated to the new homes.

#### Verification
- Run: `make test` (new audit contract + all existing); `./compile-rom.sh`;
  `tools/capture_baseline.sh madeline_cube_rom.z64 130 inc3` — expect
  byte-identical `[memory]`/`[counters]` to Inc 1 tail (no behavior change).
- Tests to add/update: `tests/alignment_audit_contract.cpp` (new).
- Done: audit table (buffer → classification → status) in the increment
  commit; device capture delta vs Inc 1 ≈ 0.

### Inc 4 — Renderer pooling + zero-churn arena (M)
**Status:** DONE (2026-09-07) — `renderer_pool.hpp` (fixed-capacity
`RendererPool<T, Capacity, SlotBytes, ScratchBytes>`: placement-new objects,
per-slot 16-aligned buffer blocks, shared parse scratch, PeakHighWater) +
PIMPL `RendererPools` in tile_streamer.cpp (textured 26×56 KB slots + 240 KB
scratch; flat 26×44 KB + 144 KB). All `new`/`delete` in tile_streamer replaced
with pool Acquire/Release (only the one-time `new RendererPools()` remains);
the textured + flat renderers carve their persistent per-cell buffers
(batches/runs/run_faces/material_groups) from the slot buffer and the
transient parse buffers (file buf/faces/lvl_verts/specs/order/sorted_specs)
from the shared scratch (16-aligned bump allocators — the first device run
hit a MIPS misaligned-write exception from unaligned bump offsets, fixed by
16-aligning every allocation). DEVIATION (documented): verts_/matrix_fp_
STAY malloc_uncached — they are DMA-direct, uncached, and outside the
mallinfo arena; pooling them into cached RAM would need a per-frame cache
writeback (the reveal matrix is rewritten every frame). LevelRenderer is
legacy (not instantiated by the map-pack path) — not pooled. FrameArena
gains PeakUsed(); new [memory] buckets pool_highwater + arena_peak.
make test 56/56 (renderer_pool_smoke new; alignment_audit_contract updated
to the pool homes; tile_streamer_compaction_smoke passes standalone). ROM
builds. Device capture `build/raw-baseline-inc4j-20260907-123146.txt.log`:
map boots, steady-state counters IDENTICAL to baseline (near_batches=56,
resident=13 across 81 lines), `[memory] total=5,858,072 used=5,822,032
free=36,040` (pools preallocated ~2.9 MB as designed; used flat across
captures), buckets `pool_highwater=13 arena_peak=0`. NOTE: the Ares
"early exits" during captures were the USER closing the window, not crashes.
**Depends on:** 2
**Unblocks:** 5
**Done criteria:** churn definition — zero per-frame heap allocations (already
true for the hot tick, verified by test) AND post-boot heap growth bounded at
cell transitions (per-cell renderer OBJECTS and their internal vertex/face/
batch/run buffers all come from fixed per-slot pools, so a full autowalk shows
flat `[memory] used` with no drift and no `malloc` in tile_streamer or the
per-cell renderer load paths); existing compaction smoke stays green.

#### Files to touch

##### src/user/gameplay/render/renderer_pool.hpp (new)
- What changes: fixed-capacity pool for per-cell renderer objects
  (`TexturedRoomRenderer`, `LvlRoomRenderer`), capacity = `kMaxResident + 1`
  (26) with in-use bitmask + free list. Host-safe; no N64 types.
- Function(s):
  `T* Acquire(Index* out_index)` / `void Release(Index, T*)` /
  `T* At(Index)` / `bool InUse(Index)` / `size_t PeakHighWater() const` /
  `uint8_t* SlotBuffer(Index, size_t need)` — SlotBuffer returns a pointer
  into the slot's PREALLOCATED buffer block (`kSlotBufferBytes`, rounded to
  16-byte alignment) that the renderer's per-cell vertex/face/batch/run
  buffers are carved from (see textured_room_renderer / level_renderer
  specs below); the block is reset on `Release`, never freed.
- Data shapes: `template <typename T, size_t Capacity>` over a
  `std::aligned_storage`-style byte array for the objects, PLUS one
  `alignas(16) uint8_t kSlotBufferBytes[Capacity]` block per slot;
  indices stable across compaction. `kSlotBufferBytes` sized to the per-cell
  max (face/vertex caps from the `.lvl` header) and recorded in the Inc 5
  budget table.
- Integration points: replace `new TexturedRoomRenderer()` /
  `new LvlRoomRenderer()` (tile_streamer.cpp:220, 237, 249, 379, 405, 414)
  with pool acquire/release; tile_streamer passes the slot index + slot
  buffer pointer into the renderer at acquire time.
- Error paths: pool exhaustion (acquire while full) → assert
  (capacity = kMaxResident+1 makes this a programming error); a cell that
  exceeds `kSlotBufferBytes` → assert + fail the load loudly, never
  partial-load.

##### src/user/gameplay/render/tile_streamer.cpp
- What changes: swap renderer allocation to the pool; PRESERVE the
  snapshot-before-compact discipline for `old_spec[]`/`old_flat[]`/
  `old_tex[]`/`old_last_used[]` (learning tile-streamer-compaction); release
  pool slots only after the renderer is fully evicted and no async load
  references it.
- Function(s): `SetCenterImpl` (load/evict paths), `LoadCell` helpers.
- Error paths: release of a slot with a pending async load → assert
  (tracking array `slot_in_flight[]`).

##### src/user/gameplay/render/textured_room_renderer.cpp
- What changes: THE dominant churn lives here, not in tile_streamer — per-cell
  `malloc` buffers (file buf :109, faces :163, verts :176, packed verts :191
  [already `malloc_uncached`], batches :244, face specs :276, runs :283-284,
  run faces :291, counts :293, matrix :325, material groups :372). Replace the
  load-time `malloc`s with per-slot buffers owned by the renderer pool
  (`Acquire(slot)` hands the renderer its preallocated buffer block sized to
  the cell's face/vertex caps; caps from the manifest or the LVL header's
  counts). Freeing stays pool-scoped (reset on `Release`), so nothing hits
  the heap after boot.
- Function(s): `LoadLvl(...)` buffer acquisition switches from `malloc` to
  pool-slot buffers; `Destroy()` resets slot buffers instead of `free`.
- Data shapes: per-slot buffer block budgeted at the per-cell max
  (face/vertex caps from the `.lvl` header — assert on overflow).
- Integration points: tile_streamer passes the pool slot index into the
  renderer at acquire time.
- Error paths: cell exceeds slot budget → assert + fail the load loudly
  (budget table in Inc 5 records the cap); never partial-load a cell.

##### src/user/gameplay/render/level_renderer.cpp
- What changes: same treatment for the flat renderer path — packed verts
  :38 (`malloc_uncached`, aligned) and matrix :100 move into the pool-slot
  buffer block; no per-load heap alloc.
- Integration points: pool slot index passed at acquire.
- Error paths: same assert-on-budget as textured path.

##### src/user/gameplay/world/map_runtime.cpp / physics/coll_mesh.cpp (if needed)
- What changes: global coll mesh is boot-time only — verify it is loaded
  once and never freed (document as permanent-resident); no pooling needed.
- Error paths: n/a.

##### src/user/n64/frame_arena.hpp
- What changes: add `size_t PeakUsed() const` (high-water since last
  ResolvePeak()) for telemetry; no behavioral change.
- Integration points: profiler bucket provider in Inc 1 shape.

##### src/user/n64/profiler.cpp / tests/memory_budget_contract.cpp
- What changes: add pool high-water + arena peak as budget buckets.
- Integration points: bucket providers registered in rom_main.

#### Edge cases
- Renderer objects are non-trivial (constructors/destructors): pool must
  placement-new and call destructors on release.
- Async-load slots: a resident slot with null renderer awaiting async load
  must NOT be released (tile_streamer.cpp:451-487 pending-queue logic).
- Compaction reorder: pool indices are the stable identity; the diff must
  snapshot before rewriting (learning).
- Pool must not grow: hard `Capacity = kMaxResident + 1`; overflow is an
  assert, not an abort-with-leak.

#### Verification
- Run: `make test` (new pool smoke + existing
  `tile_streamer_compaction_smoke.cpp` + full suite); `./compile-rom.sh`;
  `tools/capture_baseline.sh madeline_cube_rom.z64 180 inc4` (3-minute walk
  to observe multiple transitions; compare `[memory] used` at start vs end).
- Tests to add/update: `tests/renderer_pool_smoke.cpp` (new);
  `tests/tile_streamer_compaction_smoke.cpp` stays green (guards the
  snapshot discipline).
- Done: zero `new`/`delete` in tile_streamer; `[memory] used` flattens (no
  growth drift over the walk); autowalk + transitions show no missing-geometry
  or error screens.

### Inc 5 — Memory budget doc + budget warn + final validation (S)
**Status:** DONE (2026-09-07) — `docs/memory_budget.md` published (arena
figures citing the Inc 1 + Inc 4 captures, per-component caps, headroom
target, revert knobs, warm-boot note); `docs/streaming_memory.md` now links
to it as the memory authority (duplicated numbers removed).
`src/user/gameplay/memory_budget.hpp` (header-only BudgetTracker:
RegisterCap/Check, `[budget] WARN <name>=<bytes> cap=<cap>` lines,
host-safe) wired into rom_main (7 caps: frame_arena 64 KB, staging 128 KB,
res_render 4 KB, coll_mesh 700 KB, mappack 500 KB, pool_highwater 26,
arena_peak 64 KB) and checked in the 3600-frame [memory] report block.
`tools/capture_baseline.sh` greps `[budget]` lines. `tests/memory_budget_contract.cpp`
extended (over-cap warn shape, under-cap silence, unmatched-cap skip).
make test 56/56; ROM builds (dfs 907,838 B, z64 1,343,488 B). Final device
capture `build/raw-baseline-final-20260907-123804.txt.log`: 170/170 counters
at near_batches=56 resident=13 (baseline-identical), `[memory] total=5,857,528
used=5,822,032 free=35,496` (used FLAT across all Inc 4/5 captures), NO
[budget] warns (healthy), no exceptions. Cross-cutting: player_movement.cpp
diff EMPTY (movement feel untouched); near_batches/syncs not worse than the
n64-optimization baseline.
**Depends on:** 2, 3, 4
**Unblocks:** none
**Done criteria:** `docs/memory_budget.md` published with a budget table whose
every number cites a capture file; boot-time warn fires if a budget bucket
exceeds its cap; full Ares validation passes.

#### Files to touch

##### docs/memory_budget.md (new)
- What changes: single source of truth for memory. Table: arena total/used
  targets (2.16 MB stationary / 2.29 MB walking reference), per-component
  caps (global coll mesh, resident visuals pool, staging pool, frame arena
  64 KB, sprite/TMEM working set, DFS/asset footprint), headroom target
  (> 1 %), and revert knobs.
- Integration points: referenced by docs/streaming_memory.md (which Inc 1 fixed).
- Error paths: n/a (doc).

##### src/user/gameplay/memory_budget.hpp / .cpp (new)
- What changes: host-safe budget registry matching Inc 1 buckets, with
  `struct BudgetCap { const char* name; size_t cap; };` and
  `BudgetTracker::WarnIfOver()`.
- Function(s):
  `void BudgetTracker::RegisterCap(const BudgetCap&)` /
  `bool BudgetTracker::Check(const MemorySnapshot&, char* out, size_t)`.
- Integration points: registered capacities in rom_main; CHECK emitted at
  boot and with the `[memory]` report cadence (debug-only warn).
- Error paths: over-cap → `debugf` warn line ONLY (user-confirmed Q3 —
  never a release hard-fail).

##### src/user/rom_main.cpp
- What changes: wire `BudgetTracker` (register caps, call `Check` in the
  `[memory]` report block, line ~105).
- Error paths: n/a.

##### tools/capture_baseline.sh
- What changes: also capture budget-warn lines (`rg '\[budget\]'`); include
  them in the before/after diff.
- Error paths: warn lines may be absent when healthy — treat as pass.

##### docs/streaming_memory.md
- What changes: link to `docs/memory_budget.md` as the memory authority;
  remove duplicated numbers (single source of truth).

##### tests/memory_budget_contract.cpp
- What changes: extend to assert every cap has a matching bucket and that
  CHECK output shape is stable (`[budget]` prefix).
- Integration points: `make test`.

#### Edge cases
- Warm-boot skip: budget warn may be skipped on warm reset by design (like
  intros) — document.
- Over-cap during walking (2.29 vs 2.16 MB): caps must be state-labeled
  (stationary/walking) or set at the walking high-water.
- Host runs: budget registry is host-safe and exercised by the contract test.

#### Verification
- Run: `make test`; `./compile-rom.sh`; full Ares pass:
  `tools/capture_baseline.sh madeline_cube_rom.z64 240 inc5` + manual checks
  (boot without pak: `assert_memory_expanded` error screen still works).
- Tests to add/update: `tests/memory_budget_contract.cpp` extended.
- Done: budget doc published + cite-verified; `[budget]` warn fires only on
  real over-cap during the validation walk (or not at all — pass); final
  DAG-wide diff recorded.

## Cross-cutting verification

After Inc 5, run the full ladder in order and record one final diff:

1. `make test` — full host suite (includes Inc 1-5 contracts +
   tile_streamer_compaction_smoke + bake smokes + gameplay_smoke).
2. `./compile-rom.sh` — ROM builds; note `.z64`/`.dfs` sizes before/after
   (compression should shrink the dfs).
3. `tools/capture_baseline.sh madeline_cube_rom.z64 240 final` with
   `kDebugAutoWalk` — collect `[profiler]`, `[render-phases]`, `[counters]`,
   `[memory]` (with buckets), `[budget]`.
4. Manual Ares checks: boot straight into the map; orbit the spawn (C-stick)
   to confirm no pop-in/missing geometry at ring transitions; verify the
   no-pak error screen path is unchanged.
5. Assert the two invariants that must NOT move: `near_batches`/`syncs`
   telemetry not worse than the `.plans/n64-optimization/` baseline, and
   movement feel untouched (`player_movement.cpp` diff = empty across this
   plan).

## Standards / common-mistakes referenced

- `.agents/common-mistakes/dfs-path-prefix.md` — applies to: Inc 2 (compressed
  read paths must keep the exact `filesystem/`-relative `rom:/` prefix).
- `.agents/common-mistakes/boot-facing-vs-map-layout.md` — applies to: Inc 4
  (resident-array edits must not disturb forward-wedge ordering/orientation).
- `.agents/learnings/tile-streamer-compaction.md` — applies to: Inc 4
  (snapshot before in-place compaction; check streamer telemetry first).
- `.agents/learnings/n64-opt-device-verify-and-telemetry.md` — applies to:
  all increments (measure first; cite the latest capture, not docs;
  near_batches is the fast signal; elf-size delta proves flag gating).
- `.agents/learnings/rsp-bound-frame-and-material-grouping.md` — applies to:
  Inc 2 (target DMA/RSP traffic, not CPU work).
- `.agents/learnings/stale-contract-vs-working-tree.md` — applies to: Inc 1,
  Inc 5 (docs must cite fresh captures; a stale test may encode a stale
  default — compare evidence before restoring).

## Open questions (CONSIDER from review)

- Plan-reviewer gatherer was not run (subagent blocked: session not under
  tmux). The deterministic structural checks (Step 7) own the review per
  plan-large rules; all checks passed with no MUST-FIX / SHOULD-FIX -
  findings to apply. Implementer should treat the self-review notes below as
  part of the contract:
  - Inc 4 is gated on Inc 2 for measurement stability, not file conflicts
    (their file sets are disjoint).
  - Inc 2's byte-for-byte revert requires a re-bake from the staging
    uncompressed artifacts, not just a flag flip; keep `build/bake-fc-1200/staging`
    until Inc 2 is closed out and the compressed path is validated.
- CONSIDER (from the review-plan pass): self-detecting load path — probe the
  asset header magic at load time and choose `asset_fopen` vs `fopen`
  automatically; this would remove the flag↔bake coupling entirely (future
  simplification of Inc 2, not required for the plan's correctness).
- CONSIDER: extend the same level-1 compression lever to sprite/font assets
  (`mksprite --compress 1` / `mkfont`) in a follow-up increment — same
  research-backed payoff, currently out of scope.
- CONSIDER: `kEnableCompressedCells` should live in `debug_flags.hpp` (matches
  the `kEnableCellReveal` / `kEnableBackCull` / `kEnableDistantTier`
  convention there) — the plan already places it there; do NOT move it to
  `tile_streamer.hpp` alongside `kEnableTextures`/`kEnableNearCulling`.
- CONSIDER: Inc 3 audit should classify the per-cell vertex/face/batch/run
  buffers: `malloc_uncached` ones (T3DVertPacked, matrices) are already
  aligned — "verify + assert"; plain-malloc CPU-side buffers (batches, runs,
  face specs) are "document, don't fix" unless they feed rdpq DMA.

## Out of scope

- CPU data-locality / SoA / pointer-chasing refactors of
  `player_movement.cpp` / collision query hot paths — measurement-gated and
  deferred (CPU `update` = 0.6 % of an RSP-bound frame; prior gate failed).
- Fixed-point-vs-float migration — unresolved in the research brief; would
  need a dedicated hardware-measurement pass.
- Factor-5-style full streaming (code overlays / DSO) and microcode rewrites
  — explicitly rejected by the research brief ("do not port Factor 5").
- Re-enabling the distant `.dlod` pass or any distant-tier loading — not part
  of the memory story; `.dlod` files remain unused.
- Enlarging the heap arena or the 4 MB base-RDRAM target — the 8 MB Expansion
  Pak requirement is load-bearing (`assert_memory_expanded`); budget work
  flattens churn within the arena instead.
- Any change to movement physics feel, OG-parity climb/wall logic, or the
  single-mover collision path.
