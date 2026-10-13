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

// Temporary debug mode: when true, the player walks slowly forward every frame
// so Ares can exercise chunk transitions and movement without controller
// input. This is for reproducing the "crash after walking around" bug. NEVER
// leave true in normal builds.
inline constexpr bool kDebugAutoWalk = false;
// Autowalk distance budget in world units (streaming-experience Inc 1).
// kDebugAutoWalk alone caps the walk at 100 units — less than one 240-unit
// cell — so the standard capture may never cross a cell seam and never
// exercises streaming. Preload/async captures run autowalk at 260.0 to
// guarantee >=1 seam crossing. Default 100.0 preserves the legacy behavior.
inline constexpr float kDebugAutoWalkDistance = 260.0f;

// Deterministic movement diagnostics. Replaces controller input with a
// fixed-step run/stop/reverse/jump/dash/climb sequence and emits compact
// [phys] samples. Climb target is selected from real collision geometry.
// `make physics-autoplay-rom` enables this only in its isolated diagnostic
// build; normal ROMs keep it disabled.
#if defined(MC_DEBUG_PHYSICS_AUTOPLAY)
inline constexpr bool kDebugPhysicsAutoplay = true;
#else
inline constexpr bool kDebugPhysicsAutoplay = false;
#endif
#if defined(MC_DEBUG_PHYSICS_AUTOPLAY_MANTLE)
inline constexpr bool kDebugPhysicsAutoplayMantle = true;
#else
inline constexpr bool kDebugPhysicsAutoplayMantle = false;
#endif
#if defined(MC_DEBUG_PHYSICS_AUTOPLAY_CORNER)
inline constexpr bool kDebugPhysicsAutoplayCorner = true;
#else
inline constexpr bool kDebugPhysicsAutoplayCorner = false;
#endif
// Temporary live-controller trace for the Ares repro session. Reset false
// after capture; emits ~10 Hz samples plus input/state/contact transitions.
#if defined(MC_DEBUG_INPUT_RECORDING)
inline constexpr bool kDebugInputRecording = true;
#else
inline constexpr bool kDebugInputRecording = false;
#endif

// Per-cell scale-in deforms terrain while async-loaded. Keep disabled: stream
// cells at full scale; any future fade must change opacity/color, never geometry.
inline constexpr bool kEnableCellReveal = false;

// render-distance-and-scaleup Inc 1: backface-cull the near pass. tiny3d's
// RSP microcode performs the per-triangle winding test when T3D_FLAG_CULL_BACK
// is set (rspq_triangle.inc FACE_CULLING), rejecting backfaces BEFORE the RDP
// — a closed city mesh is ~50% backfaces, so this roughly halves RSP triangle
// work + RDP fill. Default OFF: the og-baked Forsaken City map loses floor
// textures with culling enabled (user-verified device fix, 2026-08-24
// backcull baselines); only flip ON after every baked surface is proven
// double-clean under winding tests.
inline constexpr bool kEnableBackCull = false;

// Stable recovery profile: use one near pass until the separate DLOD projection
// is visually qualified. Its depth reset and ownership handoff previously
// produced flicker/coverage artifacts on this map.
inline constexpr bool kEnableDistantTier = false;

// Procedural hair is a separate vertex renderer, not part of Tiny3D's joint
// skinning. Keep it off until its head anchor and frame cost are device-proven.
inline constexpr bool kEnablePlayerHair = false;

// memory-opt Inc 2: route cell/map-pack loads through libdragon's asset
// compression layer (level-1 LZ4, documented faster-than-uncompressed) when
// the bake shipped compressed files. Host builds (no __mips__) always use the
// plain-fopen path so host tests exercise the same parsing. The shipped
// filesystem and this flag MUST flip together (a mismatch fails loud via the
// CMSH magic canary).
inline constexpr bool kEnableCompressedCells =
#ifdef __mips__
    true
#else
    false
#endif
    ;
// Reveal ramp duration in seconds (0.3–0.5 s per the plan).
inline constexpr float kRevealDurationSec = 0.4f;
// Reveal start scale (a degenerate point would be invisible; epsilon avoids
// a zero-scale matrix).
inline constexpr float kRevealEpsilon = 0.03f;

// Temporary reproduction knobs for the "spawn slightly left + rotate 5° + walk
// sideways" glitch. Used to exercise the exact camera/frustum/ring boundary
// where the geometry split appears. NEVER leave non-zero in normal builds.
inline constexpr float kDebugSpawnOffsetLeft = 0.0f;   // world units, +X is right
inline constexpr float kDebugCameraRotateDeg = 0.0f;  // degrees, rotates camera from map-center facing

}  // namespace madeline_cube
