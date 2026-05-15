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

## 4 MB budget

Target hardware is stock Nintendo 64 (4 MB RAM).
Current ROM footprint:
- .text: ~261 KB
- .data: ~74 KB
- .bss: ~8 KB
- Heap: remainder of 4 MB after framebuffer and audio buffers

## Notes

- libdragon `preview` branch provides `timer_ticks()` and `TIMER_TICKS_PER_SEC`
- `mallinfo()` is from newlib and reports the main heap managed by libdragon
- Frame time is logged every 60 frames to avoid spamming the debug output
