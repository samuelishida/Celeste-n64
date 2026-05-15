# Acceptance scenarios

## Scenario 1: Bootable vertical slice

GIVEN a freshly built ROM on the target emulator and stock 4 MB N64 hardware  
WHEN the ROM boots into the spike scene  
THEN it displays the scene, accepts controller input, reports frame time, and keeps running for 10 minutes without a crash.

## Scenario 2: Movement parity

GIVEN the parity test map and reference captures from the PC build  
WHEN the player performs jump, dash, wall climb, skid, feather, respawn, and camera cases  
THEN the reviewed trajectories, timings, and state transitions stay within the accepted parity tolerances.

## Scenario 3: Graybox content load

GIVEN the first Forsaken City validation route represented with semantic placeholders  
WHEN the player enters each connected map segment  
THEN the runtime loads the correct geometry, actors, collision, text, and placeholder shapes without requiring final asset conversion.

## Scenario 4: Progress persistence

GIVEN a save with changed options, checkpoint, collected strawberries, deaths, and elapsed time  
WHEN the console loses power after a successful save and reboots  
THEN the latest valid slot restores the same progression and settings.

## Scenario 5: Full-route completion

GIVEN a clean save on the prototype ROM  
WHEN a player completes the full validation route including cassette and ending flow  
THEN every required actor, transition, cutscene, collectible, and menu path works without progression blockers.

## Scenario 6: Hardware acceptance

GIVEN the prototype ROM on emulator and stock 4 MB hardware  
WHEN the full validation route is played from title screen to ending  
THEN there are no blocker frame drops, save failures, missing progression, or unreadable placeholder scenes.

## Cross-cutting checks

- Rebuild graybox data twice from the same sources and compare outputs byte-for-byte.
- Run a power-loss test during save commit and confirm the older valid slot still boots.
- Compare the actor checklist against every entity class used by the validation route before declaring the prototype complete.
- Capture performance numbers before and after each major optimization so cuts are evidence-driven.
