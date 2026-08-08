# Missing `ResetPlayerToRoomStart()` after level load

## Symptom
Player spawns at `(0, 0, 0)` or `(0, 30, 0)` instead of the PlayerSpawn
position from the baked level, then immediately enters an infinite hazard
respawn loop. Telemetry shows `pos=(0.000,30.000,0.000)` with velocity 0
and `[hazard] kind=N` each frame.

## Cause
`LoadLevel()` fills `Room::player_start` and `Room::checkpoint` from the
PlayerSpawn entity in the LVL file, but these don't automatically propagate
to the runtime `Impl::player.position` or `Impl::checkpoint`. The
`ResetPlayerToRoomStart()` method does that synchronization — and it is not
called during `ReloadLevel()` or `Init()`.

The default checkpoint `{0, 30, 0}` (from `world.hpp`) often happens to
fall inside a DeathBlock kill volume, causing the infinite loop.

## Fix
Call `ResetPlayerToRoomStart()` at the end of `ReloadLevel()`, right after
all level state is swapped in and before `baked_level_loaded_` is set.

## Prevention
Every code path that calls `ReloadLevel()` or `LoadLevel()` must ensure
player position and checkpoint are synchronized afterward. Since all level
loading goes through `ReloadLevel()`, placing the call there covers both
initial boot and cassette-triggered reloads.
