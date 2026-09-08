# N64 Optimization & C++ Memory Optimization — Research Draft

Slug: `n64-cpp-memory-opt` · direct-search mode (researcher subagents blocked)
[SRC#] markers are resolved to URLs in the cited version; legend at the end of
this draft.

---

## Executive summary

The Nintendo 64 is a memory-tight, cache-odd machine: 4 MB of general-use
RDRAM base (8 MB with the Expansion Pak), a 4 KB texture cache (TMEM) that
shapes what art is even possible, 16-byte data-cache lines that make DMA
buffer alignment a correctness issue, and a cartridge bus that is slow enough
that *streaming + compression* beat "load everything into RAM" even with
8 MB [SRC1][SRC2][SRC4].

For C++ game code, the community's established answer is a small set of
techniques: fixed-budget allocation (pools, arenas, free lists) instead of
heap churn, data-oriented/SoA layout to keep the 8 KB data cache hot, hot/cold
splitting, and 16-byte-aligned DMA buffers [SRC7][SRC8][SRC4].

Libdragon — the SDK this repo is built on — exposes exactly the levers an
N64-scale engine needs: a single newlib heap whose live arena the game can
query (`sys_get_heap_stats` → `total`/`used` — same shape as this repo's
`[memory]` telemetry), uncached-memory allocations, cache writeback/invalidate
primitives, and a three-level asset-compression system whose level-1 (LZ4)
path is *faster than loading uncompressed data* from the cartridge
[SRC4][SRC5][SRC9].

Commercial precedent (Factor 5) went further than any homebrew budget allows —
full cartridge-as-RAM streaming of data *and code*, per-texture TMEM format
optimization tools, microcode rewrites, and dropping the Z-buffer at high
resolution to reclaim fillrate [SRC3 via SRC1][SRC10].

For this repo specifically (8 MB target, ~2.16–2.29 MB live arena, streamed
45-cell map, six `-O2` hot TUs), the highest-leverage, lowest-risk moves are:
aligning DMA/streaming buffers to 16 bytes, moving per-frame temporaries off
the heap into a frame arena, SoA/packed layouts for the player/collision hot
paths, and default level-1 compression for streamed cell data instead of
uncompressed reads [repo observation][SRC9][SRC4].

---

## Findings

### 1. The hardware this code must respect

**Memory.** The N64 physically carries 4.5 MB of RDRAM wired through a 9-bit
bus, where the 9th bit is reserved for framebuffer metadata (Z-buffering,
anti-aliasing); every component except the GPU therefore sees 4 MB. The
Expansion Pak adds another 4.5 MB, standardizing on 8 MB; the Jumper Pak is
just a terminator. RDRAM access latency is severe — around 640 ns — offset by
a 250 MHz bank clock and ~500 MB/s only when reading *consecutive* data;
random access is the enemy [SRC2][SRC1].

**CPU.** The NEC VR4300 is a 93.75 MHz MIPS III R4300i derivative (~125 MIPS
per NEC) with 32 general-purpose registers, a 32-bit *external* data bus (64-bit
internally), and 24 KB of L1 cache split 16 KB instruction / 8 KB data. In
practice games ran 32-bit code: 32-bit instructions execute faster because of
the narrow external bus. The CPU has **no direct DMA access** — the RCP is the
memory controller, and the CPU sits behind the graphics chip's arbitration.
There is no virtual memory in use for games; the standard segment is KSEG0
(cached, unmapped), and TLB tricks are available but rarely needed
[SRC2][SRC1][SRC1: N64 Programming Manual, cited via Wikipedia].

**Texture memory.** The RDP textures from 4 KB of TMEM (up to 8 tiles).
Mipmapping cuts available TMEM in half. This single fact explains the N64's
low-res-texture look: Super Mario 64 leaned on flat shading / Gouraud vertex
colors precisely because 4 KB would not hold high-res textures, and Factor 5's
later games succeeded by engineering *per-texture* format choices against the
same 4 KB. The libdragon rdpq layer handles TMEM packing transparently for
sprites [SRC2][SRC1][SRC10][SRC3].

**Caches & DMA coherency.** The VR4300 uses *manual* cache management: any
buffer handed to a DMA engine must be writeback'd (cached → RDRAM) or
invalidated (RDRAM → cached) by software. The data cache is built of
**16-byte cache lines**, so invalidation calls must operate on 16-byte-aligned,
16-multiple ranges or they can discard live neighboring data (libdragon asserts
on misuse) [SRC4].

**Cartridge bandwidth.** Cartridge reads average ~5 MB/s over a 16-bit
Parallel Interface — far slower than RDRAM. This makes the classic move: keep
compressed data in ROM, decompress into RDRAM, and stream rather than load
whole levels [SRC2][SRC1][SRC10].

**Display.** VI supports up to 32-bit color and 640×480 (interlaced) / 320×240;
most games shipped 320×240, and higher resolutions typically required the
Expansion Pak (e.g., Star Wars Episode I Racer at 640×480 interlaced). The
Z-buffer is real (hardware) but costs fillrate — Factor 5 dropped it at
high-res to hit their framerate target [SRC2][SRC1][SRC10].

### 2. C++ memory optimization techniques that apply

These are the techniques the broader game/C++ community converges on for
console-class memory (small RAM, small caches, no OS heap guarantees)
[SRC7][SRC8]:

- **Pools & free lists.** Allocate one big fixed block at boot; reuse slots
  with an in-place free list that cannibalizes dead objects' memory. The
  rationale is twofold: `malloc`/`new` churn fragments the heap ("memory
  fragmentation is deadly" on consoles) and costs time per call. Pitfalls:
  fixed slot size wastes memory for heterogeneous objects (split pools by
  size), reused objects must be *fully* re-initialized (no allocator magic
  value to catch stale state), and pool sizing must be tuned per game state
  [SRC8].
- **Data locality / SoA.** Cache lines load adjacent data; pointer-chasing
  through entities → components is the classic thrash. Packed contiguous
  arrays of homogenous records, processed in the order stored, avoid the
  pointer chase. GPP's author measured a worst-case/best-case gap of ~50× in
  toy programs from cache misses alone. Hot/cold splitting (move rarely-touched
  fields to a side structure) shrinks the hot record. Polymorphism (virtual
  calls, mixed-type arrays) is the enemy in hot loops — prefer per-type arrays
  and static dispatch [SRC7].
- **Alignment is correctness on N64, not just speed.** PI DMA requires RAM
  buffers 8-byte aligned, ROM addresses 2-byte aligned, lengths even; cache
  invalidation needs 16-byte alignment. `malloc_uncached`/`_aligned` exist
  specifically so buffers don't share cache lines with other heap data
  [SRC5][SRC4].
- **Optimization flags & language modes.** This repo already reflects the
  console consensus: `-Os` globally, `-O2` on hot TUs, and (implicitly) no
  exceptions/RTTI dependence typical of embedded C++. libdragon's own docs
  warn that at `-O2` GCC reorders volatile accesses, which is why its DMA
  paths use memory barriers; any optimized engine code touching hardware
  registers needs the same discipline [repo observation][SRC4].
- **Fixed-point / 32-bit math.** The VR4300's narrow external bus made 32-bit
  the practical choice on the original console; libdragon's toolchain is
  64-bit-capable but the ABI used by games is 32-bit. Float vs fixed-point on
  this MIPS is not definitively documented in the sources gathered here — it
  is marked as an open question rather than a recommendation [SRC2][SRC3].

### 3. libdragon: the memory machinery actually available

- **One heap, queryable.** The default environment is newlib `malloc` over a
  heap that starts at `__bss_end`; `sys_get_heap_stats()` returns
  `{total, used}` — the exact shape of this repo's `[memory]` telemetry (a
  stationary arena ≈ 2.16 MB total / 2.15 MB used, walking ≈ 2.29/2.23 MB on
  this project) [SRC4][repo observation].
- **Expansion Pak is a first-class gate.** `is_memory_expanded()`,
  `assert_memory_expanded()` (error screen + halt), `get_memory_size()` — the
  repo's `assert_memory_expanded()` at boot is the documented pattern [SRC4].
- **Uncached allocation.** `malloc_uncached` / `malloc_uncached_aligned` /
  `free_uncached` give buffers in the 0xA0000000 uncached segment, guaranteed
  not to share cache lines with other heap buffers — the right tool for
  buffers DMA'd to/from by the RCP [SRC4].
- **Manual cache ops.** `data_cache_hit_writeback`, `data_cache_hit_invalidate`,
  `data_cache_hit_writeback_invalidate`, whole-cache variants, plus
  instruction-cache invalidate for self-modifying/overlay code [SRC4].
- **DMA.** `dma_read*`/`dma_write*` over the PI; raw transfer rules are RAM
  addr ≡ 0 mod 8, ROM addr ≡ 0 mod 2, len ≡ 0 mod 2; `dma_read_async` relaxes
  this for arbitrary alignment (RAM/PI same-parity). Never poke the cartridge
  domain directly with CPU loads/stores — it can collide with in-flight DMA or
  hit caching issues; use `io_read`/`io_write` [SRC5].
- **Compression as a memory strategy.** libdragon ships three levels — LZ4
  (default), Aplib, Shrinkler — with MIPS-assembly decompressors that run in
  parallel with DMA. The measured payoff on a 1 MiB asset corpus: level 1
  compresses to 35.5% and *loads faster than uncompressed* (156 ms / 6577 KiB/s
  vs 220 ms / 4652 KiB/s); level 2 (Aplib) beats gzip-9 at 237 ms; level 3
  (Shrinkler) approaches xz ratios at 1133 ms (rarely-loaded assets only).
  Even game code is compressed by default so the ROM *boots faster* than
  uncompressed (Brew Volley: 112 ms vs 139 ms). Streaming decompression is
  available via `asset_fopen` with a 4 KiB window; DSO overlays load code at
  runtime via `dlopen`-style APIs [SRC9][SRC3].
- **2D path.** rdpq handles TMEM management ("transparently and efficiently")
  for arbitrary-size sprites in all pixel formats including palettized ones;
  `mksprite` includes a color quantizer and transparent compression [SRC3].

### 4. Commercial precedent: what shipped games actually did

- **Factor 5 (Indiana Jones and the Infernal Machine, Rogue Squadron etc.,**
  **via IGN interviews):** treated the cartridge "almost like normal RAM,"
  streaming level data, textures, animations, music, *sound, and even program
  code* while the game runs, in the background, so that level/texture size is
  bounded by the cartridge (their cart was 32 MB), not by RAM. For textures
  they "used every trick possible in terms of optimal use of the 4K texture
  cache," writing a tool that chose the best (often unusual) texture format
  per source texture. At high resolution they dropped the Z-buffer because it
  cost too much fillrate. They also rewrote Nintendo/SGI's microcode from
  scratch to gain near-unlimited real-time lights and more polygons
  [SRC10][SRC1].
- **Super Mario 64 (per Copetti's analysis):** flat-shaded/Gouraud look instead
  of richly textured because 4 KB TMEM could not hold high-res textures —
  exactly the constraint the current repo works around with world-scale UVs
  and repeat-wrapped tiles [SRC2][repo observation].
- **General era practice (Wikipedia):** compressed textures, shorter music,
  fewer cutscenes, and downscaled ports were direct consequences of cartridge
  size limits; large-cart games (Resident Evil 2, Sin & Punishment, Conker)
  used 32/64 MB carts to afford more layered textures [SRC1].
- **Microcode as the performance ceiling:** custom microcode was how the
  most advanced N64 titles raised polygon counts, lighting, animation, and AI
  beyond stock pipelines (Conker's Bad Fur Day cited as the generation's
  peak) [SRC1].

### 5. Applicability to this repo (libdragon preview + tiny3d, 8 MB target)

These are recommendations synthesized by the lead from the sources above;
each is marked as inference where it goes beyond the cited facts.

- **Streaming + compression belong together.** The repo already streams a
  baked 45-cell map through a resident 3×3 ring. Given that libdragon's level-1
  compression loads *faster* than uncompressed reads, baking cell/pak payloads
  at level 1 (already the default) is the documented right choice; level 2 is
  the option for load-once-at-level-change data, level 3 for cold assets
  [SRC9][repo observation]. *(inference: applying the documented tradeoff
  table to this streaming design)*
- **Fix alignment at the transport boundary.** Streamed cell data crossing
  PI DMA (or rdpq upload DMA) should live in 16-byte-aligned, 16-multiple
  buffers; the repo's fixed-point/vertex packing (`s10.5`) is a data-format
  concern, alignment is separate [SRC4][SRC5]. *(inference)*
- **Move per-frame temporaries to a frame arena.** The gameplay/hot paths
  (player movement, tile streaming) should not `malloc` per tick; console
  wisdom says pool/arena everything that lives longer than a frame
  [SRC7][SRC8]. *(inference)*
- **Keep 2 MB-scale heap discipline.** The repo's live arena (~2.16–2.29 MB
  of 8 MB) proves the engine holds; growth should be *deliberate and
  boot-time-planned*, mirroring how N64 titles partitioned RAM
  [repo observation][SRC8]. *(inference)*
- **Cache-op discipline is a correctness rule, not a micro-opt.** Any RCP/DMA
  shared buffer needs explicit writeback/invalidate; `malloc_uncached` is the
  simpler escape hatch for DMA targets [SRC4]. *(inference)*
- **Do not port Factor 5.** Streaming *code* requires DSO/overlay machinery and
  microcode rewrites — far beyond the current milestone scope. The repo's
  single near-pass renderer with a fixed resident ring is already a sane
  middle ground between "load everything" and Factor-5-style full streaming
  [SRC10][repo observation]. *(inference)*

---

## Caveats & disagreements

- **Search API was unavailable.** All queries were blocked (missing Google CSE
  credentials); sourcing is therefore URL-driven from canonical pages rather
  than an exhaustive sweep. Coverage skews toward sources I could reach
  directly. n64brew (the community hardware wiki) returned HTTP 403, removing a
  second witness for RDRAM spec details; the 403 is anti-bot, not a
  content problem.
- **The 640 ns RDRAM latency and 500 MB/s figures are single-source** (Copetti,
  citing RDRAM analyses); Wikipedia does not repeat them. Treat them as
  strongly-worded secondary claims, not hardware-manual numbers.
- **Copetti says "most games ran 32-bit"; libdragon ships a 64-bit-capable
  toolchain.** These are not contradictory (modern SDK vs. '90s practice), but
  the draft does not resolve which is better for a modern homebrew C++ engine —
  it is listed as an open question, and the repo's `-Os`/`-O2` choices are
  reported, not blessed.
- **GPP's 50× figure is from the author's own toy benchmarks**, reported for
  intuition, not a portable N64 number.
- **Factor 5 is one studio with exceptional engineers**; its streaming-code /
  microcode-rewrite playbook is not a template for a small homebrew project
  (explicitly flagged in §5).
- **Fixed-point vs float on the VR4300 is unresolved in gathered sources** —
  intentionally not recommended either way.

## Open questions

1. Float vs fixed-point on the VR4300 for gameplay math — no authoritative
   source gathered; worth a dedicated hardware-measurement pass (Ares timing).
2. Exact n64brew memory-map numbers (expansion pak window, RDRAM register
   layout) — blocked at fetch time (403).
3. What fraction of `[memory] used` is texture/vertex pools vs. collision mesh
   vs. cells in this repo — the telemetry doesn't decompose the arena.
4. Whether libdragon's `preview`-branch threaded kernel changes heap/stack
   accounting for a future multithreaded tick.
5. GoldenEye/Rare-era explicit memory-layout postmortems (level streaming
   details) — not reachable in this pass; only Factor 5 gave concrete detail.

---

## Source legend (draft markers)

- [SRC1] https://en.wikipedia.org/wiki/Nintendo_64 (fetched)
- [SRC2] https://www.copetti.org/writings/consoles/nintendo-64/ (fetched)
- [SRC3] https://libdragon.dev/ (fetched)
- [SRC4] https://dragonminded.github.io/libdragon/ref/group__n64sys.html (fetched)
- [SRC5] https://dragonminded.github.io/libdragon/ref/group__dma.html (fetched)
- [SRC6] https://dragonminded.github.io/libdragon/ref/topics.html (fetched)
- [SRC7] https://gameprogrammingpatterns.com/data-locality.html (fetched)
- [SRC8] https://gameprogrammingpatterns.com/object-pool.html (fetched)
- [SRC9] https://github.com/DragonMinded/libdragon/wiki/Compression (fetched)
- [SRC10] https://web.archive.org/web/20130927083804/http://www.ign.com/articles/2000/11/10/bringing-indy-to-n64 (fetched)
- [repo observation] AGENTS.md in this repository (local ground truth, not a URL)
- Blocked, cited indirectly only: n64brew.dev (403), ultra64.ca online manual
  (extract failed; referenced via [SRC1]'s citation list).
