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

// streaming-experience Inc 4: per-cell reveal animation gate. When true, a
// cell that enters the drawn 3×3 while still loading (or whose staged parse
// lands while it is already drawn) animates in via a matrix pivot-scale about
// its render origin (epsilon → 1 over `kRevealDurationSec`) instead of a hard
// pop. Preloaded cells (READY before any drawn frame) snap at 100% with zero
// animation. When false, all cells draw at scale 1.0 (today's behavior,
// byte-identical path) — the A/B rollback. The reveal composes into the
// CPU-built camera-relative matrix only; the precompiled RSPQ block is
// unchanged, so it adds zero RSP cost.
inline constexpr bool kEnableCellReveal = true;

// render-distance-and-scaleup Inc 1: backface-cull the near pass. tiny3d's
// RSP microcode performs the per-triangle winding test when T3D_FLAG_CULL_BACK
// is set (rspq_triangle.inc FACE_CULLING), rejecting backfaces BEFORE the RDP
// — a closed city mesh is ~50% backfaces, so this roughly halves RSP triangle
// work + RDP fill. Default OFF: the og-baked Forsaken City map loses floor
// textures with culling enabled (user-verified device fix, 2026-08-24
// backcull baselines); only flip ON after every baked surface is proven
// double-clean under winding tests.
inline constexpr bool kEnableBackCull = false;

// render-distance Inc 4/5: the whole-map horizon tier (DLOD) — merged into
// ONE block. Default OFF: the merged single-block approach regressed the
// frame (146 ms vs 48.8 ms near-only) because it forces all distant geometry
// through one block every frame with no per-cell culling. Keeping it OFF
// preserves the shipped perf; flip ON only after the distant pass is made
// cheap (per-cell cull + smaller budget).
inline constexpr bool kEnableDistantTier = false;

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
