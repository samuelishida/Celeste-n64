# Researcher Brief T1 — N64 Hardware Memory & Performance Constraints

**Output file:** `outputs/.drafts/n64-cpp-memory-opt-research-hardware.md`

## Mission
Gather primary, source-backed evidence on the Nintendo 64's memory and
performance hardware, focused on what a C++ game engine for N64 homebrew
actually has to respect.

## Must verify (with exact numbers + source URLs for each)
- RDRAM: 4 MB base, 8 MB with the Expansion Pak (and its 4.5 MB usable
  window / memory map details if sources support them).
- TMEM (texture memory): size (≈4 KB total, split into two 2 KB halves?),
  how it constrains texture formats, and what "one texel" costs.
- CPU caches on the R4300i: instruction cache size, data cache size, cache
  line size, and why DMA/cache coherency matters (write-back vs write-through
  behavior).
- DMA paths: PI (cartridge) bus width/speeds, SP/RSP DMAs, and alignment
  requirements (e.g., 8-byte / 64-byte alignment rules).
- RCP, RDP fill rates / bandwidth limits, VI limits (pixel clock, framebuffer
  sizes, 16/32-bit framebuffers).
- Cartridge ROM speeds, pre-fetch/read-ahead behavior, and why loading from
  ROM is slow (worst-case latency numbers if available).
- Anything about the 64-bit CPU running 32-bit code (why 32-bit pointers are
  standard on N64 homebrew) and MIPS alignment rules.

## Sources to prefer
- n64brew (N64 Brew Wiki) hardware pages
- Official N64 hardware docs (Nintendo Ultra64 manuals, if web-hosted)
- libdragon documentation (hardware-related pages)
- Reputable hardware explainers (Retro Game Mechanics Explained, YouTube
  transcripts, hardware blogs) — treat YouTube as secondary, prefer written
  sources.
- Avoid PDF text parsing: if only a PDF exists, cite its URL from search
  metadata and mark full-text parsing BLOCKED.

## Output format
Structured notes: each claim with URL. Distinguish verified facts from
inferences. End with a "gaps/blocked" list (e.g., PDF-only sources not
parsed). Write the file to the output path above. This is a research notes
file, not the final report — thorough but ragged is fine.