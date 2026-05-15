# Decisions & assumptions

## D1: Rebuild natively instead of porting the C# runtime

- **Context:** The PC game depends on Foster, FMOD, SharpGLTF, SledgeFormats, .NET file IO, and runtime asset loading.
- **Decision:** Treat the C# project as the reference implementation and build a new native C runtime for the N64.
- **Consequences:** Behaviour must be revalidated against the source game, but the N64 code can be shaped around memory, DMA, and frame pacing from day one.
- **Alternatives rejected:** A mechanical C#-to-C port would keep source resemblance but drag platform-mismatched abstractions into the hardest parts of the project.

## D2: Use semantic placeholders before final asset conversion

- **Context:** The source game includes many distinct visual assets, but the immediate goal is validating the port rather than solving final art ingestion.
- **Decision:** First classify what each gameplay-relevant asset represents, then substitute primitive stand-ins such as boxes, spheres, capsules, cones, and quads during the validation phase.
- **Consequences:** The port can prove movement, room logic, and hardware viability earlier; final asset conversion becomes a later dedicated plan owned by the user.
- **Alternatives rejected:** Building the conversion pipeline first would spend the earliest milestones on content tooling before the core port is proven.

## D3: Use one vertical slice to set the production budget

- **Context:** The original build was made quickly and is not optimized; hardware viability cannot be inferred from asset count alone.
- **Decision:** Port a representative Forsaken City route plus the full player loop before broad content production.
- **Consequences:** The project gets hard numbers for geometry, animation, textures, audio, and frame time before committing to final visual scope.
- **Alternatives rejected:** Porting every subsystem first would delay the first meaningful hardware answer until too late.

## D4: Preserve gameplay fidelity ahead of visual fidelity

- **Context:** The player controller carries the game's identity, while many presentational details can be simplified for N64.
- **Decision:** Core movement timings, camera behaviour, collectibles, progression, and story flow outrank shader parity, dense decoration, and post effects.
- **Consequences:** Some effects may be stylized or removed, but cuts should not change the route logic or the feel review baseline.
- **Alternatives rejected:** A 1:1 visual target would spend the scarce budget on the least portable part of the source game.

## D5: Store saves as a fixed, versioned, dual-slot block

- **Context:** The source uses flexible JSON records, but cartridge storage is tiny and interrupted writes must not corrupt progression.
- **Decision:** Use compact numeric IDs, bitfields, checksums, and inactive-slot writes before flipping the active slot.
- **Consequences:** Saves are easy to validate and future migrations are explicit; free-form modded content is outside v1 scope.
- **Alternatives rejected:** A direct JSON-like save model wastes space and makes corruption recovery weaker.

## D6: Use libdragon plus Tiny3D for the native runtime

- **Context:** The demake needs direct access to N64 input, display, audio, DMA, and a practical 3D path without recreating the whole hardware stack first.
- **Decision:** Start from libdragon's stable branch for the platform layer and Tiny3D for the 3D renderer, while keeping game systems and asset formats project-owned.
- **Consequences:** The project gains a maintained homebrew base and a faster route to hardware tests, but it still must adapt its material and texture handling to Tiny3D's explicit model.
- **Alternatives rejected:** Building directly on bare metal gives maximum control but spends the earliest milestones on infrastructure instead of proving the game.

## D7: Target stock 4 MB hardware for the prototype

- **Context:** The first phase needs a concrete memory budget before room layout, actor counts, and placeholder rendering can be judged meaningfully.
- **Decision:** The validation prototype targets a base Nintendo 64 without Expansion Pak.
- **Consequences:** Every early profile number is honest for the lowest-memory target; optional richer tiers can be planned later instead of quietly becoming dependencies now.
- **Alternatives rejected:** Starting on Expansion Pak hardware would make early success easier to achieve while leaving the baseline viability question unanswered.

## Assumptions resolved from code

- The shipped game currently has one authored level, `Forsaken City`, with 30 strawberries and a root map plus submaps. Source: code @ `Content/Levels.json`.
- Map loading is entity-driven and already centralizes actor construction in a registry, so the demake can preserve the authored entity vocabulary while changing the storage format. Source: code @ `Source/Data/Map.cs:33`.
- The runtime currently loads maps, textures, glTF models, text, audio, and level metadata from disk at startup; these identify what later conversion work must eventually cover, but not what the validation phase must solve now. Source: code @ `Source/Data/Assets.cs:45`.
- Scene transitions already provide a natural place to save progression and swap scene-local assets. Source: code @ `Source/Game.cs:100`.
- Save data includes checkpoint, strawberries, completed submaps, flags, deaths, timer, and settings. Source: code @ `Source/Data/Save.cs:22`.
- The player controller is a bespoke multi-state implementation and is the main gameplay parity risk. Source: code @ `Source/Actors/Player.cs:5`.
- No repository-local engineering standards or plan guidance were found outside the user-provided skill instructions. Source: code search.
- Final asset conversion is intentionally deferred; the validation build uses semantic placeholders first. Source: user-confirmed.
- Placeholder identifiers must be semantic rather than file-based, e.g. `pickup_strawberry`, `npc_granny`, `hazard_spike`. Source: user-confirmed.
- The prototype targets stock 4 MB hardware. Source: user-confirmed.

## Open questions (from review)

- Is ROM distribution intended only for non-commercial fan use under the README's asset permission note, or should the plan include a clean-room asset path?
