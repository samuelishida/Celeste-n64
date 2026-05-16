# Decisions & assumptions

## D1: Replicate semantics, not libraries

- **Context:** Celeste64 depends on FosterFramework 0.1.18-alpha, but the player relies on a narrow slice of behaviour: buffered virtual buttons, deadzoned sticks, time/math helpers, and no large engine feature from Foster.
- **Decision:** port the small behavioural surface we use; do not attempt a Foster clone.
- **Consequences:** less code, smaller ROM, easier testing. Future ports may add more semantics only when a real actor needs them.
- **Alternatives rejected:** importing or reimplementing the whole framework would widen the blast radius without making `Player.cs` meaningfully easier to translate.

## D2: Keep the motor as the single post-move authority

- **Context:** the current scene loop deliberately feeds requested velocity into `PlayerMotor` and lets the motor own resolved position/contact output.
- **Decision:** build compatibility layers around that boundary instead of restoring the OG monolithic `Player.Popout()` ownership model.
- **Consequences:** source code ports become slightly adapted rather than line-for-line, but collision remains testable and already-validated motor work survives.
- **Alternatives rejected:** moving resolution back into player logic would erase the cleanest piece of the current C++ architecture.

## D3: World/actor compatibility comes before source-faithful solids

- **Context:** OG solids are actors: they participate in deferred add/remove, typed queries, transformed bounds, rider propagation, and broadphase insertion.
- **Decision:** port the world/actor registry before replacing the current `Room` collider ownership model.
- **Consequences:** the solid migration has somewhere correct to live, and later actors can reuse the same registry.
- **Alternatives rejected:** bolting actor pointers onto the present `Room` arrays first would create a second temporary architecture we would have to delete.

## D4: Treat parity as traces, not eyeballing

- **Context:** the source player hides several feeling-critical clauses behind helper calls: jump buffering, climb snap-up, corner handling, ledge nudging, stored platform velocity, and spike-aware climb blocking.
- **Decision:** every increment adds or preserves observable fixtures before migration.
- **Consequences:** future ports can move faster because behaviour is pinned before code shifts.
- **Alternatives rejected:** "port until it feels right" is too lossy for a system with this many coupled micro-rules.

## D5: Buffered input owns jump/dash timing after migration

- **Context:** C++ already has `jump_buffer_remaining`; OG input already buffers presses.
- **Decision:** Inc 2 makes the input layer authoritative for buffered presses and removes duplicate timing from player state rather than stacking two buffers.
- **Consequences:** one source of truth for "was jump pressed recently" and no accidental longer window.
- **Alternatives rejected:** preserving both buffers would make parity drift invisible until feel-testing.

## D6: Promote actor/world collision queries; retire `Room` gradually

- **Context:** `PlayerMotor::Step` already consumes `Room`, but keeping `Room` as the long-term collision authority would force every OG actor port through a legacy fixed-array projection.
- **Decision:** actor/world queries become authoritative in Inc 5. `Room` remains a temporary motor-facing envelope for authored metadata and legacy fallback only; when it has an actor world attached, query functions read solids from actors, not `Room::colliders`.
- **Consequences:** the motor boundary can stay stable while ownership moves now, and later work can narrow `Room` to level metadata instead of teaching every new actor about old collider arrays.
- **Consequences:** the motor boundary remains stable while the world beneath it gets more source-shaped.
- **Alternatives rejected:** changing the motor API in the same increment would make failures hard to attribute.

## D7: Preserve a fixed update phase contract

- **Context:** OG and current C++ update order differ, and moving solids become gameplay-significant once they are actors.
- **Decision:** keep the explicit C++ order: input → player state → moving solids → motor → late contact → respawn → camera → actor reactions → resolve add/remove.
- **Consequences:** actor lifecycle work gains a testable frame contract instead of inheriting OG ordering by accident.
- **Alternatives rejected:** letting actor updates float freely would make moving-platform parity nondeterministic.

## Assumptions resolved from code

- OG movement reads `VirtualButton` / `VirtualStick` abstractions, not raw controller state. Source: code @ `Celeste64-og/Source/Data/Controls.cs:6-14`.
- OG `Player` uses buffered press consumption, ground snap cancellation, wall snap/corner helpers, and stored platform velocity. Source: code @ `Celeste64-og/Source/Actors/Player.cs:722-899`, `1450-1618`.
- OG world queries are actor-backed and include deferred lifecycle, typed lookup, raycasts, wall checks, overlaps, and recycling. Source: code @ `Celeste64-og/Source/Scenes/World.cs:155-265`, `412-629`.
- OG moving solids notify riders while temporarily disabling their own collision. Source: code @ `Celeste64-og/Source/Actors/Solid.cs:122-150`.
- Current C++ already has a valuable clean boundary: timer/input → state → motor → late contact → respawn → camera. Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp:213-276`.
- Current C++ world is still a fixed-array graybox room, and the actor base is intentionally thin. Source: code @ `src/user/gameplay/world/world.hpp:16-145`, `src/user/gameplay/actor/actor.hpp:9-23`.
- FosterFramework 0.1.18-alpha is the package version named by OG, while the public Foster repo is now on a newer second iteration; this is why historical source archaeology is needed instead of trusting current APIs. Source: code @ `Celeste64-og/Celeste64.csproj`; upstream package/repo references recorded during research.

## Open questions (from review)

- The full coroutine layer is only needed by some non-movement states; if memory pressure becomes acute, defer it until the first source actor truly needs it.
