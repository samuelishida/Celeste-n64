# Compatibility contracts

## Input

```cpp
struct VirtualButtonState {
    bool down;
    bool pressed;
    bool released;
    bool consumed;
    float buffer_remaining;
};

bool ConsumePress(VirtualButtonState&);
struct VirtualStickState { Vec2 value; Vec2 raw_value; };
```

Required behaviour: source-style buffering, one-shot press consumption, circular deadzone at the input boundary, and sole ownership of jump/dash buffering once migrated.

## Runtime

```cpp
float Approach(float from, float to, float amount);
float AngleApproach(float from, float to, float max_delta);
bool OnInterval(float elapsed, float delta, float interval, float offset = 0);

class StateMachine;
```

Required behaviour: enough to express OG player state transitions without duplicating helpers inside each actor.

## World / actors

```cpp
ActorId Add(Actor&);
void Destroy(ActorId);
template<class T> T* Get();
template<class T> ActorView<T> All();
template<class T> T* OverlapsFirst(Vec3 point);
```

Required behaviour: deferred mutation, typed lookup, recycling hooks, and overlap queries that do not depend on bespoke scene code.

## Traits

```cpp
struct PickupTrait {};
struct PushoutTrait {};
struct RidePlatformTrait {};
```

Required behaviour: source-shaped trait queries for pickups, actor pushout, and rider propagation.

## Solids / queries

```cpp
RayHit SolidRayCast(...);
WallHit SolidWallCheckNearest(...);
WallHit SolidWallCheckClosestToNormal(...);
```

Required behaviour: actor-backed owner identity, transformed bounds/faces, broadphase pruning, and compatibility with the existing motor-facing `GroundHit / WallHit / CeilingHit` results.

Migration contract: keep `Room` as the motor-facing envelope while actor/world queries become the source of truth. Legacy `Room::colliders` are fallback-only once an actor world is attached.
