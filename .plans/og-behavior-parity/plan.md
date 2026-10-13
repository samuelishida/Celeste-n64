# OG physics and animation behavior parity

## Context
User authorizes analysis and implementation of a replacement physics/animation contract faithful to the vendored Celeste64 source. Earlier mover consolidation incorrectly equated structural similarity with formula parity; animation-alignment follow-up added snaps absent from OG. Preserve the existing dirty tree. N64 +Y-up, center positions adapt OG Z-up feet positions at explicit boundaries. Scope covers all currently implemented locomotion (Normal, Dash, Skid, Climb), moving surfaces, spring launches, and player skeletal animation. Feather/bubble/cutscene/collectible cinematics and procedural ponytail need separate actor systems; do not claim whole-game equality before those exist.

## Architectural decisions
- One shared OG resolver, ordered kinematic and velocity sweeps plus final Popout; preserve BVH acceleration but reproduce query normal/cull/ranking semantics before selection.
- State handlers issue ordered animation commands (Play, Clear, restart, rate) directly into engine-light state. No renderer reconstruction from final flags.
- Fixed simulation ticks own animation clocks, facing and squash; render produces buffered skeleton matrices once per frame. Never rotate matrix buffers per catch-up tick.
- Bounded active tracks reproduce SkinnedModel's retained fading clocks and blend-duration table. Verify pinned SharpGLTF weight semantics before translating blends.
- Restore source climb constants, lip sample, branch order, corner rays, visual facing smoothing and grounded contact ownership. Changes to intentional previous safeguards require source-derived fixtures and baked-map verification.
- Keep current user-chosen model size; validate conversion/forward basis and calibrate feet placement. Wall clearance is a known visual adaptation, not OG physics parity.

## Answers from code
OG Player.cs: ClimbSpeed=40, lip offset=1; Normal airborne Clear+Run; climb selection uses translated input, not velocity. SkinnedModel owns blend clocks and preserved fading tracks. Current animation advances with render delta; physics has fixed 60Hz ticks and whole-world hitstop. Mesh ray normals are flipped before caller cull, masking backfaces. Wall queries prematurely filter slopes and flip signed normals.

## Risks accepted
- CMSH now preserves authored polygon IDs, groups fan triangles, and streams every overlapping candidate before retaining the earliest eight canonical source faces. Exact OG `SolidGrid` bucket/insertion ordering on ties remains unqualified; the separate legacy sphere-query helper still has a 256-triangle cap.
- Existing tests may enforce historical adaptations rather than OG behavior; revise only with source evidence, retain penetration/finite-edge safeguards.
- N64 animation DMA cost: use persistent streams and pose-only skeletons; matrix generation once per rendered frame. Device profiling and identity capture required.
- Original dependencies and source bugs must be inspected, not replaced by assumed conventions.

## Increment DAG
1 source/dependency ledger (M) -> 2 physics semantics (L), 3 animation contract (L); 2+3 -> 4 integration/device evidence (M).

## Increments
### 1 source/dependency ledger (M) — done
Files: docs/og_behavior_gap_audit.md; this plan. Compare current source against OG Player, World, SkinnedModel, Utils, StateMachine and pinned dependency helpers. Record each formula/query with evidence, previous failed assumption, executable check and residual limitations. Review plan independently before editing.
### 2 physics semantics (L) — done
Files: player_controller.{hpp,cpp}, movement_config.hpp, player_state.hpp, player_movement.cpp, world.cpp, physics/coll_mesh.cpp, runtime/math.* and physics regression tests.
Preserve external scene phase APIs; repair synchronous transition hook ordering, per-state source formulas and collision culling before nearest-hit selection. Keep OG physical grounded flag owned by late-contact phase, not Popout. Port correct climb basis/corner behavior and query slope filters. Test front/back finite surfaces, nearest-front behind nearest-back, slope ranking, climb/lip rate, dash/skid launch boundaries and moving-surface carry.
### 3 animation contract (L) — done
Files: render/player_animation.hpp (engine-light commands/clocks), render/animated_model.{hpp,cpp}, player_state.hpp, player_controller.cpp, animation tests.
Command data: clip, restart, clear, inherited/global rate. State hooks own commands; bounded clock/weight model preserves interrupted transitions. Tiny3D samples pose-only skeletons and blends into buffered output. Missing clips fail load; non-finite dt rejected. Port source squash on jumps/spring/land and .8s recovery; visual Facing approaches after movement.
### 4 integration/device evidence (M) — done
Files: scene/gameplay_scene.cpp, tests/run_host_tests.sh, docs/physics_feel_acceptance.md, docs/og_port_gap_matrix.md and audit ledger.
Remove post-frame heuristic selector; advance player animation each non-hitstop simulation tick. Sample/matrix build once per render. Apply per-axis scale to model. Run host suite, compile ROM, capture Ares only if no live session (never start second emulator). Evidence must identify ROM SHA and boot/input; guided physics capture preferred when available. Document unproven visual equivalence and missing original actor features honestly.

## Cross-cutting verification
Source-derived numerical fixtures for semantics; whole host suite after integrated gameplay edits; compile-rom.sh after N64 changes; installed Ares Windows path via existing capture tooling. Passing host checks is not proof of every baked-map transition.

## Standards/common mistakes
No .agents/standards or common-mistakes index exists. Read actual common-mistakes: mid-refactor-working-tree, visual-facing-must-track-target-facing (supersede added snaps using OG LateUpdate), climb-corner-model-clearance, air-movement-parity, fixed-step-backlog-must-be-visible, plane-query-geometry-bound; learnings/og-single-mover-port.

## Review considerations
Independent review applied: late-contact landing squash precedes same-tick recovery/facing/clocks; retain zero-blend tracks; sole zero-weight track samples directly; carry shifts corner endpoints by resolved displacement; cull before nearest selection. Add source-derived numerical traces and command-order fixtures. Remaining whole-game actor/procedural-hair gaps must stay explicit.


## Implementation closure
- Source output adapter now negates inward brush clipping planes before polygon normals/winding; canonical and legacy writers agree. Rebaked45 visual cells and global10618-triangle CMSH. Regression independently checks brush interior signs, not just triangle/polygon agreement.
- Synchronous transitions, source corner rays/basis/lateral controls/camera ease, climb40/lip1, late contact/normal/facing/squash, authored culling before nearest, moving-wall carry and retained animation clocks implemented.
- Pinned SharpGLTF pose blending has executable fixtures. Source animation metadata validates durations/bones and holds STEP scales. Initial delayed keys are padded at0; Skid duration .833333313 now matches original. STEP scale discrepancies were only float roundoff, not visible scale jumps.
- Completed simulation ticks own actor clocks and camera phases. Render only samples skeletal poses and buffered matrices; rom_main rspq_wait protects root matrix reuse.
- Host closure:95 passed,0 failed (/tmp/hawk-parity-closure-suite.log). ROM compilation succeeds (/tmp/hawk-parity-closure-build.log).
- Windows Ares strict autoplay captures qualified walljump, mantle and corner; filenames and identities recorded in docs/og_behavior_gap_audit.md. Final interactive ROM boot/load capture verifies exact artifact,7 clips, stable supported idle and raw shoulders0.
- Final ROM1540096 bytes SHA256396eb6ac68c05f1b90010918f1b7fd4f5cb57e073e9cb583c7e15a3d3b338ce1.
- Manual image/animation equivalence has not been proven by telemetry. Existing model size/wall visual clearance and explicit unsupported actor/procedural-hair features remain documented adaptations/gaps; whole-game equivalence is not claimed.
