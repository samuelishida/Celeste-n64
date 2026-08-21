# Boot facing vs map layout (forward-wedge resident load)

## Symptom
At boot, the map content in front of the player is loaded last or behind other
cells — the forward-prioritized 3×3 ring (`ResolveForwardWedge`) loads all
Chebyshev-1 neighbors, but sorts so forward cells are first. If the camera
faces away from the map, the sort order is backwards and the most relevant
geometry is not prioritized, causing pop-in or a visually empty forward view
until the player turns around.

## Cause
The player and camera boot with default facing `+Z` (`{0, 0, 1}`) — set in
`player_state.hpp` (facing/target_facing/last_facing) and
`camera_controller.cpp::Reset` (cold-start `target_forward`). The Forsaken
City start spawn (`cell_00_00`) is at the `+Z` edge of the map (`iz=0`); the
entire map extends in `-Z` (`iz=-7..0`).

The resident pool is shaped by the camera's XZ forward direction
(`GameplayScene::Impl::CameraForwardDir` → `ResolveForwardWedge`), which prioritizes
forward cells but still loads the full 3×3 Chebyshev-1 ring. With `+Z` facing,
the camera looks **away** from the map, so the sort places back cells first
and forward cells are still resident but loaded last. The visible artifact is
less severe than a strict wedge, but boot still faces the wrong way.

## Fix
In `GameplayScene::Impl` boot (after `ResetPlayerToRoomStart`), orient the
player's `facing`/`target_facing`/`last_facing` and the camera toward the map
center (computed from `world_bounds`). Use `CameraController::OrientForward`
so the camera state is recomputed from `DesiredLookAt`/`DesiredPosition`
(single source of truth) — do NOT hand-roll the distance/height in
`gameplay_scene.cpp` (at `target_distance=0.5` the controller uses dist 85 /
height 55, not 60/30; hand-rolling causes a camera jump on the next Step).

## Fingerprint
If the boot view is empty and fills when the player turns, check the boot
facing direction, not the wedge/cull math.

## Lesson
The default `+Z` facing is a legacy placeholder from the single-room demo
where the map was centered on the origin. In the interconnected map-pack,
the start spawn is at a map edge, so the default facing points off the map.
Always orient the player toward the map content at boot.