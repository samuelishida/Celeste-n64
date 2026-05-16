# Decisions & assumptions

## D1: Replace modeled parity traces with actual C# captures

- **Context:** The current fixtures are generated from constants without running the original game, while the ROM is now failing in behavior those fixtures did not expose.
- **Decision:** Captured C# runs become the only authority for parity acceptance; modeled traces are temporary scaffolding only.
- **Consequences:** Early work shifts toward tooling, but later tuning has a real target and can reject false positives.
- **Alternatives rejected:** Continue extending modeled traces. It is cheaper short-term, but it already failed to catch movement substrate defects.

## D2: Rebuild around one authoritative player motor

- **Context:** The current branch spreads motion across controller math, sweep logic, late repairs, and older resolver paths, making ownership ambiguous.
- **Decision:** State code requests motion; the motor alone writes resolved post-move position/contact results.
- **Consequences:** Some current recovery branches will be deleted even if they appear useful in isolation. Tests must pin phase outputs before controller tuning resumes.
- **Alternatives rejected:** Keep patching the existing split ownership. That preserves work already done, but it is the architecture that produced the present failure mode.

## D3: Mirror source-shaped queries before higher-level feel work

- **Context:** The original player depends on raycasts, wall hits, pushout vectors, normals, and owner identity, while the port currently relies mostly on planes and boxes.
- **Decision:** Rebuild the query layer to answer the same categories of questions as the source before re-porting climb, ledges, and platform carry.
- **Consequences:** The world layer becomes richer, but higher-level player code stops inventing local approximations.
- **Alternatives rejected:** Special-case each player feature in controller code. That would be faster once, then brittle forever.

## D4: Treat ROM observability as part of the feature

- **Context:** Host startup simulation passed while the actual ROM still fell through the world and crashed.
- **Decision:** Each major physics increment carries low-cost ROM diagnostics and hardware acceptance checks, not only host assertions.
- **Consequences:** Some debug surfaces stay in the branch longer, but hardware-only failures become diagnosable before the next tuning pass.
- **Alternatives rejected:** Rely on emulator/host equivalence until the end. The present bug already disproved that assumption.

## D5: Keep the 4 MB target as a hard design constraint

- **Context:** The user has already chosen stock 4 MB hardware for the prototype.
- **Decision:** Runtime structures remain fixed-capacity after load, query hot paths avoid allocation, and every richer system ships with budget instrumentation.
- **Consequences:** Some generality is deferred, but the prototype remains honest about its target machine.
- **Alternatives rejected:** Build a more dynamic desktop-style physics representation first and optimize later. That risks proving out behavior that cannot ship on target hardware.

## Assumptions resolved from code

- The source update order is timers/input, state update, sweep/popout movement, then late ground resolution and snap. Source: code @ `Celeste64-og/Source/Actors/Player.cs:320-430`.
- Ground, ceiling, nearest-wall, and closest-to-normal wall checks are distinct source queries, not interchangeable convenience helpers. Source: code @ `Celeste64-og/Source/Actors/Player.cs:2280-2305`, `Celeste64-og/Source/Scenes/World.cs:412-599`.
- The current parity corpus is source-derived and does not execute the C# game. Source: code @ `tools/reference_capture/README.md`, `tools/reference_capture/reference_capture.py`.
- The current motor already contains ad hoc sanitization and below-floor recovery logic, which is evidence that the architecture is compensating for deeper mismatches. Source: code @ `src/user/gameplay/player_motor.cpp`.
- Stock 4 MB hardware is the target. Source: user-confirmed in prior review.
