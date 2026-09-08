# Provenance: Celeste64 Controls & Physics → N64 Port

- **Date:** 2026-09-07
- **Rounds:** 1 (lead-owned direct research; subagent spawn + web_search blocked, so no researcher/verifier/reviewer subagents ran)
- **Sources consulted:** 3 online URLs fetched (GitHub EXOK/Celeste64, maddymakesgamesinc.itch.io/celeste64, raw ControlsConfig.cs) + repo-local primary evidence (Celeste64-og/Source/Actors/Player.cs, Data/Controls.cs, Data/ControlsConfig.cs, Graphics/Camera.cs, Scenes/World.cs; src/user/gameplay/player/*, input/*, physics/*; docs/controller_spec.md, movement_spec.md, og_port_gap_matrix.md, physics_feel_acceptance.md, feel_fixture_taxonomy.md; tests/run_host_tests.sh)
- **Sources accepted:** 3 online (GitHub repo, itch page, ControlsConfig.cs raw) + all repo-local files listed above
- **Sources rejected:** https://exok.itch.io/celeste64 (404 — wrong URL; GitHub README points to maddymakesgamesinc.itch.io)
- **Verification:** PASS WITH NOTES — self-review found 0 FATAL / 0 MAJOR / 3 MINOR (Foster VirtualStick internals not in checkout — deadzone/buffer values confirmed from Controls.cs, circular-deadzone math inferred; spring constants live in spring_actor.cpp not movement_config.hpp; 57-smoke count is current green). All claims verified on disk via reads + rg before writing the verification file.
- **Plan:** outputs/.plans/celeste64-controls-physics-n64-port.md
- **Research files:** outputs/.drafts/celeste64-controls-physics-n64-port-research-direct.md, outputs/.drafts/celeste64-controls-physics-n64-port-draft.md, outputs/.drafts/celeste64-controls-physics-n64-port-cited.md, outputs/.drafts/celeste64-controls-physics-n64-port-verification.md
- **Blocked capabilities:** web_search (no GOOGLE_SEARCH_API_KEY/CSE id/auth.json), subagent spawn (session not under tmux), Foster VirtualStick/VirtualButton source (not in checkout)
