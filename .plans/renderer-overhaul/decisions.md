# Decisions & assumptions

## D1: Prove boundaries before rewriting them

- **Context:** The room looks broken, but the current evidence implicates both the baker and the renderer path.
- **Decision:** Start with a known-good tiny3d fixture plus deterministic host-side bake diagnostics before changing architecture.
- **Consequences:** We spend one increment on truth-finding, then later work has a clean target and can say whether the source map, bake product, or renderer is at fault.
- **Alternatives rejected:** Immediately rewriting the renderer; it would feel decisive while preserving the same bad inputs.

## D2: Split gameplay geometry from visible geometry while keeping LVL1

- **Context:** `.lvl` currently carries collision, entities, and render faces in one artifact, which makes a map-bake mistake look like an engine mistake.
- **Decision:** Keep LVL1 on disk during this overhaul, treat `.lvl` as gameplay truth, and introduce a separate static render artifact for visible room geometry. After cutover, LVL1 face/vertex payloads remain compatibility data only.
- **Consequences:** Collision can remain brush-friendly while rendering can use a format optimized for tiny3d. We avoid smuggling an unplanned schema migration into the renderer work.
- **Alternatives rejected:** Introduce LVL2 inside this plan; that is a real data-format migration with its own rollback cost and is not required to prove the new render path.

## D3: Use tiny3d's native static-model path for room rendering

- **Context:** tiny3d examples consistently use `T3DModel`, preprocessed parts/indices/materials, display-list recording, and optional object culling; its docs explain why the 70-vertex cache should be solved at build time.
- **Decision:** Static level visuals should follow the `.t3dm` model pipeline instead of a bespoke per-face raw-vertex renderer.
- **Consequences:** We inherit tiny3d's intended batching/material behavior and stop re-solving vertex-cache partitioning at runtime. Room render assets become a build-pipeline concern.
- **Alternatives rejected:** Keep the custom face renderer and improve it incrementally; feasible, but it duplicates work tiny3d already solves better and keeps us on a non-canonical path.

## D4: Treat the OG map as evidence, not sacred output

- **Context:** Current bake evidence shows all 402 faces with reversed winding relative to stored normals, 226 faces with duplicate vertices, 202 first-fan degeneracies, and render inclusion of Decoration/SpikeBlock/TrafficBlock/DeathBlock brush sets.
- **Decision:** The audit increment assigns separate render/collision policies to brush classes and emits one explicit route decision: keep a repaired map-derived render mesh, or re-author the first room for rendering while preserving `.map`-derived gameplay where useful.
- **Consequences:** The first room may cease to be a direct OG visual port, which aligns with the project goal of an N64-friendly rewrite.
- **Alternatives rejected:** Assume source parity is always the goal; that contradicts the project's own “small rewrite, not direct port” stance.

## D5: Move TMEM validation to the render artifact

- **Context:** The existing TMEM smoke test reads `1-1.manifest`, but `.t3dm` becomes the source of render material references after cutover.
- **Decision:** Keep manifest validation only for the legacy path and add deterministic `.t3dm`-aware material validation before bundling the new room asset.
- **Consequences:** The hardware budget stays first-class even after the renderer changes shape.
- **Alternatives rejected:** Rely on runtime upload assertions again; that already failed once.

## Assumptions resolved from code

- Static actor models already use tiny3d's native model path. Source: code @ `src/user/gameplay/render/model.cpp`.
- The current room renderer bypasses `.t3dm` and feeds raw packed vertices face-by-face. Source: code @ `src/user/gameplay/render/level_renderer.cpp`.
- The current baker emits brush geometry from all brush-bearing entity classes, not only `worldspawn`. Source: code @ `tools/bake_map.py` plus source scan of `assets/og_converted/maps/1-1.map`.
- Current bake smoke coverage only checks broad counts and does not validate winding, duplicate vertices, class policy, or render suitability. Source: code @ `tests/bake_map_smoke.py`.
- `tests/level_loader_test.cpp` still expects 8 colliders while the current baked room loads 402, so even the existing fixtures have drifted from reality. Source: code @ `tests/level_loader_test.cpp` and current bake output.
- First-room materials are already constrained to small TMEM-safe derivatives; that guardrail remains. Source: docs @ `docs/asset_budget.md` and tests @ `tests/material_budget_smoke.py`.
- tiny3d's basic examples prove that `t3d_model_draw` is the canonical baseline; object/material iteration is an optional optimization for culling scenarios. Source: code @ `tiny3d-main/examples/01_model/main.c`, `tiny3d-main/examples/17_culling/main.c`, and `tiny3d-main/examples/99_testscene/main.c`.
