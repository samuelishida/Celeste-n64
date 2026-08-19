#pragma once

namespace madeline_cube {

// Master gate for per-frame verbose logging (Inc 1 / D5). When false (release
// default), the per-frame `debugf` spam in `GameplayScene::Update` and the
// 60-frame telemetry burst are suppressed — USB serial output blocks the RSP
// and is a large hidden per-frame cost on N64.
//
// NOT gated (they are not per-frame hot and are needed for on-device
// measurement): boot/init logs and the 60-frame profiler report.
//
// Flip to true for a verbose diagnostics session (per-frame update/tick
// traces + position telemetry). Host tests assert this defaults false.
inline constexpr bool kVerboseFrameLogging = false;

// When true, boot teleports the player to the map-center cell's authored spawn
// instead of the manifest Start spawn (a corner). Used for on-device baseline
// capture at map center (streaming-memory-opt pre-step) where Ares cannot take
// controller input. The baseline capture is complete (build/baseline-*.txt);
// keep false so normal boot always uses the Start spawn.
inline constexpr bool kDebugTeleportToMapCenter = false;

}  // namespace madeline_cube
