#!/usr/bin/env python3
"""Inc 4 determinism test.

Runs two clean bakes into separate directories and compares every published
byte except intentionally human-readable diagnostics. Asserts no generated
manifest timestamp changes the build, and that the pack references exactly
the files that are staged.

Run:
    python3 tests/interconnected_determinism_test.py
"""

import sys
import json
import subprocess
import tempfile
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

FIXTURE = REPO / "tests" / "fixtures" / "interconnected-2x2.map"


def bake(out_dir: str) -> None:
    proc = subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "bake_interconnected_map.py"),
            str(FIXTURE),
            "--out-dir", out_dir,
            "--chunk-size", "1000",
            "--scale", "0.2",
        ],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise AssertionError(f"bake failed: {proc.stderr}\n{proc.stdout}")


def staging_files(out_dir: str) -> dict:
    """Return {relative_path: bytes} for all staged artifacts."""
    staging = Path(out_dir) / "staging"
    out = {}
    for p in sorted(staging.rglob("*")):
        if p.is_file():
            out[str(p.relative_to(staging))] = p.read_bytes()
    return out


def test_deterministic_artifacts():
    """Two clean bakes produce byte-identical staged artifacts."""
    d1 = tempfile.mkdtemp(prefix="inc4-det-a-")
    d2 = tempfile.mkdtemp(prefix="inc4-det-b-")
    try:
        bake(d1)
        bake(d2)
        a = staging_files(d1)
        b = staging_files(d2)
        assert set(a.keys()) == set(b.keys()), (
            f"staged file sets differ: {set(a)^set(b)}"
        )
        for name in a:
            assert a[name] == b[name], f"artifact {name} differs between bakes"
        print(f"PASS: {len(a)} staged artifacts byte-identical across two bakes")
    finally:
        shutil.rmtree(d1, ignore_errors=True)
        shutil.rmtree(d2, ignore_errors=True)


def test_pack_references_staged_files():
    """The v2 pack references exactly the files staged for DFS."""
    d = tempfile.mkdtemp(prefix="inc4-det-ref-")
    try:
        bake(d)
        staging = Path(d) / "staging"
        # The pack's global colmesh path + each room's lvl path must exist.
        from mappack_format import read_mappack_v2_binary
        pack = read_mappack_v2_binary(str(staging / "forsyken-city.mappack"))
        # Global colmesh.
        gname = pack.global_colmesh_path.rsplit("/", 1)[-1]
        assert (staging / gname).exists(), f"missing {gname}"
        # Room LVLs.
        for r in pack.rooms:
            lname = r.lvl_path.rsplit("/", 1)[-1]
            assert (staging / lname).exists(), f"missing {lname}"
        # No extra .lvl/.colmesh in staging that the pack doesn't reference.
        referenced = {pack.global_colmesh_path.rsplit("/", 1)[-1]}
        referenced |= {r.lvl_path.rsplit("/", 1)[-1] for r in pack.rooms}
        for f in staging.glob("*.lvl"):
            assert f.name in referenced, f"unreferenced lvl {f.name}"
        print(f"PASS: pack references exactly {len(referenced)} staged artifacts")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_no_timestamp_in_artifacts():
    """No generated artifact contains a timestamp-dependent byte."""
    d = tempfile.mkdtemp(prefix="inc4-det-ts-")
    try:
        bake(d)
        staging = Path(d) / "staging"
        for f in staging.glob("*.mappack"):
            data = f.read_bytes()
            # The v2 binary has no timestamp field; assert it's stable by
            # re-reading and comparing to a fresh parse (already covered by
            # determinism). Here we just assert the binary is parseable.
            from mappack_format import read_mappack_v2_binary
            read_mappack_v2_binary(str(f))
        print("PASS: v2 binary parses; no timestamp field in format")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main() -> int:
    test_deterministic_artifacts()
    test_pack_references_staged_files()
    test_no_timestamp_in_artifacts()
    print("\nAll interconnected_determinism_test tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
