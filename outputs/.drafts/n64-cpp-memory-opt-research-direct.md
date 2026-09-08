# Research notes (direct mode) — N64 optimization & C++ memory optimizations

## Method note
- Planned: 3 researcher subagents (T1 hardware / T2 C++ / T3 practice) — **BLOCKED**:
  `Subagents require tmux` (session not under tmux). Switched to lead-owned direct research.
- `web_search`: **BLOCKED** (missing Google Custom Search credentials). The three
  intended angle queries (hardware, C++ allocators, libdragon practice) could not be
  executed. Recovery: direct `web_fetch` of canonical, known source URLs.

## Fetch log (verified reachable)
1. `https://en.wikipedia.org/wiki/Nintendo_64` — fetched OK.
   Key facts: 4 MB RDRAM / 8 MB with Expansion Pak; VR4300 93.75 MHz 125 MIPS;
   RCP 62.5 MHz; 32-bit system bus; 4096-byte on-chip texture memory limit;
   cartridge range 4–64 MB; first home console with trilinear filtering; most
   games 320×240, higher resolutions often requiring Expansion Pak; Factor 5
   quote "We use the cartridge almost like normal RAM and are streaming all
   level data, textures, animations, music, sound and even program code";
   Copetti ref; N64 Programming Manual ref; 2000 IGN "Bringing Indy to N64" ref.
2. `https://www.copetti.org/writings/consoles/nintendo-64/` — fetched OK.
   VR4300: 32/64-bit modes, 32 GPRs, MIPS III, 32-bit external data bus, 32-bit
   address bus, 5-stage pipeline, 24 KB L1 (16 KB I + 8 KB D), FPU; UMA; no CPU
   DMA — RCP arbitrates; 4.5 MB physical RAM via 9-bit bus → 4 MB usable;
   RDRAM ~640 ns latency, 250 MHz, ~500 MB/s sequential; Expansion Pak +4.5 MB;
   KSEG0 typical for games; TLB mirrors; RSP 4 KB instr + 4 KB data for
   microcode; RDP TMEM 4 KB, ≤8 tiles, bilinear (triangular), mipmap halves
   TMEM, trilinear, perspective correction; 4 RDP modes; z-buffer (9th bit
   metadata); VI up to 32-bit color, 640×480/320×240; most games 320×240;
   Star Wars Episode I Racer 640×480 interlaced via Expansion Pak; pipeline
   stalls are programmer's job; audio 16-24 ADPCM ch / ~100 PCM; sequencer/MIDI
   as memory workaround; boot IPL1/2/3; cartridge ~5 MB/s average (max 64 MB);
   development in C + assembly; 32-bit instructions faster in practice; SM64
   used Gouraud/vertex colors because 4 KB TMEM insufficient.
3. `https://libdragon.dev/` — fetched OK.
   GCC 16 + Newlib; 64-bit-capable toolchain; rdpq sprites arbitrary sizes,
   transparent TMEM handling, all RDP pixel formats incl. palettized; mksprite
   with quantizer; "transparent compression of graphics"; audio streaming from
   ROM for low memory; compression: LZ4/Aplib/Shrinkler with MIPS-asm
   decompressors running in parallel with DMA, streaming decompression via
   fopen; DSO (dlopen/dlsym overlays); rom:// and sd:// fss; Ares + homebrew
   mode; MPEG1 320×240 800 Kbit/s 20 fps; preview branch includes OpenGL 1.1
   port and Tiny3D; threads with stack overflow detection.
4. `https://dragonminded.github.io/libdragon/ref/group__n64sys.html` — fetched OK.
   Manual cache management (SW must sync cache/RDRAM for DMA); cache lines are
   16 bytes; invalidate requires 16-byte alignment + multiple of 16 length;
   data_cache_* writeback/invalidate ops; inst cache invalidate ops;
   sys_get_heap_stats → heap_stats_t{total,used}; is_memory_expanded /
   assert_memory_expanded / get_memory_size; malloc_uncached / _aligned /
   free_uncached (0xA0000000 segment, no cacheline sharing with other heap
   buffers); KSEG0_START_ADDR 0x80000000; HEAP_START_ADDR = __bss_end;
   MEMORY_BARRIER "Since GCC seems to reorder volatile at -O2 ... library is
   useless at higher optimization levels" without it; CPU 93.75 MHz, RCP
   62.5 MHz.
