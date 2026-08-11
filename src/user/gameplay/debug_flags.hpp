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

}  // namespace madeline_cube
