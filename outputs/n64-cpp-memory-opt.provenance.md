# Provenance: N64 Optimization & C++ Memory Optimizations

- **Date:** 2026-06-06
- **Rounds:** 1 research round (direct mode; no subagent rounds — see below)
- **Sources consulted:** 11 fetched + 2 attempted-but-blocked (n64brew 403,
  ultra64.ca extract fail) + 1 dead (Bitsquid 404). Search-API queries were
  attempted but BLOCKED (missing Google CSE credentials); the three research
  angles were instead covered via direct URL fetch of canonical sources.
- **Sources accepted (10 web + 1 local):**
  1. Wikipedia — Nintendo 64
  2. Copetti — N64 Architecture: A Practical Analysis
  3. libdragon.dev
  4. libdragon API — N64 System Interface
  5. libdragon API — DMA Controller
  6. libdragon API — Topics
  7. Game Programming Patterns — Data Locality
  8. Game Programming Patterns — Object Pool
  9. libdragon wiki — Compression
  10. IGN 2000 "Bringing Indy to N64" (via Wayback Machine)
  11. AGENTS.md (this repository — local ground truth)
- **Sources rejected (unverifiable/dead):** Bitsquid "Allocation Adventures 1"
  (404); Albrecht "Pitfalls of OOP" PDF (not parsed per workflow rule, cited
  only via GPP references); n64brew `Memory_map` (HTTP 403); ultra64.ca N64
  Programming Manual (extraction failed — referenced indirectly via Wikipedia's
  citation list).
- **Verification:** PASS WITH NOTES
  - Lead self-review (verifier/reviewer subagents BLOCKED: "Subagents require
    tmux"). Two MINOR findings fixed with small edits and confirmed on disk via
    `rg` (no `[SRC#]` markers remain; new wording present). Details in
    `outputs/.drafts/n64-cpp-memory-opt-verification.md`.
  - Blocked capabilities recorded: `subagent` (tmux requirement),
    `web_search` (credentials). Degraded-mode plan updates in
    `outputs/.plans/n64-cpp-memory-opt.md` decision log. Memory not saved via
    `memory_remember` (tool absent from toolset).
- **Plan:** outputs/.plans/n64-cpp-memory-opt.md
- **Research files:**
  - outputs/.drafts/n64-cpp-memory-opt-research-direct.md (fetch log, notes, blocked list)
  - outputs/.drafts/n64-cpp-memory-opt-draft.md (un-cited draft w/ marker legend)
  - outputs/.drafts/n64-cpp-memory-opt-cited.md (cited brief; final candidate)
  - outputs/.drafts/n64-cpp-memory-opt-verification.md (self-review record)
  - outputs/n64-cpp-memory-opt.md (delivered brief = cited.md)
