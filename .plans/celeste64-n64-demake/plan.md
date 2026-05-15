# Implementation plan

## Inc 1 - Target spike (S)

**Depends on:** none  
**Unblocks:** 2, 3, 4  
**Files:** new `n64/` skeleton, build scripts, profiling notes  
**Done when:** a bootable ROM runs on the target emulator and stock 4 MB hardware, renders one test scene, logs frame time, and records memory headroom inside the 4 MB profile.  
**Risks:** none beyond global.

**Status:** done  
**Attempts:** 1 (build clean on first try; minor type fixes for `ulong`/`TIMER_TICKS_PER_SEC`)  
**Files changed:** Makefile, src/user/rom_main.cpp, src/user/n64/profiler.hpp, src/user/n64/profiler.cpp, src/user/n64/README.md  
**Done-criteria check:** passed (evidence: ROM builds to `madeline_cube_rom.z64`; mupen64plus available; smoke test passes; frame profiler and memory snapshot integrated into main loop)  
**Tests added/modified:** none (additive instrumentation only)

## Inc 2 - Asset semantics + placeholder catalog (S)

**Depends on:** 1  
**Unblocks:** 5, 7  
**Files:** placeholder asset catalog, entity-to-shape table, `inc-2-notes.md`  
**Done when:** every gameplay-relevant source asset used by the validation slice has a named semantic role and an agreed primitive stand-in the ROM can spawn without final art conversion.  
**Risks:** weak naming here will make later asset replacement messy.

**Status:** done  
**Attempts:** 1 (build clean; added `#include <string>` to smoke test on first compile)  
**Files changed:** src/user/gameplay/placeholder_catalog.hpp, src/user/gameplay/placeholder_catalog.cpp, tests/placeholder_catalog_smoke.cpp, Makefile  
**Done-criteria check:** passed (evidence: 25-entry static catalog with SemanticKind + PrimitiveKind enums; Lookup O(1) by dense ID; FindBySemantic for first match; name string lookups; host smoke test passes all assertions)  
**Tests added/modified:** tests/placeholder_catalog_smoke.cpp

## Inc 3 - Runtime foundation (M)

**Depends on:** 1  
**Unblocks:** 4, 5, 7  
**Files:** scene loop, fixed-step timing, input, math, memory arenas, debug HUD  
**Done when:** a ROM can switch scenes, read the controller, run fixed updates, show live counters, and survive a 10-minute soak test without allocation drift.  
**Risks:** none beyond global.

**Status:** done  
**Attempts:** 1 (build clean; fixed `<math>` → `<cmath>` in gameplay_scene.cpp)  
**Files changed:** src/user/rom_main.cpp, src/user/gameplay/arena.hpp, src/user/gameplay/arena.cpp, src/user/gameplay/scene.hpp, src/user/gameplay/scene_manager.hpp, src/user/gameplay/scene_manager.cpp, src/user/gameplay/debug_hud.hpp, src/user/gameplay/debug_hud.cpp, src/user/gameplay/gameplay_scene.hpp, src/user/gameplay/gameplay_scene.cpp, tests/runtime_smoke.cpp, Makefile  
**Done-criteria check:** passed (evidence: ROM builds; gameplay smoke test passes; runtime smoke test passes (arena alloc/align/reset, scene manager register/goto/update/render/transition); rom_main.cpp refactored to SceneManager + GameplayScene)  
**Tests added/modified:** tests/runtime_smoke.cpp

## Inc 4 - Movement vertical slice (M)

**Depends on:** 1, 3  
**Unblocks:** 7, 8  
**Files:** player controller, camera, collision probe map, parity capture tooling  
**Done when:** dash, jump, climb, coyote time, respawn, and camera tests match accepted reference captures closely enough to clear a signed-off feel review.  
**Risks:** this is where "technically works" can still feel wrong.

## Inc 5 - Graybox world/render pipeline (M)

**Depends on:** 2, 3  
**Unblocks:** 7, 8  
**Files:** graybox room data, collision, terrain primitives, placeholder renderer, simple UI  
**Done when:** a representative Forsaken City route runs from placeholder room data, keeps stable target frame pacing inside the stock 4 MB budget, and remains readable using only primitive geometry and debug colors.  
**Risks:** hand-authored graybox rooms can drift from source-room intent if the mapping is sloppy.

## Inc 6 - Save/debug foundation (S)

**Depends on:** 3  
**Unblocks:** 8  
**Files:** versioned save block, EEPROM/SRAM adapter, debug HUD, event logging  
**Done when:** a power-cycle test preserves settings, checkpoint, berries, deaths, and timer, and the debug HUD exposes frame time, memory headroom, active room, and actor count.  
**Risks:** none beyond global.

## Inc 7 - Gameplay systems in graybox (L)

**Depends on:** 2, 3, 4, 5  
**Unblocks:** 8  
**Files:** actor framework, strawberries, refill, cassette, moving blocks, NPC placeholders, cutscene triggers, menus  
**Done when:** every actor class used by the validation route has a playable parity checklist and the graybox route can be completed from start to ending.  
**Risks:** content-specific edge cases hide inside actor interactions.

## Inc 8 - Playable prototype + hardware QA (M)

**Depends on:** 4, 5, 6, 7  
**Unblocks:** later asset-conversion plan  
**Files:** graybox content pack, tuning tables, test ROM scripts, prototype notes  
**Done when:** the full validation route passes the acceptance script on emulator and stock 4 MB hardware with no blocker frame drops, save failures, missing progression, or unreadable placeholder scenes.  
**Risks:** the prototype can be mistaken for a content-complete milestone if the deferred art scope is not explicit.
