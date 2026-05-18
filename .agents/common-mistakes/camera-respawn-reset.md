# Reset camera state on respawn

- Symptom: after falling and respawning, the camera can remain too close until later camera input/motion corrects it.
- Cause: respawn rewinds `PlayerState`, but the scene can leave the camera's old obstructed boom state alive and only ease toward the checkpoint over later frames.
- Fix: when `RespawnSystem::Step(...)` returns true in scene orchestration, call `CameraController::Reset(camera, player.position)` before the normal camera step for that frame.
- Guardrail: `tests/scene_update_order_smoke.cpp` includes a respawn case that starts from an obstructed old boom and requires spawn-frame framing to match a fresh reset.
