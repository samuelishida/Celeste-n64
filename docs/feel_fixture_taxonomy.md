# Feel fixture taxonomy

This overhaul keeps two fixture families separate:

- `motor-regression` — collision/query invariants that must stay stable while the controller changes (slope, ledge snap, platform carry, camera obstruction).
- `feel-spec` — controller outcomes owned by the new design spec, not by historical source traces.

`tests/movement_traces/` remains useful history, but any fixture describing controller feel is superseded by the scenarios in `tests/feel_spec/` once the new controller lands. Feel fixtures prefer bounds and relationships over a single magic coordinate whenever pad tuning is expected.
