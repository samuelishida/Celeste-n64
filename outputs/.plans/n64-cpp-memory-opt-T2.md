# Researcher Brief T2 — C++ Memory Optimization Techniques for Consoles

**Output file:** `outputs/.drafts/n64-cpp-memory-opt-research-cxx.md`

## Mission
Survey proven C++ memory-optimization techniques relevant to a small
console/embedded game engine (think: N64-class hardware: 4–8 MB RAM, tiny
caches, no virtual memory, no OS heap guarantees). For each technique, capture
WHAT it is, WHY it helps memory/perf, and the classic PITFALLS.

## Technique families to cover (verify with sources, prefer game-dev/console sources)
1. Allocators: arena/arena-per-frame, linear/stack allocators, pool
   allocators, object pooling/free lists. Why malloc/new in hot paths is bad
   (fragmentation, cache, cost).
2. Layout: data-oriented design, SoA vs AoS, cache-line friendliness, struct
   packing/alignment (alignas, padding), packing booleans/bitfields.
3. Static & lifecycle: static allocation at boot, fixed budgets, avoiding
   heap growth at runtime, double buffering, ring buffers, job/task queue
   allocation patterns.
4. Data & code size: ROM-resident immutable data, const/RO data placement,
   compiler flags that shrink code (`-Os`, `-fno-exceptions`, `-fno-rtti`,
   `-ffunction-sections` + gc-sections), why exceptions/RTTI are avoided on
   consoles.
5. Math: fixed-point vs float on MIPS-class CPUs (only if sources support it;
   otherwise note as debated), look-up tables over computation.
6. Anything else credible you find (e.g., copy elision, arena'd strings,
   entity component systems specifically as a memory pattern).

## Sources to prefer
- Game engine blog posts (Our Machinery, Bitsquid, GDC Vault talk summaries,
  Game Programming Patterns, Squirrel Eiserloh / Mike Acton talks)
- CppCon/C++Now talk pages and writeups
- Embedded/console C++ references
- libdragon/newlib notes only as secondary here (T3 owns that)
- No PDF text parsing: cite PDF URLs from metadata, mark parsing BLOCKED.

## Output format
Structured notes by technique family: what / why / pitfalls, each with URL(s).
Mark inferences. End with "gaps/blocked". Write the file to the output path
above. Research notes, not final prose.