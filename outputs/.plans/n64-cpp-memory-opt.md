# Deep Research Plan: N64 Optimization & C++ Memory Optimizations

- **Slug:** `n64-cpp-memory-opt`
- **Topic:** Nintendo 64 performance optimization and C++ memory optimization
  techniques, with focus on what is actionable for N64 homebrew (libdragon +
  tiny3d) development.
- **Request:** "Run deep research for: nintendo64 optimation/c++ memory optizmations"

## Key Questions

1. **Hardware constraints:** What are the N64's hard memory limits (RDRAM
   4 MB base / 8 MB with Expansion Pak, 4 KB texel TMEM, 16 KB instruction +
   data caches, DMA paths, bus/RDP bandwidth) and which ones actually bite a
   C++ game engine?
2. **C++ techniques:** Which C++ memory-optimization techniques are proven on
   consoles/embedded targets — arena/pool/frame allocators, alignment, POD,
   data-oriented design, cache-friendly layouts, avoiding heap churn in hot
   loops, static/ROM-resident data, fixed-point math?
3. **Homebrew practice:** How do libdragon and N64 homebrew engines actually
   manage memory — heap/mallinfo, DMA to RDRAM/TMEM, pak filesystem, texture
   budgets, streaming, compression?
4. **Commercial precedent:** What did real N64 games do (texture compression,
   level streaming, cutscene tricks, per-game memory layouts) per postmortems
   and interviews?
5. **Applicability:** What maps directly to this repo's architecture (8 MB
   Expansion Pak target, `[memory]` mallinfo telemetry ~2.16–2.29 MB arena,
   tiny3d near-pass renderer, tile-streamed whole-map)?

## Evidence Needed

- Spec-numbered sources for RDRAM / TMEM / cache / DMA limits (n64brew, N64
  hardware references, libdragon docs).
- Concrete C++ memory techniques with mechanics (why they work) and pitfalls —
  not just names. Prefer game-dev/console sources.
- libdragon memory-management API and observed behavior (malloc, `mallinfo`
  arena, `newlib`, dma, `dfs`, compression support).
- Commercial N64 postmortems/interviews with named techniques.
- Benchmarks/claims must be traced to sources; PDFs not fetchable → cite from
  metadata and mark parsing blocked.

## Scale Decision

**Broad, multi-faceted survey → 3 researcher subagents**, then verifier, then
reviewer. Rationale: topic spans hardware architecture + C++ language
technique + ecosystem practice + commercial history; decomposition into
non-overlapping briefs clearly helps, and the user's project gives it a
practical target. Not a narrow "what is X" explainer, so subagents are
justified.

- T1 = hardware constraints (Q1, partly Q4)
- T2 = C++ memory optimization technique landscape (Q2)
- T3 = homebrew practice + commercial games + applicability (Q3, Q4, Q5)

failFast: false throughout. Researchers use web search + web fetch only; if a
PDF-only source appears, cite its URL from search metadata and mark PDF text
parsing as BLOCKED rather than fetching it.

## Task Ledger

| # | Task | Owner | Status |
|---|------|-------|--------|
| 1 | Write plan artifact | lead | DONE |
| 2 | User approves plan | user | DONE |
| 3 | Write per-researcher briefs `outputs/.plans/n64-cpp-memory-opt-T{1,2,3}.md` | lead | DONE (briefs unused; subagents blocked) |
| 4 | Direct research hardware → `outputs/.drafts/n64-cpp-memory-opt-research-hardware.md` | lead | DONE (superseded: consolidated `research-direct.md` per direct-mode rules) |
| 5 | Direct research C++ → `outputs/.drafts/n64-cpp-memory-opt-research-cxx.md` | lead | DONE (superseded: consolidated `research-direct.md`) |
| 6 | Direct research practice → `outputs/.drafts/n64-cpp-memory-opt-research-practice.md` | lead | DONE (superseded: consolidated `research-direct.md`) |
| 7 | Update ledger + verification log after research | lead | DONE |
| 8 | Synthesize draft → `outputs/.drafts/n64-cpp-memory-opt-draft.md` | lead | DONE |
| 9 | Self-cite/verify → `outputs/.drafts/n64-cpp-memory-opt-cited.md` (verifier subagent BLOCKED) | lead | DONE |
| 10 | Self-review → `outputs/.drafts/n64-cpp-memory-opt-verification.md` (reviewer subagent BLOCKED) | lead | DONE (PASS WITH NOTES; 2 MINOR fixed) |
| 11 | Apply fixes (2 small localized edits; ≤3 so no revised file needed) | lead | DONE (verified via rg) |
| 12 | Deliver `outputs/n64-cpp-memory-opt.md` + `.provenance.md` | lead | DONE |
| 13 | On-disk artifact verification | lead | DONE (PASS) |

## Verification Log

| Step | Check | Status |
|------|-------|--------|
| V0 | Subagent spawn — BLOCKED (`Subagents require tmux`; session not under tmux) | FAILED (degraded to direct mode) |
| V1 | Research file `outputs/.drafts/n64-cpp-memory-opt-research-direct.md` on disk (replaces the 3 brief files; briefs still on disk) | PASS |
| V2 | Draft exists; every critical number maps to a source | PASS |
| V3 | `cited.md` exists on disk after self-verification; 0 `[SRC#]` markers | PASS |
| V4 | Review findings triaged; 2 MINOR fixed, 0 FATAL/MAJOR | PASS + NOTES |
| V5 | `rg` proof: fixed wording present (`commercial games and libultra`=1, `SM64 art-process claim`=1), markers gone | PASS |
| V6 | All required artifacts exist: plan, research dir, draft, cited, verification, output, provenance | PASS |

## Decision Log

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Slug | `n64-cpp-memory-opt` | ≤5 words, lowercase, hyphenated |
| Scale | ~~3 researchers + verifier + reviewer~~ → **lead-owned direct search** (DEGRADED) | Subagent calls failed: `Subagents require tmux`; session not under tmux. Per workflow degraded mode, lead performs research/cite/review. Direct-search rules now apply (no verifier/reviewer subagents). |
| Research method | Web search/fetch only; no PDF parsing | Workflow constraint; PDFs cited from metadata only |
| Applicability lens | Tie findings to 8 MB Expansion Pak, libdragon mallinfo arena, tiny3d, tile streaming | Matches repo facts in AGENTS.md |
| memory_remember | Skipped | Tool not present in this toolset (noted, non-blocking) |
| Capability failure | subagent (researcher/verifier/reviewer) | Error: `Subagents require tmux. Start pi inside tmux`. Lead continues in degraded mode; direct search minimum 3 queries satisfied by ~14 targeted queries. |
