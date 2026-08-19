# N64 Target Layer

Profiling and platform-specific utilities for the Madeline Cube ROM.

## Frame timing

`FrameProfiler` measures elapsed time per frame using `timer_ticks()`.
- Reports average frame time over 60-frame windows
- Logs via `debugf()` (visible in emulator debug output or USB logging)

## Memory headroom

`MemorySnapshot` captures heap stats via `mallinfo()`.
- Tracks total arena size, used bytes, and free bytes
- Reports largest contiguous free block (important for N64 DMA)

## 8 MB budget (Expansion Pak required)

Target hardware is the Nintendo 64 with the **Expansion Pak (8 MB RDRAM)**.
`rom_main.cpp` calls `assert_memory_expanded()` at boot, so the ROM fails
early with a clear error screen (not a crash) if the pak is absent.

Current ROM footprint:
- .text: ~261 KB
- .data: ~74 KB
- .bss: ~8 KB
- Heap: remainder of 8 MB after framebuffer and audio buffers (measured
  `[memory] total` ≈ 5.48 MB heap arena at boot)

The open-world renderer's streaming/memory budget (incremental near ring,
distant per-cell memory, global near material grouping) assumes the full 8 MB
heap. The 4 MB base-RDRAM target was dropped because the whole-map
interconnected renderer needs the extra headroom.

## Notes

- libdragon `preview` branch provides `timer_ticks()` and `TIMER_TICKS_PER_SEC`
- `mallinfo()` is from newlib and reports the main heap managed by libdragon
- Frame time is logged every 60 frames to avoid spamming the debug output
