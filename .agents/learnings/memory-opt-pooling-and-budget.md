# memory-opt plan: renderer pooling + budget (Inc 3-5 learnings)

## Context
Executed `.plans/memory-opt/plan.md` Inc 3-5 (DMA/alignment audit, renderer
pooling, memory budget) on the Celeste 64 demake. Inc 1-2 (telemetry buckets,
compressed cells) were done in prior sessions. All 5 increments DONE;
`make test` 56/56; final device capture `build/raw-baseline-final-20260907-123804.txt.log`
shows baseline-identical counters (near_batches=56, resident=13) with
`[memory] used=5,822,032` flat across every Inc 4/5 capture.

## Hardest decisions
1. **Pool footprint vs the plan's literal per-slot sizing.** The plan said
   "one `kSlotBufferBytes` block per slot" sized to the per-cell max. The
   worst cell's FULL parse peak (file buf 93 KB + faces + lvl_verts + specs +
   order + sorted_specs ≈ 237 KB) × 26 slots is ~6 MB — impossible on a
   2.9 MB heap. The working design: per-slot buffers hold only the
   PERSISTENT per-cell buffers (batches/runs/run_faces/material_groups,
   ≈ 56 KB/slot), and the TRANSIENT parse buffers share ONE pool-level
   scratch block (parses are serialized — the pump parses one per frame,
   SetCenter loads synchronously one at a time). Total prealloc ≈ 2.9 MB.
2. **verts_/matrix_fp_ stay malloc_uncached (deviation).** The plan listed
   them for pooling, but they are DMA-direct and the reveal matrix is
   REWRITTEN EVERY FRAME (SetCameraPosition → BuildRevealMatrix). Pooling
   them into cached RAM would need a per-frame `data_cache_hit_writeback` —
   a correctness hazard with zero [memory] benefit (malloc_uncached is
   memalign on the SAME arena, but the reveal writes would still need the
   flush). Documented deviation; the measurable done criteria (flat used,
   no malloc in load paths) still hold.

## Pitfalls hit
- **Bump allocators MUST 16-align every allocation.** The first Inc 4 device
  run crashed with a MIPS misaligned-write exception: the file buf landed at
  offset 11467 (odd), so the next struct (LvlFace) was at a non-4-aligned
  address. Round the cursor to the alignment boundary on EVERY bump.
- **`malloc_uncached` is memalign on the SAME newlib arena** — not a
  separate pool. Uncached allocations DO consume the mallinfo arena (the
  "uncached pool" is just the address mapping).
- **The edit tool is atomic per call.** A multi-block edit that fails on one
  block applies NONE of the blocks. After a failed edit + retry, verify ALL
  blocks landed (the `AttachSlotBuffer` definition was silently lost this
  way — link error caught it).
- **Ares "early exits" during captures can be the USER closing the window.**
  Three captures "crashed" before the user said "ares is working it was me
  closing it". Ask before debugging a phantom emulator crash.
- **The alignment audit contract must be re-run after pooling.** The Inc 3
  handoff said the inventory moves to the pool homes; the textured renderer's
  10 plain mallocs became 0 (all pool/scratch) — the contract's per-file
  count assertions caught the drift.

## Reuse
- Any bump-allocator work: 16-align every allocation (MIPS misaligned-write
  is a silent-ish crash — the exception handler prints, but the fix is
  structural).
- Any pooling work: per-slot for persistent + shared scratch for transient
  (serialized) is the footprint-safe shape; DMA-direct buffers that are
  rewritten per frame stay uncached.
- Any budget work: `[budget] WARN` lines are debug-only by user decision
  (Q3) — never a release hard-fail; caps live in rom_main and are recorded
  in docs/memory_budget.md with capture citations.
- The `[memory]` bucket registry (MemorySnapshot::RegisterBucket) is the
  pattern for adding report components — capture-free function pointers
  reading a file-scope scene handle.
