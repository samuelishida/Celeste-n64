# Reference capture corpus

Run from the repository root:

```sh
python3 tools/reference_capture/reference_capture.py compare
python3 tools/reference_capture/reference_capture.py regenerate
```

The fixtures in `tests/movement_traces/` are host-side source-derived models.
They use the checked-in C# values from `Celeste64-og/Source/Actors/Player.cs`
without launching the C# game, so they are deterministic and usable before the
N64 runtime grows a full parity replay harness.

Each JSON file records its source symbols and keeps the C# coordinate system:
`+Z` is up. Later runtime parity code should adapt coordinates at its boundary
rather than rewriting these source-reference fixtures.
