# ROM build marker freshness

## Context
Manual Ares logs showed an old `[build] compiled` timestamp after gameplay
objects changed and Make relinked the ROM.

## Hardest decision
Refresh only `rom_main.o`, the translation unit embedding `__DATE__/__TIME__`,
on every ROM build. This keeps identity current without forcing all gameplay
objects to rebuild.

## Alternatives rejected
- Trust relink time to update the marker — it cannot change constants embedded
  in an unchanged object.
- Force the full ROM build — unnecessary compile cost; normal object dependency
  timestamps already cover code freshness.

## Least confident
Build time is a quick human-visible freshness hint, not artifact identity.
Captured SHA-256 remains authoritative, especially when ROMs are copied or
renamed outside the build.

## Reuse
Keep the `refresh_rom_build_marker` prerequisite in `Makefile` for normal and
isolated ROM variants; verify with `make -n` and the rebuilt `rom_main.o`.
