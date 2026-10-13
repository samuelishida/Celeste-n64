#!/usr/bin/env python3
"""Host-only contract tests for the OG asset registry."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from asset_manifest import load_manifest, validate_manifest, write_make_include  # noqa: E402


def fail(message: str) -> None:
    raise AssertionError(message)


def expect_error(manifest: dict, needle: str, roots: dict[str, Path]) -> None:
    diagnostics = validate_manifest(manifest, roots)
    if not any(needle in item for item in diagnostics):
        fail(f"expected diagnostic containing {needle!r}, got {diagnostics!r}")


def main() -> int:
    manifest_path = ROOT / "assets/og_converted/asset_manifest.json"
    manifest = load_manifest(manifest_path)
    diagnostics = validate_manifest(manifest)
    if diagnostics:
        fail("checked-in manifest invalid: " + "; ".join(diagnostics))

    with tempfile.TemporaryDirectory(prefix="asset-manifest-smoke-") as tmp_name:
        tmp = Path(tmp_name)
        roots = {"repo": tmp}
        source = tmp / "source.bin"
        source.write_bytes(b"fixture")
        base = {
            "schema_version": 1,
            "pipeline_version": "test",
            "assets": [{
                "id": "fixture",
                "kind": "model",
                "source": "source.bin",
                "outputs": [{
                    "path": "assets/og_converted/fixture.t3dm",
                    "format": "t3dm",
                    "package_path": "filesystem/mdl/fixture.t3dm",
                }],
                "status": "planned",
                "required": False,
                "package": False,
                "profile": "static_model",
                "dependencies": [],
            }],
        }
        # Planned output may be absent; planned entries cannot leak into DFS.
        if validate_manifest(base, roots):
            fail("planned optional fixture should validate")
        duplicate = copy.deepcopy(base)
        duplicate["assets"].append(copy.deepcopy(base["assets"][0]))
        expect_error(duplicate, "duplicate asset id", roots)
        traversal = copy.deepcopy(base)
        traversal["assets"][0]["source"] = "../source.bin"
        expect_error(traversal, "traversal", roots)
        required_missing = copy.deepcopy(base)
        required_missing["assets"][0]["status"] = "active"
        required_missing["assets"][0]["required"] = True
        required_missing["assets"][0]["package"] = True
        expect_error(required_missing, "missing required output", roots)
        stale = copy.deepcopy(base)
        stale["assets"][0]["source_sha256"] = hashlib.sha256(b"different").hexdigest()
        expect_error(stale, "source hash mismatch", roots)
        unknown_profile = copy.deepcopy(base)
        unknown_profile["assets"][0]["profile"] = "not-a-profile"
        expect_error(unknown_profile, "unknown profile", roots)

        # Generator output must be byte-for-byte stable and contain concrete
        # package targets, not only a variable assignment.
        active = copy.deepcopy(base)
        active["assets"][0]["status"] = "active"
        active["assets"][0]["required"] = False
        active["assets"][0]["package"] = True
        (tmp / "assets/og_converted").mkdir(parents=True)
        (tmp / "assets/og_converted/fixture.t3dm").write_bytes(b"t3dm")
        first = tmp / "first.mk"
        second = tmp / "second.mk"
        write_make_include({**base, "assets": [active["assets"][0]]}, first, roots)
        write_make_include({**base, "assets": [active["assets"][0]]}, second, roots)
        if first.read_bytes() != second.read_bytes():
            fail("generated Make include is not deterministic")
        text = first.read_text(encoding="utf-8")
        if "filesystem/mdl/fixture.t3dm:" not in text or "OG_ASSET_PACKAGE_FILES" not in text:
            fail("generated Make include lacks concrete staging rule")

    print("asset_manifest_smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
