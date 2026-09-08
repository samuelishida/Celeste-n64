# Researcher Brief T3 — N64 Homebrew (libdragon) Memory Practice + Commercial Games + Applicability

**Output file:** `outputs/.drafts/n64-cpp-memory-opt-research-practice.md`

## Mission
Two evidence tracks, plus an applicability synthesis seed.

## Track A — libdragon / N64 homebrew memory management
- How libdragon manages memory: the default heap (newlib malloc), `mallinfo`
  usage, stack sizes for threads/tasks, `sys_memory` (expansion pak detection,
  the 4 MB vs 8 MB check), DMA APIs (e.g., `dma_read`), the `dfs` filesystem
  and asset loading, RDPQ texture path (sprite upload to TMEM), and any
  documented memory budgets/limits.
- Prefer the libdragon GitHub repo README/docs, libdragon.net, n64brew wiki,
  and real libdragon homebrew projects' writeups.
- Note anything about compression/streaming assets in libdragon projects.

## Track B — commercial N64 games: what they actually did
- Texture/asset memory tricks (16/32-bit framebuffers, CI/8-bit paletted
  textures, mipmapping, texture reuse, vertex colors to fake detail).
- Level data handling: static in ROM, streaming, cutscene tricks.
- Named postmortems/interviews with concrete techniques: e.g., Super Mario 64,
  GoldenEye (Rare), Banjo, Conker, Zelda OoT/MM, F-Zero X (60 fps), Factor 5
  (Indiana Jones/Star Wars: Rogue Squadron — microcode, RDRAM tricks), Wipeout
  64, Jet Force Gemini — anything you can verify.
- Memory management techniques: custom allocators, memory pools, boot-time
  partition of the 4 MB (and how games handled the Expansion Pak).

## Track C — applicability seed (notes only)
Based on A + B, note which techniques are known to suit a small libdragon +
tiny3d engine with an 8 MB target and large streamed static maps. This feeds
the lead's synthesis; you do not need to inspect this repo's code.

## Sources to prefer
- libdragon GitHub / libdragon.net docs, n64brew wiki pages, developer
  interviews/postmortems (written or transcribed), Retro Gamer/articles.
- No PDF text parsing: cite PDF URLs from metadata, mark parsing BLOCKED.

## Output format
Structured notes with URLs per claim; mark inferences; end with
"gaps/blocked". Write to the output path above. Research notes only.