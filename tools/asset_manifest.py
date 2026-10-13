#!/usr/bin/env python3
"""Validate OG asset recipes and generate deterministic DFS prerequisites.

The manifest is intentionally host-side.  ROM code only consumes generated
metadata files and converted assets; it never parses this JSON.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import re
import sys
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
KNOWN_PROFILES = {
    "none",
    "static_model",
    "static_player",
    "animated_model",
    "model_atlas_ci4_64",
    "world_repeat_rgba16",
    "world_decal_rgba16",
    "sprite_ci8",
    "sprite_rgba16",
    "font",
    "audio_sfx",
    "audio_music",
}
ID_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _relative(path: Any, label: str, diagnostics: list[str]) -> str | None:
    if not isinstance(path, str) or not path:
        diagnostics.append(f"{label}: expected non-empty relative path")
        return None
    p = Path(path)
    if p.is_absolute() or ".." in p.parts:
        diagnostics.append(f"{label}: path traversal or absolute path is not allowed: {path}")
        return None
    normalized = posixpath.normpath(path.replace("\\", "/"))
    if normalized in (".", "") or normalized.startswith("../"):
        diagnostics.append(f"{label}: invalid normalized path: {path}")
        return None
    return normalized


def load_manifest(path: str | Path) -> dict[str, Any]:
    """Load JSON manifest, raising ValueError with a useful file diagnostic."""
    manifest_path = Path(path)
    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read manifest {manifest_path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"manifest {manifest_path}: top-level object required")
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _profile_name(profile: Any) -> str | None:
    if isinstance(profile, str):
        return profile
    if isinstance(profile, dict):
        name = profile.get("name")
        return name if isinstance(name, str) else None
    return None


def texture_profile_for_asset(asset: dict[str, Any], repo: Path | None = None) -> dict[str, Any]:
    """Resolve deterministic texture dimensions and sampling policy.

    Manifest entries keep the stable profile name; source dimensions are read
    here so UV conversion and runtime metadata cannot drift from the input.
    """
    repo = (repo or _repo_root()).resolve()
    name = _profile_name(asset.get("profile")) or "none"
    source = repo / asset.get("source", "")
    width = height = 0
    if name == "model_atlas_ci4_64":
        from glb_assets import embedded_image_info
        image_index = int(asset.get("transform", {}).get("embedded_image_index", 0))
        width, height, mime = embedded_image_info(source, image_index)
        return {
            "name": name,
            "source_width": width,
            "source_height": height,
            "output_width": 64,
            "output_height": 64,
            "format": "CI4",
            "palette_colors": 16,
            "palette_bytes": 32,
            "texel_bytes": 2048,
            "alpha_mode": "opaque",
            "wrap_s": "clamp",
            "wrap_t": "clamp",
            "filter": "nearest",
            "resize_filter": "nearest",
            "quantize": "MEDIANCUT",
            "dither": "NONE",
            "mse_rgb8_max": 4.0,
            "uv_scale_u": 0.5,
            "uv_scale_v": 0.5,
            "mime": mime,
        }
    try:
        from PIL import Image
        with Image.open(source) as image:
            width, height = image.size
    except (OSError, ImportError):
        pass
    if name in ("world_repeat_rgba16", "world_decal_rgba16"):
        # Keep smaller source tiles crisp at 32px. Larger repeating surfaces
        # use 16px tiles to cut base+mip upload traffic by 4x while preserving
        # the box-filtered mip chain. Both limits remain within RGBA16 TMEM.
        max_width = max_height = (
            16 if name == "world_repeat_rgba16" and max(width, height) > 64 else 32
        )
        if width > 0 and height > 0:
            factor = min(1.0, max_width / width, max_height / height)
            output_width = max(1, int(round(width * factor)))
            output_height = max(1, int(round(height * factor)))
        else:
            output_width = output_height = 32
        # RDP repeat masks require power-of-two tile dimensions. Preserve
        # repeat on each dimension that satisfies hardware; clamp a
        # non-power-of-two axis explicitly instead of boot-time assertion.
        is_pot = lambda value: value > 0 and (value & (value - 1)) == 0
        repeat = name == "world_repeat_rgba16"
        wrap_s = "repeat" if repeat and is_pot(output_width) else "clamp"
        wrap_t = "repeat" if repeat and is_pot(output_height) else "clamp"
        return {
            "name": name,
            "source_width": width,
            "source_height": height,
            "output_width": output_width,
            "output_height": output_height,
            "format": "RGBA16",
            "alpha_mode": "blend" if name == "world_decal_rgba16" else "opaque",
            "wrap_s": wrap_s,
            "wrap_t": wrap_t,
            # Repeating world surfaces need minification filtering at steep
            # viewing angles. Keep decals unmipped: their straight-alpha edges
            # would otherwise pick up color fringes during box reduction.
            "filter": "bilinear",
            "mipmap": "BOX" if name == "world_repeat_rgba16" else "NONE",
            # Prefilter opaque repeat textures before reducing their base
            # image; point-sampling source pixels aliases detail into every
            # generated mip level. Keep straight-alpha decals on nearest until
            # the converter has premultiplied-alpha resampling.
            "resize_filter": "BOX" if repeat else "NEAREST",
            "uv_scale_u": output_width / width if width else 1.0,
            "uv_scale_v": output_height / height if height else 1.0,
        }
    return {
        "name": name,
        "source_width": width,
        "source_height": height,
        "output_width": width,
        "output_height": height,
        "format": "RGBA16" if name != "sprite_ci8" else "CI8",
        "alpha_mode": "blend" if name in ("sprite_rgba16", "sprite_ci8") else "opaque",
        "wrap_s": "clamp",
        "wrap_t": "clamp",
        "filter": "nearest",
        "resize_filter": "nearest",
        "uv_scale_u": 1.0,
        "uv_scale_v": 1.0,
    }


def texture_profiles(manifest: dict[str, Any], repo: Path | None = None) -> dict[str, dict[str, Any]]:
    """Return profiles keyed by Quake material name, including sentinels."""
    repo = (repo or _repo_root()).resolve()
    profiles: dict[str, dict[str, Any]] = {}
    for asset in manifest.get("assets", []):
        if asset.get("kind") != "texture":
            continue
        profile = texture_profile_for_asset(asset, repo)
        outputs = asset.get("outputs") or []
        if outputs:
            package_path = outputs[0].get("package_path", "")
            if isinstance(package_path, str) and package_path.startswith("filesystem/"):
                profile["rom_path"] = "rom:/" + package_path[len("filesystem/"):]
        asset_id = asset.get("id")
        if isinstance(asset_id, str):
            profiles[asset_id] = profile
    # TrenchBroom's sentinel names are deliberately explicit no-texture slots.
    if "TB_empty" in profiles:
        profiles["__TB_empty"] = {
            **profiles["TB_empty"],
            "name": "none",
            "alpha_mode": "none",
            "wrap_s": "none",
            "wrap_t": "none",
            "filter": "none",
            "resize_filter": "none",
            "rom_path": "",
        }
    profiles["invisible"] = {
        "name": "none", "source_width": 0, "source_height": 0,
        "output_width": 0, "output_height": 0, "format": "none",
        "alpha_mode": "none", "wrap_s": "none", "wrap_t": "none",
        "filter": "none", "resize_filter": "none",
        "uv_scale_u": 1.0, "uv_scale_v": 1.0,
    }
    return profiles


def validate_manifest(manifest: dict[str, Any], roots: dict[str, Path] | None = None,
                      check_outputs: bool = True) -> list[str]:
    """Return deterministic diagnostics; an empty list means valid manifest."""
    roots = roots or {"repo": _repo_root()}
    repo = roots["repo"].resolve()
    diagnostics: list[str] = []
    if manifest.get("schema_version") != SCHEMA_VERSION:
        diagnostics.append(
            f"schema_version: expected {SCHEMA_VERSION}, got {manifest.get('schema_version')!r}"
        )
    if not isinstance(manifest.get("pipeline_version"), str) or not manifest["pipeline_version"]:
        diagnostics.append("pipeline_version: required non-empty string")
    assets = manifest.get("assets")
    if not isinstance(assets, list):
        return diagnostics + ["assets: required array"]

    ids: set[str] = set()
    output_paths: set[str] = set()
    package_paths: set[str] = set()
    known_ids = {
        item.get("id") for item in assets if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    for index, asset in enumerate(assets):
        prefix = f"assets[{index}]"
        if not isinstance(asset, dict):
            diagnostics.append(f"{prefix}: expected object")
            continue
        asset_id = asset.get("id")
        if not isinstance(asset_id, str) or not ID_RE.fullmatch(asset_id):
            diagnostics.append(f"{prefix}.id: invalid asset id {asset_id!r}")
        elif asset_id in ids:
            diagnostics.append(f"{prefix}.id: duplicate asset id {asset_id}")
        else:
            ids.add(asset_id)
        status = asset.get("status")
        if status not in ("active", "planned"):
            diagnostics.append(f"{prefix}.status: expected active or planned")
        if not isinstance(asset.get("required"), bool):
            diagnostics.append(f"{prefix}.required: expected boolean")
        package = asset.get("package")
        if not isinstance(package, bool):
            diagnostics.append(f"{prefix}.package: expected boolean")
        source = _relative(asset.get("source"), f"{prefix}.source", diagnostics)
        if source is not None:
            source_path = repo / source
            if status == "active" and not source_path.exists():
                diagnostics.append(f"{prefix}: missing active source {source}")
            expected_hash = asset.get("source_sha256")
            if expected_hash is not None:
                if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash):
                    diagnostics.append(f"{prefix}.source_sha256: expected 64 hexadecimal characters")
                elif source_path.is_file() and _sha256(source_path) != expected_hash.lower():
                    diagnostics.append(f"{prefix}: source hash mismatch for {source}")
        profile = _profile_name(asset.get("profile"))
        if profile not in KNOWN_PROFILES:
            diagnostics.append(f"{prefix}.profile: unknown profile {profile!r}")
        outputs = asset.get("outputs")
        if not isinstance(outputs, list) or (not outputs and profile != "none"):
            diagnostics.append(f"{prefix}.outputs: required non-empty array")
            outputs = []
        for out_index, output in enumerate(outputs):
            out_prefix = f"{prefix}.outputs[{out_index}]"
            if not isinstance(output, dict):
                diagnostics.append(f"{out_prefix}: expected object")
                continue
            output_path = _relative(output.get("path"), f"{out_prefix}.path", diagnostics)
            raw_package_path = output.get("package_path")
            if raw_package_path is None and status == "planned" and not asset.get("package"):
                package_path = None
            else:
                package_path = _relative(raw_package_path, f"{out_prefix}.package_path", diagnostics)
            if output_path:
                if output_path in output_paths:
                    diagnostics.append(f"{out_prefix}.path: duplicate output {output_path}")
                output_paths.add(output_path)
                if not output_path.startswith("assets/og_converted/") and not output_path.startswith("build/"):
                    diagnostics.append(f"{out_prefix}.path: outside generated output roots: {output_path}")
                output_file = repo / output_path
                expected_output_hash = output.get("sha256")
                if expected_output_hash is not None:
                    if not isinstance(expected_output_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_output_hash):
                        diagnostics.append(f"{out_prefix}.sha256: expected 64 hexadecimal characters")
                    elif output_file.is_file() and _sha256(output_file) != expected_output_hash.lower():
                        diagnostics.append(f"{out_prefix}: output hash mismatch for {output_path}")
                if check_outputs and asset.get("required") and status == "active" and not output_file.is_file():
                    diagnostics.append(f"{out_prefix}: missing required output {output_path}")
            if package_path:
                if not package_path.startswith("filesystem/"):
                    diagnostics.append(f"{out_prefix}.package_path: must be under filesystem/: {package_path}")
                if package_path in package_paths:
                    diagnostics.append(f"{out_prefix}.package_path: duplicate package path {package_path}")
                package_paths.add(package_path)
            if output.get("format") is not None and not isinstance(output.get("format"), str):
                diagnostics.append(f"{out_prefix}.format: expected string")
        dependencies = asset.get("dependencies", [])
        if not isinstance(dependencies, list) or any(not isinstance(dep, str) for dep in dependencies):
            diagnostics.append(f"{prefix}.dependencies: expected array of asset ids")
        else:
            for dep in dependencies:
                if dep not in known_ids:
                    diagnostics.append(f"{prefix}: unknown dependency {dep}")
                elif status == "active" and asset.get("package"):
                    dep_asset = next((item for item in assets if item.get("id") == dep), None)
                    if not isinstance(dep_asset, dict) or dep_asset.get("status") != "active" or not dep_asset.get("package"):
                        diagnostics.append(f"{prefix}: active packaged asset depends on inactive/unpackaged {dep}")
        if asset_id == "player" and status == "active":
            if profile not in ("static_player", "animated_model"):
                diagnostics.append(f"{prefix}: active player must use a supported player profile")
            if profile == "static_player":
                for output in outputs:
                    if str(output.get("path", "")).endswith((".animmap", ".sdata")):
                        diagnostics.append(f"{prefix}: active static player cannot package animation sidecar {output.get('path')}")
            if profile == "animated_model" and not any(str(output.get("path", "")).endswith(".sdata") for output in outputs):
                diagnostics.append(f"{prefix}: animated player must declare animation stream outputs")
            if profile == "animated_model" and sum(output.get("format") == "weighted_skin_sidecar" for output in outputs) != 1:
                diagnostics.append(f"{prefix}: animated player must declare exactly one weighted skin sidecar")
            if profile == "animated_model" and sum(output.get("format") == "animation_curve_sidecar" for output in outputs) != 1:
                diagnostics.append(f"{prefix}: animated player must declare exactly one source animation curve sidecar")
        if asset.get("kind") == "animation_manifest":
            source_asset = asset.get("source_asset")
            if source_asset not in known_ids:
                diagnostics.append(f"{prefix}.source_asset: unknown asset {source_asset!r}")
            elif source_asset not in dependencies:
                diagnostics.append(f"{prefix}: source_asset {source_asset!r} must be dependency")
        if status == "planned" and asset.get("package"):
            diagnostics.append(f"{prefix}: planned asset cannot be packaged")
        if status == "active" and asset.get("required") and not asset.get("package"):
            diagnostics.append(f"{prefix}: required active asset must be packaged")
    return diagnostics


def _make_quote(value: str) -> str:
    # Paths in this repository contain no whitespace. Keep generated Makefile
    # readable and reject unsafe shell metacharacters in manifest paths.
    if any(ch in value for ch in " \t\n;|&$`"):
        raise ValueError(f"manifest path contains unsupported Make characters: {value}")
    return value


def _active_package_outputs(manifest: dict[str, Any]) -> Iterable[tuple[dict[str, Any], dict[str, Any]]]:
    for asset in manifest.get("assets", []):
        if asset.get("status") != "active" or not asset.get("package"):
            continue
        for output in asset.get("outputs", []):
            yield asset, output


def write_make_include(manifest: dict[str, Any], output_path: str | Path, roots: dict[str, Path] | None = None) -> None:
    """Emit concrete conversion and staging rules for active packaged outputs."""
    # Include generation must be possible before outputs exist; individual
    # generated rules and DFS prerequisites fail closed when outputs are absent.
    diagnostics = validate_manifest(manifest, roots, check_outputs=False)
    if diagnostics:
        raise ValueError("manifest validation failed:\n" + "\n".join(f"- {item}" for item in diagnostics))
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Generated by tools/asset_manifest.py; do not edit.",
        "OG_ASSET_PACKAGE_FILES := \\",
    ]
    outputs = list(_active_package_outputs(manifest))
    if outputs:
        for index, (_, output) in enumerate(outputs):
            suffix = " " + "\\" if index < len(outputs) - 1 else ""
            lines.append(f"\t{_make_quote(output['package_path'])}{suffix}")
    else:
        lines[-1] = "OG_ASSET_PACKAGE_FILES :="
    lines.append("")
    for asset, output in outputs:
        generated = _make_quote(output["path"])
        package_path = _make_quote(output["package_path"])
        asset_id = _make_quote(asset["id"])
        source = _make_quote(asset["source"])
        dependencies = [
            source,
            "assets/og_converted/asset_manifest.json",
            "tools/asset_manifest.py",
            "tools/convert_og_assets.py",
        ]
        for dependency_id in asset.get("dependencies", []):
            dependency = next((item for item in manifest.get("assets", []) if item.get("id") == dependency_id), None)
            if dependency:
                dependencies.extend(
                    _make_quote(item["path"])
                    for item in dependency.get("outputs", [])
                    if isinstance(item, dict) and isinstance(item.get("path"), str)
                )
        if asset.get("kind") == "model":
            dependencies.append("tools/prepare_n64_model.py")
            if asset.get("profile") == "animated_model":
                dependencies.extend([
                    "tools/glb_assets.py", "tools/player_color_bake.py",
                    "tools/blender/prepare_static_player.py",
                    "tools/blender/prepare_animated_player.py",
                    "tools/patch_t3dm_stream_paths.py", "tools/asset_toolchain.lock.json",
                    "tools/build_player_skin_sidecar.py", "tools/build_player_animation_sidecar.py",
                ])
            else:
                dependencies.extend(["tools/glb_assets.py", "tools/blender/prepare_static_player.py", "tools/asset_toolchain.lock.json"])
        elif asset.get("kind") == "animation_manifest":
            dependencies.append("tools/generate_runtime_asset_metadata.py")
        elif asset.get("kind") == "audio_manifest":
            dependencies.append("tools/generate_audio_events.py")
        lines.extend(
            [
                f"{generated}: {' '.join(dependencies)}",
                *([
                    f"\t$(ASSET_PYTHON) tools/prepare_n64_model.py --manifest assets/og_converted/asset_manifest.json --asset-id {asset_id} --out-dir build/n64-prepared"
                ] if asset.get("kind") == "model" and asset.get("profile") in ("static_player", "animated_model") else []),
                f"\t$(ASSET_PYTHON) tools/convert_og_assets.py --manifest assets/og_converted/asset_manifest.json --asset-id {asset_id} --strict",
                "",
                f"{package_path}: {generated} | filesystem/mdl filesystem/tex filesystem/fnt filesystem/audio",
                f"\tmkdir -p $(@D)",
                f"\tcp $< $@",
                "",
            ]
        )
    output_path.write_text("\n".join(lines), encoding="utf-8")


def _main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default="assets/og_converted/asset_manifest.json")
    parser.add_argument("--write-make", metavar="PATH")
    parser.add_argument("--check", action="store_true", help="validate only")
    args = parser.parse_args(argv)
    try:
        manifest = load_manifest(args.manifest)
        diagnostics = validate_manifest(manifest, check_outputs=not bool(args.write_make))
        if diagnostics:
            for diagnostic in diagnostics:
                print(f"asset-manifest: {diagnostic}", file=sys.stderr)
            return 1
        if args.write_make:
            write_make_include(manifest, args.write_make)
        if args.check or not args.write_make:
            print(f"asset-manifest: valid ({len(manifest.get('assets', []))} assets)")
        return 0
    except (OSError, ValueError) as exc:
        print(f"asset-manifest: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(_main(sys.argv[1:]))
