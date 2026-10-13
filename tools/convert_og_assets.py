#!/usr/bin/env python3
"""
Asset conversion script for Celeste64 OG → N64 format.

Converts all convertible assets from Celeste64-og/Content/ to N64-compatible
formats under assets/og_converted/.

Usage:
    python3 tools/convert_og_assets.py

Requires N64 toolchain on PATH (mksprite, gltf_to_t3d, mkfont, audioconv64).
"""

import os
import sys
import subprocess
import shutil
import tempfile
import argparse
import struct
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
from PIL import Image

from asset_manifest import load_manifest, validate_manifest, texture_profile_for_asset
from glb_assets import extract_embedded_image

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
OG_CONTENT = REPO_ROOT / "Celeste64-og" / "Content"
OUT_DIR = REPO_ROOT / "assets" / "og_converted"
FIRST_ROOM_LEVEL_MATERIALS = {
    "rock_1",
    "snow_1",
    "rock_2",
    "metal_floor_1",
    "floor_dirty_concrete",
    "TB_empty",
}
FIRST_ROOM_TEXTURE_SIZE = (32, 32)

# N64 toolchain binaries (assumed on PATH, or set N64_INST env var).
# Default is the space-free access symlink (libdragon tools are not space-safe).
N64_INST = Path(os.environ.get("N64_INST", os.path.expanduser("~/.n64-bootstrap/opt/libdragon")))
BIN_DIR = N64_INST / "bin"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def tool(name: str) -> str:
    """Return full path to an N64 toolchain binary."""
    p = BIN_DIR / name
    if p.exists():
        return str(p)
    # fallback to PATH
    return name


