# Inc 2 notes - placeholder asset catalog

## Goal

Name what each asset means before replacing it with final art. The prototype only needs silhouettes that preserve gameplay readability.

| Source asset / entity family | Semantic placeholder ID | Meaning in play | Placeholder |
|---|---|---|---|
| `player.glb` | `actor_player` | controllable avatar | colored capsule + small head sphere |
| `strawberry.glb` | `pickup_strawberry` | collectible | red sphere + green cone |
| `refill_gem*.glb` | `pickup_refill` | dash refill | cyan diamond or octahedron |
| `coin.glb` | `pickup_coin` | pickup / gate token | yellow thin cylinder |
| `feather.glb` | `pickup_feather` | flight pickup | tall yellow quad |
| `cassette`, `tape_*` | `pickup_cassette` | submap reward / rhythm cue | magenta box |
| `spring_board.glb` | `actor_spring` | launcher | short green box with arrow decal stand-in |
| `spike.glb`, `spike_ball.glb` | `hazard_spike` | hazard | cone / spiked sphere surrogate |
| moving, falling, gate, traffic, cassette blocks | `solid_*` family | solid gameplay blocks | color-coded boxes |
| `granny` | `npc_granny` | NPC dialogue actor | distinct colored capsule |
| `badeline` | `npc_badeline` | NPC dialogue actor | distinct colored capsule |
| `theo` | `npc_theo` | NPC dialogue actor | distinct colored capsule |
| `sign*.glb` | `interact_sign` | interactable sign | upright quad on a thin box post |
| `tree1`, `bush1`, `grass1` | `decor_foliage_*` family | decorative foliage | cylinder + cone / green spheres |
| `hydrant`, `bucket`, `antenna` | `decor_prop_*` family | decorative props | simple boxes and cylinders |
| `car_*` | `prop_intro_car` | intro prop | stacked boxes + cylinders |
| room solids / decorations | `room_*` family | traversal space | boxes, wedges, and flat colored quads |

## Rules

- Placeholder names follow gameplay meaning, not source-file names: `pickup_strawberry`, `npc_granny`, `hazard_spike`.
- Every placeholder must preserve collision intent, interaction radius, and player readability before it preserves visual resemblance.
- Final asset conversion replaces placeholder render bindings later; actor IDs and gameplay semantics should survive that swap unchanged.
