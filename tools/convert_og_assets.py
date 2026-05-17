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
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
from PIL import Image

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

# N64 toolchain binaries (assumed on PATH, or set N64_INST env var)
N64_INST = Path(os.environ.get("N64_INST", "/tmp/n64-toolchain-root/opt/libdragon"))
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


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a command, print output, raise on error."""
    print(f"  → {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        print(f"ERROR (rc={result.returncode}):")
        print(result.stderr)
        raise subprocess.CalledProcessError(result.returncode, cmd)
    if result.stdout:
        print(result.stdout)
    return result


def convert_texture(src: Path, dst_dir: Path, fmt: str = "RGBA16", compress: int = 1) -> Path:
    """Convert a PNG/JPG texture to N64 .sprite format."""
    dst = dst_dir / f"{src.stem}.sprite"
    run([
        tool("mksprite"),
        "-f", fmt,
        "-c", str(compress),
        "-o", str(dst_dir),
        str(src),
    ])
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


def convert_model(src: Path, dst_dir: Path, base_scale: int = 64) -> Path:
    """Convert a GLB model to N64 .t3dm format.

    Uses --ignore-materials because the OG GLBs were not exported via Fast64
    and lack the custom F3D material properties gltf_to_t3d expects.
    Uses --ignore-transforms to avoid armature/skin transform errors.
    """
    dst = dst_dir / f"{src.stem}.t3dm"
    run([
        tool("gltf_to_t3d"),
        str(src),
        str(dst),
        f"--base-scale={base_scale}",
        "--ignore-materials",
        "--ignore-transforms",
        "--verbose",
    ])
    return dst


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

def main() -> int:
    print("=" * 60)
    print("Celeste64 OG → N64 Asset Conversion")
    print("=" * 60)
    print(f"Source : {OG_CONTENT}")
    print(f"Output : {OUT_DIR}")
    print(f"N64_INST: {N64_INST}")

    if not BIN_DIR.exists():
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
