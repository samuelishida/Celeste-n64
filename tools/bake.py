#!/usr/bin/env python3
"""Final OG → N64 bake CLI.

Loads ParsedMap once, validates, runs all four writers, generates manifest/report,
and publishes atomically.

Usage:
    python3 tools/bake.py <room.map> [--strict] [--out-dir DIR]
        [--scale 0.2] [--eps 1e-4]
        [--toolchain-dir DIR] [--fixture-manifest PATH]
"""

import sys
import os
import argparse
import hashlib
import json
import shutil
import tempfile
from pathlib import Path
from datetime import datetime
from typing import Optional

# Add tools to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from ogmap_lib import (
    parse_map, validate_scene, classify_entity, is_skipped,
    ParsedMap, MaterialClass,
)
from writers.colmesh_writer import write_colmesh, ColmeshStats
from writers.lvl_writer import write_lvl, LvlStats
from writers.t3dm_writer import write_t3dm, T3dmStats
from writers.nav_writer import write_nav, NavStats


class BakeConfig:
    """Configuration for bake operation."""
    def __init__(
        self,
        input_path: str,
        out_dir: str,
        scale: float = 0.2,
        eps: float = 1e-4,
        strict: bool = False,
        toolchain_dir: Optional[str] = None,
        fixture_manifest: Optional[str] = None,
    ):
        self.input_path = input_path
        self.out_dir = out_dir
        self.scale = scale
        self.eps = eps
        self.strict = strict
        self.toolchain_dir = toolchain_dir
        self.fixture_manifest = fixture_manifest


class BakeError(Exception):
    """Pipeline error with non-zero exit status."""
    pass


class BakeReport:
    """Report generated after bake operation."""
    def __init__(self):
        self.input_sha256: str = ""
        self.scale: float = 0.2
        self.eps: float = 1e-4
        self.toolchain: str = ""
        self.classes: dict = {}
        self.warnings: list = []
        self.counts: dict = {}
        self.hashes: dict = {}
        self.timestamp: str = ""


def compute_sha256(path: str) -> str:
    """Compute SHA-256 hash of a file."""
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()


def build_material_manifest(parsed_map: ParsedMap) -> list:
    """Build material manifest from parsed map."""
    materials = set()
    for ent in parsed_map.entities:
        for brush in ent.brushes:
            for face in brush.faces:
                materials.add(face["texture"])
    return sorted(materials)