5. `https://dragonminded.github.io/libdragon/ref/group__dma.html` — fetched OK.
   Raw DMA well-defined only for RAM addr ≡ 0 mod 8, ROM addr ≡ 0 mod 2, len ≡
   0 mod 2; dma_read_async wrapper handles arbitrary alignment (RAM/PI same
   parity constraint); code must never raw-access cartridge domain (collision
   with in-flight DMA / caching); io_read/io_write for single 32-bit; full
   32-bit PI address space; ROM range 0x1000_0000–0x1FBF_FFFF; DMA needs no
   init.
6. `https://dragonminded.github.io/libdragon/ref/topics.html` — fetched OK.
   API map: DMA, n64sys, newlib hooks, DragonFS, asset subsystem, rdpq, fast
   math, backtrace etc.
7. `https://gameprogrammingpatterns.com/data-locality.html` — fetched OK.
   Cache line 64–128 bytes; miss ≈ few-hundred-cycle stall; author measured
   50× slowdown worst vs best case; contiguous arrays vs pointer chasing;
   packed data; sort-by-active; hot/cold splitting; avoid virtual dispatch in
   hot paths / separate arrays per type; references Albrecht GCAP "Pitfalls of
   OOP" and Llopis "Data-Oriented Design".
8. `https://gameprogrammingpatterns.com/object-pool.html` — fetched OK.
   Fragmentation "deadly" on consoles; pool allocates big block upfront; free
   list cannibalizing dead-object memory (union); fixed slot size wastes memory
   → split pools by size; reused objects must be fully re-initialized;
   overflow strategies (prevent / skip / kill quietest / overflow pool).
9. `https://github.com/DragonMinded/libdragon/wiki/Compression` — fetched OK.
   Level 1 = LZ4: decompress+load faster than uncompressed (1 MiB corpus:
   156 ms / 6577 KiB/s vs uncompressed 220 ms / 4652 KiB/s; 35.5% ratio);
   Level 2 = Aplib (237 ms, 4318 KiB/s, better than gzip -9); Level 3 =
   Shrinkler (1133 ms, 904 KiB/s, near xz); game code compression boots faster
   (Brew Volley: 112 ms vs 139 ms uncompressed); DSOs compressed by default;
   asset_fopen streaming decompress with 4 KiB window; mkasset window option;
   asset_load() → malloc'd buffer; init only compressions actually used (link
   size).
10. `https://web.archive.org/web/20130927083804/http://www.ign.com/articles/2000/11/10/bringing-indy-to-n64`
    — fetched OK. Factor 5 on Indiana Jones N64 (2000): cartridge used "almost
    like normal RAM" — streaming level data, textures, animations, music,
    sound, and program code while running; background streaming of needed level
    parts; "every trick possible in terms of optimal use of the 4K texture
    cache for every single texture" + custom tool choosing per-texture
    formats; dropped Z-buffer in hi-res to save fillrate (Z-buffer "uses quite
    a bit of the N64's fillrate"); rewrote ALL microcode from scratch for
    real-time lights + polygon count; 32 MB cartridge + EEPROM; 19 months,
    core team of 8.

## Blocked / rejected
- `https://n64brew.dev/wiki/Memory_map` — HTTP 403 (blocked; JS/anti-bot).
  n64brew corroboration of RDRAM/memory-map numbers unavailable.
- `https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro03/index.html`
  — "Could not extract readable content" (blocked). Cited only via Wikipedia's
  reference list; full text not read.
- `https://bitsquid.blogspot.com/2009/11/allocation-adventures-1.html` — 404/dead
  (rejected). No allocator-adventure series corroboration from Bitsquid.
- PDFs not fetched per workflow rule (e.g., Albrecht "Pitfalls of OOP" PDF
  linked from GPP): cited from the GPP references list only; full-text parsing
  BLOCKED.
- web_search API — blocked (credentials). All Google querying unavailable.

## Repo-local grounding (from AGENTS.md, this repository — not web claims)
- Target: N64 + Expansion Pak (8 MB); rom_main.cpp calls assert_memory_expanded().
- [memory] telemetry: stationary mallinfo arena total ≈ 2.16 MB, used ≈ 2.15 MB;
  walking total ≈ 2.29 MB, used ≈ 2.23 MB (ratio ≈ 0.99).
- 6 hot TUs built at -O2 (tile_streamer, textured_room_renderer, lvl_room_renderer,
  open_world_renderer, camera_controller, player_movement); global -Os.
- Whole-map (Forsaken City 1.map) baked at --chunk-size 1200 → 45 cells; one
  global CMSH collision mesh; TileStreamer forward-prioritized 3×3 ring;
  single near pass (near=5, far=800); UV world-scale texel units with 1 texel =
  32 units (s10.5), REPEAT wrap.
- Repo files: .pak packed filesystem (madeline_cube_rom.pak etc.), .z64 out.
- Libdragon `preview` + tiny3d.