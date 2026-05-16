# Data model / runtime contracts

No database or persistent storage changes. The "model" for this work is the in-memory compatibility surface that lets source-shaped gameplay code compile against stable concepts instead of re-deriving them ad hoc.

## Entities & relationships

```text
InputFrame
  ├── VirtualButtonState[Jump, Dash, Climb, Confirm, Cancel, Pause]
  └── VirtualStickState[Move, Menu, Camera]

World
  ├── ActorSlot[]                // deferred add/remove, typed lookups
  ├── Solid[]                    // actor-backed collision geometry
  └── BroadphaseGrid<Solid>

Solid : Actor
  └── Face[] + transformed bounds // ray / wall queries

PlayerState
  ├── ContactState
  ├── PlatformCarryState
  └── movement state machine adapter

Traits
  ├── Pickup
  ├── Pushout
  └── RidePlatform
```

## Constraints & invariants

- `VirtualButtonState.pressed` stays readable until consumed or its buffer expires; once Inc 2 lands, buffered input becomes the only owner of jump/dash buffering and `PlayerState.jump_buffer_remaining` is removed or reduced to an adapter field with no independent timing.
- `VirtualStickState.value` applies the configured deadzone once at the input boundary; gameplay code does not reapply deadzones, and the current analog magnitude remap remains a post-deadzone gameplay transform.
- actor add/remove is deferred until a resolve point; an actor cannot appear in typed queries before `Added()` and cannot remain after `Destroyed()`.
- `Solid` owns stable transformed bounds plus face data; query results carry face identity and owner identity back to gameplay.
- player motion code requests velocity; the motor remains the only writer of resolved post-move position/contact.

## Query patterns

1. Read buffered jump/dash presses during state updates, then consume them exactly once.
2. Ask `World.All<T>()`, `Get<T>()`, and `Overlaps<T>()` while actors are being added/destroyed without iterator invalidation.
3. Raycast from player/camera against collidable solids and retrieve nearest hit, normal, owner, and backface policy.
4. Query wall hits near the player's waist/head and choose nearest or closest-to-normal for climb / wall-jump logic.
5. Move a solid, propagate rider displacement/velocity, then let the player reuse stored platform velocity on jump.
6. Query actor traits (`Pickup`, `Pushout`, `RidePlatform`) without bespoke scene code.

## Sample rows / states

```text
Jump button after tap: {down=false, pressed=true, buffer_remaining=0.083, consumed=false}
Move stick held lightly: {raw=(0.22, 0.10), value=(0,0)}
Solid rider frame: {solid_id=12, velocity=(2,0,0), rider_count=1}
Deferred actor add: {id=31, type=Refill, phase=pending_add}
```

## Migration plan

N/A — in-memory only. Introduce the new runtime contracts beside the current direct paths, migrate call sites incrementally, then remove compatibility shims once parity tests cover the new surface. Beginning in Inc 5, actor/world solids are the authoritative collision source whenever a `Room` is attached to an actor world; `Room` remains only the temporary motor-facing envelope and legacy fallback.

## Backwards-compatibility window

During the migration window, existing `PlayerInput`, `Room`, and direct collectible code stay usable while adapters feed the new compatibility layer. Each increment must leave the ROM bootable and the current smoke suite green. Actor update order stays explicit:

```text
input → player state → moving solids → motor → late contact → respawn → camera → actor reactions → resolve add/remove
```

## Backfill

N/A — no stored data. Backfill means adding fixture coverage before swapping a subsystem over.

## Rollback

Revert by increment. The chosen order keeps the player/motor boundary intact until the final parity pass, so any failed increment can be removed without rewriting authored room data.
