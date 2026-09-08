# Verification — n64-cpp-memory-opt (lead self-review, direct mode)

Reviewer/verifier subagents were BLOCKED (`Subagents require tmux`); per the
workflow's degraded-mode rules, the lead performed citation verification and
review directly.

## Checks performed

- **C1 — Critical-claim → source mapping:** every number/figure in the cited
  draft was traced to a fetched URL or the repo-local AGENTS.md observation.
  Verified list: 4 MB/8 MB, 4.5 MB physical + 9-bit bus, 640 ns RDRAM latency,
  250 MHz bank clock, ~500 MB/s sequential, ~5 MB/s cartridge, 93.75 MHz CPU /
  125 MIPS, 62.5 MHz RCP, 24 KB L1 (16 I / 8 D), 4 KB TMEM / 8 tiles / mipmap
  halves TMEM, 16-byte cache lines, DMA 8/2/2-byte alignment rules,
  0xA0000000 uncached segment, `sys_get_heap_stats{total,used}`, LZ4/Aplib/
  Shrinkler benchmarks (156/237/1133 ms; 6577/4318/904 KiB/s; 35.5/30.2/27.7%),
  Brew Volley boot 112 vs 139 ms, GPP ~50× toy-benchmark gap, repo arena
  2.16–2.29 MB.
- **C2 — Marker sweep:** `rg "\[SRC[0-9]+\]"` against cited.md → 0 matches
  (all markers resolved to inline links).
- **C3 — Fixes verified on disk:** `rg "commercial games and libultra were
  built on a 32-bit"` → 1; `rg "SM64 art-process claim"` → 1. Both small
  edits landed.
- **C4 — Single-source flags:** 640 ns / 500 MB/s (Copetti-only) flagged in
  Caveats; SM64 Gouraud attribution (Copetti-only) flagged in Caveats after
  review; GPP 50× flagged as toy benchmark.
- **C5 — Artifact completeness:** plan, research-direct, draft, cited,
  verification, delivery copy + provenance all present on disk (checked after
  delivery step).

## Findings

### FATAL
- None.

### MAJOR
- None.

### MINOR (accepted, fixed, or noted)
- M1 — 32-bit ABI phrasing in CPU section overreached libdragon's docs
  ("the ABI used by games is 32-bit" ambiguous). FIXED: reworded to match
  libdragon.dev's actual claim (commercial games/libultra 32-bit ABI; libdragon
  toolchain 64-bit capable).
- M2 — SM64 Gouraud/vertex-color claim was single-source (Copetti). FIXED:
  added explicit caveat; Wikipedia corroborates the 4096-byte limit but not
  the SM64-specific attribution.
- M3 — n64brew 403 and ultra64.ca extraction failure limit hardware
  corroboration; documented in Caveats + Open Questions. Accepted.
- M4 — No GoldenEye/Rare memory-layout postmortem reachable this pass; listed
  as Open Question 5. Accepted.

## Result

**PASS WITH NOTES** — no FATAL; two MINOR issues fixed with small localized
edits and verified on disk. Final candidate: `outputs/.drafts/n64-cpp-memory-opt-cited.md`.
