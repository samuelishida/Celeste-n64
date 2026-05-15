# Modern N64 examples on this stack

Checked on 2026-05-15.

## Same lower-level stack

- **Driving Strikers 64** is a released N64 homebrew port built with `libdragon`, `tiny3d`, and a custom C codebase.

## Same higher-level stack direction

- **Cathode Quest 64** is the showcase game associated with `Pyrite64`, which itself is built on `libdragon` and `tiny3d`.

## What that means for this project

- `libdragon + tiny3d` is already a credible production path for a real ROM.
- `Pyrite64` remains the right editor/runtime layer once the project needs authored scenes, imported assets, and more game-like content flow.
- The current `madeline_cube_rom.z64` intentionally starts one layer lower so the first playable movement test exists before editor integration work begins.

## References

- https://github.com/DragonMinded/libdragon
- https://github.com/HailToDodongo/tiny3d
- https://github.com/HailToDodongo/pyrite64
- https://nintendo64.pl/index.php/03-n64-news-styczen-czerwiec-2025/
- https://github.com/command-tab/awesome-n64-development