def bake(
    config: BakeConfig,
) -> BakeReport:
    """Execute full bake pipeline.

    Args:
        config: BakeConfig with all parameters

    Returns:
        BakeReport with statistics

    Raises:
        BakeError: If pipeline fails
    """
    report = BakeReport()
    report.scale = config.scale
    report.eps = config.eps
    report.timestamp = datetime.now().isoformat()

    # 1. Load ParsedMap
    print(f"[bake] parsing {config.input_path}")
    parsed_map = parse_map(config.input_path)
    report.input_sha256 = compute_sha256(config.input_path)

    # 2. Validate classes and brushes
    print("[bake] validating scene")
    is_valid, messages = validate_scene(parsed_map, config.eps, config.strict)
    if config.strict and not is_valid:
        raise BakeError(f"Scene validation failed:\n" + "\n".join(messages))

    report.warnings.extend(messages)

    # Collect class statistics
    class_counts = {}
    for ent in parsed_map.entities:
        cd = classify_entity(ent)
        if cd:
            class_name = ent.classname
            class_counts[class_name] = class_counts.get(class_name, 0) + 1
    report.classes = class_counts

    # 3. Create temp directory for atomic publication
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir_path = Path(tmpdir)
        print(f"[bake] writing artifacts to temp dir")

        # 4. Run all four writers
        stats = {}

        # Colmesh
        colmesh_path = tmpdir_path / "output.colmesh"
        colmesh_stats = write_colmesh(
            parsed_map, str(colmesh_path),
            scale=config.scale, eps=config.eps, strict=config.strict
        )
        stats["colmesh"] = {
            "vertices": colmesh_stats.vertices,
            "triangles": colmesh_stats.triangles,
            "bvh_nodes": colmesh_stats.bvh_nodes,
        }

        # LVL
        lvl_path = tmpdir_path / "output.lvl"
        lvl_stats = write_lvl(
            parsed_map, str(lvl_path),
            scale=config.scale, eps=config.eps, strict=config.strict
        )
        stats["lvl"] = {
            "faces": lvl_stats.faces,
            "vertices": lvl_stats.vertices,
            "entities": lvl_stats.entities,
        }

        # T3DM (if toolchain available)
        t3dm_path = tmpdir_path / "output.t3dm"
        try:
            t3dm_stats = write_t3dm(
                parsed_map, str(t3dm_path),
                scale=config.scale, eps=config.eps,
                toolchain_dir=config.toolchain_dir, strict=config.strict
            )
            stats["t3dm"] = {
                "chunks": t3dm_stats.chunks,
                "vertices": t3dm_stats.vertices,
            }
        except (RuntimeError, NotImplementedError) as e:
            print(f"[bake] warning: T3DM generation skipped: {e}")
            report.warnings.append(f"T3DM generation skipped: {e}")

        # NAV
        nav_path = tmpdir_path / "output.nav"
        nav_stats = write_nav(parsed_map, str(nav_path), scale=config.scale)
        stats["nav"] = {
            "platforms": nav_stats.platforms,
        }

        report.counts = stats

        # 5. Generate manifest
        manifest = build_material_manifest(parsed_map)
        manifest_path = tmpdir_path / "output.manifest"
        with open(manifest_path, 'w') as f:
            for mat in manifest:
                f.write(mat + "\n")

        # 6. Compute hashes
        for name, path in [
            ("colmesh", colmesh_path),
            ("lvl", lvl_path),
            ("t3dm", t3dm_path),
            ("nav", nav_path),
        ]:
            if path.exists():
                report.hashes[name] = compute_sha256(str(path))

        # 7. Write report JSON
        report_path = tmpdir_path / "output.report.json"
        with open(report_path, 'w') as f:
            json.dump({
                "input_sha256": report.input_sha256,
                "scale": report.scale,
                "eps": report.eps,
                "toolchain": report.toolchain,
                "classes": report.classes,
                "warnings": report.warnings,
                "counts": report.counts,
                "hashes": report.hashes,
                "timestamp": report.timestamp,
            }, f, indent=2)

        # 8. Publish atomically
        out_dir = Path(config.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)

        print(f"[bake] publishing to {config.out_dir}")
        for f in tmpdir_path.iterdir():
            dst = out_dir / f.name
            shutil.copy(str(f), str(dst))

    return report


def main():
    parser = argparse.ArgumentParser(description="Bake OG map to N64 formats")
    parser.add_argument("input", help="Input .map file")
    parser.add_argument("--out-dir", default="build", help="Output directory")
    parser.add_argument("--scale", type=float, default=0.2, help="World scale factor")
    parser.add_argument("--eps", type=float, default=1e-4, help="Geometry tolerance")
    parser.add_argument("--strict", action="store_true", help="Fail on invalid brushes")
    parser.add_argument("--toolchain-dir", help="Path to N64 toolchain")
    parser.add_argument("--fixture-manifest", help="Path to fixture manifest (opt-in)")

    args = parser.parse_args()

    config = BakeConfig(
        input_path=args.input,
        out_dir=args.out_dir,
        scale=args.scale,
        eps=args.eps,
        strict=args.strict,
        toolchain_dir=args.toolchain_dir,
        fixture_manifest=args.fixture_manifest,
    )

    try:
        report = bake(config)
        print(f"[bake] done: {json.dumps(report.counts, indent=2)}")
        return 0
    except BakeError as e:
        print(f"[bake] error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
