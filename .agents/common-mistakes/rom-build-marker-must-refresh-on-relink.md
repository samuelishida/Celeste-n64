# ROM build marker must refresh on relink

## Symptom
Manual Ares logs show an old `[build] compiled=...` value after gameplay
sources changed and the ROM was relinked, making an old loaded ROM hard to
distinguish from a new one.

## Cause
The marker uses `__DATE__`/`__TIME__` from `rom_main.cpp`, but Make only
recompiled that translation unit when its own source changed. A later relink
from updated gameplay objects retained the stale marker.

## Fix
Keep `refresh_rom_build_marker` as a phony prerequisite of
`$(BUILD_DIR)/src/user/rom_main.o`. This refreshes the boot marker for normal,
autoplay, and input-trace ROM builds. Captured ROM SHA-256 remains the exact
artifact identity; build time is a quick human-visible check.
