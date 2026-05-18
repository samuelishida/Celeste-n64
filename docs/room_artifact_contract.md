# Room Artifact Contract

This project now treats gameplay data and visible static geometry as two related
artifacts with different jobs:

```txt
rom:/lvl/<room>.lvl    gameplay artifact: LVL1 collision, entities, metadata
rom:/lvl/<room>.t3dm   render artifact: static visible room geometry + materials
```

## LVL1 compatibility policy

The renderer overhaul deliberately keeps LVL1 on disk. Existing LVL1 face and
vertex sections stay readable for compatibility, but after cutover they are not
the shipping source of static room visuals. Introducing LVL2 or deleting LVL1
render payloads is a later schema migration, not part of this renderer change.

## Brush-class policy

Every brush-bearing source class must declare both axes before it may emit data:

```txt
render_mode: static_mesh | actor_model | none | unsupported
collision_mode: solid | actor_owned | trigger | none | unsupported
```

The first-room audit is expected to resolve the classes currently present in the
OG map, including at least `worldspawn`, `Decoration`, `SpikeBlock`,
`TrafficBlock`, and `DeathBlock`. No class may silently become visible or solid
just because it happens to carry brushes.

## Material policy

The legacy LVL1 render path may continue using manifest-based validation while it
exists. The active `.t3dm` render path must validate its own material references
before ROM bundling so TMEM safety follows the artifact that actually renders.