def toolchain_available() -> bool:
    return BIN_DIR.exists() or any(shutil.which(name) for name in ("mksprite", "gltf_to_t3d", "mkfont", "audioconv64"))


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a command, print output, raise on error."""
    print(f"  → {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    except OSError as exc:
        # A missing/unrunnable binary raises OSError (not CalledProcessError).
        # Normalize so the batch callers' except clauses still report per file.
        raise subprocess.CalledProcessError(127, cmd, None, str(exc)) from exc
    if result.returncode != 0:
        print(f"ERROR (rc={result.returncode}):")
        print(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, cmd)
    if result.stdout:
        print(result.stdout)
    return result


def convert_texture(src: Path, dst_dir: Path, fmt: str = "RGBA16", compress: int = 1,
                    mipmap: str = "NONE") -> Path:
    """Convert a PNG/JPG texture to N64 .sprite format."""
    dst = dst_dir / f"{src.stem}.sprite"
    command = [
        tool("mksprite"),
        "-f", fmt,
        "-c", str(compress),
    ]
    if mipmap != "NONE":
        command.extend(["--mipmap", mipmap])
    command.extend([
        "-o", str(dst_dir),
        str(src),
    ])
    run(command)
    return dst


def convert_first_room_texture(src: Path, dst_dir: Path) -> Path:
    """Convert one first-room material through the N64-safe 32x32 path."""
    if src.stem not in FIRST_ROOM_LEVEL_MATERIALS:
        return convert_texture(src, dst_dir, fmt="RGBA16", compress=1)

    with tempfile.TemporaryDirectory(prefix="madeline-level-tex-") as tmp:
        conditioned_src = Path(tmp) / src.name
        with Image.open(src) as image:
            image.resize(FIRST_ROOM_TEXTURE_SIZE, Image.Resampling.NEAREST).save(conditioned_src)
        # Keep first-room materials uncompressed so tooling can inspect the sprite
        # header directly before they ever reach the ROM.
        return convert_texture(conditioned_src, dst_dir, fmt="RGBA16", compress=0)


def resize_texture_image(image: Image.Image, output_size: tuple[int, int],
                         profile: dict) -> Image.Image:
    """Resize a conditioned texture using its manifest-declared filter."""
    filters = {
        "nearest": Image.Resampling.NEAREST,
        "box": Image.Resampling.BOX,
        "bilinear": Image.Resampling.BILINEAR,
        "bicubic": Image.Resampling.BICUBIC,
        "lanczos": Image.Resampling.LANCZOS,
    }
    filter_name = str(profile.get("resize_filter", "nearest")).lower()
    try:
        resampling = filters[filter_name]
    except KeyError as exc:
        raise ValueError(f"unsupported texture resize filter: {filter_name}") from exc
    return image.resize(output_size, resample=resampling)


def convert_profiled_texture(src: Path, dst_dir: Path, profile: dict) -> Path:
    """Condition source image to profile dimensions before mksprite."""
    profile_name = profile.get("name", "")
    if profile_name not in ("world_repeat_rgba16", "world_decal_rgba16"):
        fmt = profile.get("format", "RGBA16")
        return convert_texture(src, dst_dir, fmt=fmt, compress=1)
    output_size = (int(profile["output_width"]), int(profile["output_height"]))
    with tempfile.TemporaryDirectory(prefix="madeline-profile-tex-") as tmp:
        conditioned_src = Path(tmp) / src.name
        with Image.open(src) as image:
            image = image.convert("RGBA")
            resize_texture_image(image, output_size, profile).save(conditioned_src)
        return convert_texture(
            conditioned_src, dst_dir, fmt="RGBA16", compress=0,
            mipmap=str(profile.get("mipmap", "NONE")),
        )


def _player_texture_png(src: Path, tmp_dir: Path, profile: dict) -> tuple[Path, float, int]:
    """Extract and condition canonical embedded player atlas."""
    extracted = tmp_dir / "player-source.png"
    extract_embedded_image(src, 0, extracted)
    with Image.open(extracted) as image:
        source = image.convert("RGBA")
        expected = (int(profile["source_width"]), int(profile["source_height"]))
        if source.size != expected:
            raise ValueError(f"player atlas source dimensions changed: {source.size} != {expected}")
        if source.getchannel("A").getextrema() != (255, 255):
            raise ValueError("player atlas must be opaque")
        resized = source.resize((64, 64), Image.Resampling.NEAREST).convert("RGB")
        quantized = resized.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        conditioned = quantized.convert("RGB")
        pixels = list(zip(resized.getdata(), conditioned.getdata()))
        mse = sum(sum((float(a) - float(b)) ** 2 for a, b in zip(left, right)) / 3.0 for left, right in pixels) / len(pixels)
        colors = len(quantized.getcolors(maxcolors=256) or [])
        if colors > 16:
            raise ValueError(f"player atlas palette overflow: {colors} colors")
        if mse > float(profile.get("mse_rgb8_max", 4.0)):
            raise ValueError(f"player atlas MSE {mse:.4f} exceeds {profile.get('mse_rgb8_max', 4.0)}")
        output = tmp_dir / "player.png"
        conditioned.save(output, format="PNG", optimize=False)
    return output, mse, colors


def _validate_ci4_sprite(path: Path, expected_size: tuple[int, int]) -> dict[str, int]:
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != struct.pack(">HH", *expected_size):
        raise ValueError(f"player sprite dimensions are not {expected_size[0]}x{expected_size[1]}")
    if data[5] & 0x1F != 8:
        raise ValueError(f"player sprite is not CI4 (flags=0x{data[5]:02x})")
    texel_bytes = expected_size[0] * expected_size[1] // 2
    if len(data) < 8 + texel_bytes:
        raise ValueError("player sprite texel payload truncated")
    ext = 8 + texel_bytes
    if len(data) < ext + 8 or struct.unpack_from(">HH", data, ext)[0] != 128:
        raise ValueError("player sprite extension missing")
    palette_offset = struct.unpack_from(">I", data, ext + 4)[0]
    if palette_offset <= ext or palette_offset + 32 > len(data):
        raise ValueError("player sprite palette is missing or oversized")
    if len(data) - palette_offset > 32:
        raise ValueError("player sprite palette exceeds 32 bytes")
    return {"width": expected_size[0], "height": expected_size[1], "texel_bytes": texel_bytes, "palette_bytes": len(data) - palette_offset}


def convert_player_texture(src: Path, dst_dir: Path, profile: dict) -> Path:
    dst_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="madeline-player-atlas-") as tmp:
        conditioned, mse, colors = _player_texture_png(src, Path(tmp), profile)
        output = convert_texture(conditioned, dst_dir, fmt="CI4", compress=0)
    budget = _validate_ci4_sprite(output, (64, 64))
    print(f"  player atlas: MSE={mse:.4f}, colors={colors}, payload={budget['texel_bytes']} palette={budget['palette_bytes']}")
    return output


def convert_model(src: Path, dst_dir: Path, base_scale: int = 64, preserve_materials: bool = False) -> Path:
    """Convert a GLB model to N64 .t3dm format.

    Static player output carries Fast64 material records; legacy OG models keep
    dummy-material conversion path.
    """
    dst = dst_dir / f"{src.stem}.t3dm"
    command = [
        tool("gltf_to_t3d"),
        str(src),
        str(dst),
        f"--base-scale={base_scale}",
        "--asset-path=assets",
        "--verbose",
    ]
    if not preserve_materials:
        command[4:4] = ["--ignore-materials", "--ignore-transforms"]
    run(command)
    return dst


def _inspect_player_t3dm(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    if data[:3] != b"T3M" or len(data) < 48 or data[3] != 4:
        raise ValueError(f"{path}: invalid T3DM")
    chunk_count = struct.unpack_from(">I", data, 4)[0]
    vertex_count = struct.unpack_from(">H", data, 8)[0]
    material_count = 0
    vertex_chunks = []
    for index in range(chunk_count):
        word = struct.unpack_from(">I", data, 44 + index * 4)[0]
        if word >> 24 == ord("V"):
            vertex_chunks.append(word & 0x00FFFFFF)
        if word >> 24 != ord("M"):
            continue
        material_count += 1
        offset = word & 0x00FFFFFF
        if offset + 140 > len(data):
            raise ValueError(f"{path}: material chunk truncated")
        flags = struct.unpack_from(">I", data, offset + 28)[0]
        combiner = struct.unpack_from(">Q", data, offset)[0]
        tex_ref = struct.unpack_from(">I", data, offset + 52)[0]
        tex_path = struct.unpack_from(">I", data, offset + 56)[0]
        tex_width, tex_height = struct.unpack_from(">HH", data, offset + 68)
        if flags & 0x7 != 0x5:
            raise ValueError(f"{path}: material flags 0x{flags:x} must be shaded/depth only")
        if combiner != 0x0000000000020904:
            raise ValueError(f"{path}: material combiner is not RDPQ_COMBINER_SHADE")
        if tex_ref != 0 or tex_path != 0 or (tex_width, tex_height) != (0, 0):
            raise ValueError(f"{path}: player material unexpectedly references a texture")
    if material_count < 1:
        raise ValueError(f"{path}: no material chunks")
    if vertex_count <= 0 or len(vertex_chunks) != 1:
        raise ValueError(f"{path}: missing shared vertex/color chunk")
    color_payload_end = vertex_chunks[0] + ((vertex_count - 1) // 2) * 32 + 20
    if color_payload_end > len(data):
        raise ValueError(f"{path}: vertex/color payload truncated")
    colors = {
        data[vertex_chunks[0] + (index // 2) * 32 + 16 + (index % 2) * 4:
             vertex_chunks[0] + (index // 2) * 32 + 20 + (index % 2) * 4]
        for index in range(vertex_count)
    }
    if len(colors) < 2 or any(color[3] != 255 for color in colors):
        raise ValueError(f"{path}: vertex colors are missing, flat white, or non-opaque")
    if path.stat().st_size > 128 * 1024:
        raise ValueError(f"{path}: T3DM exceeds 128 KiB")
    return {"material_count": material_count, "vertex_count": vertex_count,
            "color_count": len(colors), "bytes": path.stat().st_size}


def convert_font(src: Path, dst_dir: Path, size: int = 16) -> Path:
    """Convert a TTF/OTF font to N64 .font64 format."""
    dst = dst_dir / f"{src.stem}.font64"
    run([
        tool("mkfont"),
        "-s", str(size),
        "-c", "1",
        "-o", str(dst_dir),
        str(src),
    ])
    return dst


def convert_audio_wav(src: Path, dst_dir: Path, compress: int = 1, resample: int | None = None) -> Path:
    """Convert a WAV/MP3 to N64 .wav64 format."""
    dst = dst_dir / f"{src.stem}.wav64"
    cmd = [
        tool("audioconv64"),
        "--wav-compress", str(compress),
        "-o", str(dst_dir),
        str(src),
    ]
    if resample is not None:
        cmd.extend(["--wav-resample", str(resample)])
    run(cmd)
    return dst


def convert_manifest_asset(manifest_path: Path, asset_id: str, strict: bool = False) -> list[Path]:
    """Convert one active manifest entry and verify every declared output."""
    manifest = load_manifest(manifest_path)
    diagnostics = validate_manifest(manifest, check_outputs=False)
    if diagnostics:
        raise ValueError("manifest validation failed:\n" + "\n".join(diagnostics))
    asset = next((item for item in manifest["assets"] if item.get("id") == asset_id), None)
    if asset is None:
        raise ValueError(f"unknown asset id: {asset_id}")
    if asset.get("status") != "active" or not asset.get("package"):
        raise ValueError(f"asset {asset_id} is not an active packaged entry")
    source = REPO_ROOT / asset["source"]
    results: list[Path] = []
    kind = asset.get("kind")
    outputs = asset["outputs"]
    if kind == "model":
        primary = next((item for item in outputs if item.get("format") == "t3dm"), None)
        if primary is None:
            raise ValueError(f"asset {asset_id}: model has no t3dm output")
        destination = REPO_ROOT / primary["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        prepared = REPO_ROOT / "build" / "n64-prepared" / f"{asset_id}.glb"
        if asset.get("profile") in ("static_player", "animated_model") and not prepared.is_file():
            raise RuntimeError(
                f"asset {asset_id}: prepared {asset.get('profile')} GLB missing; "
                "run prepare_n64_model first")
        use_prepared = asset.get("profile") in ("static_player", "animated_model") and prepared.is_file()
        model_source = prepared if use_prepared else source
        produced = convert_model(
            model_source,
            destination.parent,
            int(asset.get("transform", {}).get("base_scale", 64)),
            preserve_materials=asset.get("profile") in ("static_player", "animated_model"),
        )
        if produced != destination:
            produced.replace(destination)
        if asset.get("transform", {}).get("material_patch"):
            from patch_t3dm_materials import patch_t3dm
            patch_t3dm(str(destination), str(destination))
        if asset.get("profile") == "animated_model":
            subprocess.run([sys.executable, str(REPO_ROOT / "tools/patch_t3dm_stream_paths.py"), str(destination)], check=True)
            skin_output = next((item for item in outputs if item.get("format") == "weighted_skin_sidecar"), None)
            if skin_output is None:
                raise ValueError(f"asset {asset_id}: animated model has no weighted-skin sidecar output")
            subprocess.run([
                sys.executable, str(REPO_ROOT / "tools/build_player_skin_sidecar.py"),
                "--glb", str(prepared), "--t3dm", str(destination),
                "--out", str(REPO_ROOT / skin_output["path"]),
                "--base-scale", str(asset.get("transform", {}).get("base_scale", 64)),
            ], check=True)
            curve_output = next((item for item in outputs if item.get("format") == "animation_curve_sidecar"), None)
            if curve_output is None:
                raise ValueError(f"asset {asset_id}: animated model has no source curve sidecar output")
            subprocess.run([
                sys.executable, str(REPO_ROOT / "tools/build_player_animation_sidecar.py"),
                "--glb", str(prepared), "--t3dm", str(destination),
                "--out", str(REPO_ROOT / curve_output["path"]),
                "--base-scale", str(asset.get("transform", {}).get("base_scale", 64)),
            ], check=True)
        if asset.get("profile") in ("static_player", "animated_model"):
            _inspect_player_t3dm(destination)
    elif kind == "animation_manifest":
        destination = REPO_ROOT / outputs[0]["path"]
        subprocess.run([
            sys.executable, str(REPO_ROOT / "tools/generate_runtime_asset_metadata.py"),
            "--manifest", str(manifest_path), "--asset-id", asset_id, "--out", str(destination),
        ], check=True)
    elif kind == "audio_manifest":
        destination = REPO_ROOT / outputs[0]["path"]
        subprocess.run([
            sys.executable, str(REPO_ROOT / "tools/generate_audio_events.py"),
            "--manifest", str(manifest_path), "--out", str(destination),
        ], check=True)
    else:
        destination = REPO_ROOT / outputs[0]["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        if kind == "texture":
            profile = texture_profile_for_asset(asset, REPO_ROOT)
            if asset.get("profile") == "model_atlas_ci4_64":
                produced = convert_player_texture(source, destination.parent, profile)
            else:
                produced = convert_profiled_texture(source, destination.parent, profile)
        elif kind == "font":
            produced = convert_font(source, destination.parent)
        elif kind == "audio":
            produced = convert_audio_wav(source, destination.parent)
        else:
            raise ValueError(f"asset {asset_id}: unsupported conversion kind {kind!r}")
        if produced != destination:
            produced.replace(destination)

    for output in outputs:
        destination = REPO_ROOT / output["path"]
        if not destination.is_file() or destination.stat().st_size == 0:
            raise RuntimeError(f"asset {asset_id}: required output missing or empty: {destination}")
        results.append(destination)
    return results


# ---------------------------------------------------------------------------
# Conversion batches
# ---------------------------------------------------------------------------

def convert_models() -> list[Path]:
    """Convert all .glb models."""
    src_dir = OG_CONTENT / "Models"
    dst_dir = OUT_DIR / "models"
    dst_dir.mkdir(parents=True, exist_ok=True)

    glbs = sorted(src_dir.glob("*.glb"))
    print(f"\n[Models] {len(glbs)} GLB files → {dst_dir}")

    results = []
    for glb in glbs:
        try:
            results.append(convert_model(glb, dst_dir))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {glb.name}")
    return results


def convert_textures() -> list[Path]:
    """Convert all .png textures (root level)."""
    src_dir = OG_CONTENT / "Textures"
    dst_dir = OUT_DIR / "textures"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.glob("*.png"))
    print(f"\n[Textures] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            results.append(convert_first_room_texture(png, dst_dir))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_sprites() -> list[Path]:
    """Convert all .png sprites."""
    src_dir = OG_CONTENT / "Sprites"
    dst_dir = OUT_DIR / "sprites"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.glob("*.png"))
    print(f"\n[Sprites] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            # Sprites often work well as CI8 (palette) for smaller size
            results.append(convert_texture(png, dst_dir, fmt="CI8", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_faces() -> list[Path]:
    """Convert all face textures (character dialogue portraits)."""
    src_dir = OG_CONTENT / "Faces"
    dst_dir = OUT_DIR / "faces"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.rglob("*.png"))
    print(f"\n[Faces] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            # Keep subdirectory structure
            sub_dst = dst_dir / png.parent.relative_to(src_dir)
            sub_dst.mkdir(parents=True, exist_ok=True)
            results.append(convert_texture(png, sub_dst, fmt="RGBA16", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_fonts() -> list[Path]:
    """Convert the game font."""
    src_dir = OG_CONTENT / "Fonts"
    dst_dir = OUT_DIR / "fonts"
    dst_dir.mkdir(parents=True, exist_ok=True)

    fonts = list(src_dir.glob("*.otf")) + list(src_dir.glob("*.ttf"))
    print(f"\n[Fonts] {len(fonts)} font files → {dst_dir}")

    results = []
    for font in fonts:
        try:
            results.append(convert_font(font, dst_dir, size=16))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {font.name}")
    return results


def convert_control_sprites() -> list[Path]:
    """Convert controller button sprites."""
    src_dir = OG_CONTENT / "Sprites" / "Controls"
    dst_dir = OUT_DIR / "sprites" / "controls"
    dst_dir.mkdir(parents=True, exist_ok=True)

    # There are subdirs per platform; grab all PNGs recursively
    pngs = sorted(src_dir.rglob("*.png"))
    print(f"\n[Control Sprites] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            sub_dst = dst_dir / png.parent.relative_to(src_dir)
            sub_dst.mkdir(parents=True, exist_ok=True)
            results.append(convert_texture(png, sub_dst, fmt="CI8", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_overworld_textures() -> list[Path]:
    """Convert overworld overlay textures."""
    src_dir = OG_CONTENT / "Textures" / "Overworld"
    dst_dir = OUT_DIR / "textures" / "overworld"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.glob("*.png"))
    print(f"\n[Overworld Textures] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            results.append(convert_texture(png, dst_dir, fmt="RGBA16", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_postcards() -> list[Path]:
    """Convert postcard images."""
    src_dir = OG_CONTENT / "Textures" / "Postcards"
    dst_dir = OUT_DIR / "textures" / "postcards"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.glob("*.png"))
    print(f"\n[Postcards] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            results.append(convert_texture(png, dst_dir, fmt="RGBA16", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def convert_skyboxes() -> list[Path]:
    """Convert skybox textures."""
    src_dir = OG_CONTENT / "Textures" / "Skyboxes"
    dst_dir = OUT_DIR / "textures" / "skyboxes"
    dst_dir.mkdir(parents=True, exist_ok=True)

    pngs = sorted(src_dir.glob("*.png"))
    print(f"\n[Skyboxes] {len(pngs)} PNG files → {dst_dir}")

    results = []
    for png in pngs:
        try:
            results.append(convert_texture(png, dst_dir, fmt="RGBA16", compress=1))
        except subprocess.CalledProcessError:
            print(f"  FAILED: {png.name}")
    return results


def copy_maps() -> list[Path]:
    """Copy .map level files as-is (Trenchbroom format, parsed at runtime)."""
    src_dir = OG_CONTENT / "Maps"
    dst_dir = OUT_DIR / "maps"
    dst_dir.mkdir(parents=True, exist_ok=True)

    maps = sorted(src_dir.glob("*.map"))
    print(f"\n[Maps] {len(maps)} .map files → {dst_dir}")

    results = []
    for m in maps:
        dst = dst_dir / m.name
        shutil.copy2(m, dst)
        results.append(dst)
        print(f"  copied {m.name}")
    return results


def copy_json_configs() -> list[Path]:
    """Copy JSON config files as-is."""
    configs = [
        OG_CONTENT / "Levels.json",
        OG_CONTENT / "GameConfig.cfg",
    ]
    dst_dir = OUT_DIR / "config"
    dst_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n[Config] {len(configs)} JSON/CFG files → {dst_dir}")

    results = []
    for cfg in configs:
        if cfg.exists():
            dst = dst_dir / cfg.name
            shutil.copy2(cfg, dst)
            results.append(dst)
            print(f"  copied {cfg.name}")
    return results


def copy_fgd() -> list[Path]:
    """Copy Trenchbroom entity definition file."""
    src = OG_CONTENT / "Celeste64.fgd"
    dst_dir = OUT_DIR / "config"
    dst_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n[FGD] Copying entity definitions → {dst_dir}")

    results = []
    if src.exists():
        dst = dst_dir / src.name
        shutil.copy2(src, dst)
        results.append(dst)
        print(f"  copied {src.name}")
    return results


# ---------------------------------------------------------------------------
# Audio note
# ---------------------------------------------------------------------------

def note_audio() -> None:
    """
    The OG audio is stored in FMOD Studio .bank files (Master.bank, music.bank,
    sfx.bank). These are proprietary RIFF/FEV containers and cannot be batch-
    converted without FMOD Studio or a compatible extraction tool.

    If you have access to the FMOD Studio project (or can extract the banks
    with a tool like fsb_extractor / fmod_bank_tool), place the resulting
    WAV/MP3 files under:

        assets/og_converted/audio/sfx/
        assets/og_converted/audio/music/

    Then re-run this script or manually run:

        audioconv64 --wav-compress 1 -o assets/og_converted/audio/sfx/   *.wav
        audioconv64 --wav-compress 3 --wav-resample 22050 -o assets/og_converted/audio/music/ *.wav
    """
    print("\n[Audio] SKIPPED — FMOD .bank files require external extraction.")
    print("  See note in convert_og_assets.py::note_audio() for details.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, help="validate and convert manifest entry")
    parser.add_argument("--asset-id", help="manifest asset id to convert")
    parser.add_argument("--strict", action="store_true", help="fail on any required conversion error")
    args = parser.parse_args(argv)
    if args.manifest or args.asset_id:
        if not args.manifest or not args.asset_id:
            parser.error("--manifest and --asset-id must be supplied together")
        try:
            outputs = convert_manifest_asset(args.manifest, args.asset_id, strict=args.strict)
        except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
            print(f"asset conversion failed: {exc}", file=sys.stderr)
            return 1
        for output in outputs:
            print(f"converted {output}")
        return 0

    print("=" * 60)
    print("Celeste64 OG → N64 Asset Conversion")
    print("=" * 60)
    print(f"Source : {OG_CONTENT}")
    print(f"Output : {OUT_DIR}")
    print(f"N64_INST: {N64_INST}")

    if not toolchain_available():
        print(f"\nWARNING: N64 toolchain not found at {BIN_DIR}")
        print("Set N64_INST env var or ensure tools are on PATH.")
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Run all conversions
    all_results = {}
    all_results["models"] = convert_models()
    all_results["textures"] = convert_textures()
    all_results["sprites"] = convert_sprites()
    all_results["faces"] = convert_faces()
    all_results["fonts"] = convert_fonts()
    all_results["control_sprites"] = convert_control_sprites()
    all_results["overworld"] = convert_overworld_textures()
    all_results["postcards"] = convert_postcards()
    all_results["skyboxes"] = convert_skyboxes()
    all_results["maps"] = copy_maps()
    all_results["configs"] = copy_json_configs()
    all_results["fgd"] = copy_fgd()
    note_audio()

    # Summary
    print("\n" + "=" * 60)
    print("Conversion Summary")
    print("=" * 60)
    total = 0
    for category, files in all_results.items():
        count = len(files)
        total += count
        print(f"  {category:20s}: {count:3d} files")
    print(f"  {'TOTAL':20s}: {total:3d} files")
    print(f"\nOutput directory: {OUT_DIR}")
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
