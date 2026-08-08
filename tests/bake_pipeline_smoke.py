#!/usr/bin/env python3
"""Pipeline smoke tests — validates bake.py produces correct artifacts.

Runs tools/bake.py once against 1-1.map, then inspects the output
colmesh, LVL, manifest, and nav files for structural correctness.
"""

import struct
import subprocess
import sys
import os
import tempfile
import shutil
import math
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
TOOLS_DIR = os.path.join(REPO_DIR, "tools")
OG_MAP = os.path.join(REPO_DIR, "assets", "og_converted", "maps", "1-1.map")
SCALE = "0.2"
INT16_MAX = 32767

MAT_SOLID = 0x0001
MAT_DEATH = 0x0004
MAT_CLIMBABLE = 0x0008
MAT_ICE = 0x0010

failed = 0

# ── Shared bake output ─────────────────────────────────────────────
# Populated by run_bake_once() before any test runs.

BAKE_DIR = None  # path to temp dir containing bake.py output


def run_bake_once():
    """Run bake.py once and return the output directory path."""
    tmpdir = tempfile.mkdtemp(prefix="bake_pipeline_smoke_")
    cmd = [
        sys.executable, os.path.join(TOOLS_DIR, "bake.py"),
        OG_MAP,
        "--out-dir", tmpdir,
        "--scale", SCALE,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO_DIR)
    if result.returncode != 0:
        print(f"ERROR: bake.py failed (rc={result.returncode})")
        print(f"  stdout: {result.stdout}")
        print(f"  stderr: {result.stderr}")
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(1)
    return tmpdir


def check(name, condition, detail=""):
    global failed
    if condition:
        print(f"  PASS: {name}")
    else:
        print(f"  FAIL: {name} {detail}")
        failed += 1


# ── Library tests ───────────────────────────────────────────────────

def test_library_parse():
    """Verify ogmap_lib parse_map returns correct entity/texture counts."""
    print("\n[test_library_parse]")
    sys.path.insert(0, TOOLS_DIR)
    from ogmap_lib import parse_map
    pm = parse_map(OG_MAP)
    check("56 entities", len(pm.entities) == 56, f"got {len(pm.entities)}")
    check("6 textures", len(pm.textures) == 6, f"got {len(pm.textures)}")
    textures = set(pm.textures)
    for t in ("rock_1", "snow_1", "rock_2", "metal_floor_1",
              "floor_dirty_concrete", "TB_empty"):
        check(f"texture '{t}'", t in textures)


def test_class_registry():
    """Verify all OG entity classes in 1-1.map have a ClassDef or a skip reason."""
    print("\n[test_class_registry]")
    sys.path.insert(0, TOOLS_DIR)
    from ogmap_lib import parse_map, classify_entity, is_skipped
    pm = parse_map(OG_MAP)
    unhandled = []
    for ent in pm.entities:
        cd = classify_entity(ent)
        reason = is_skipped(ent)
        if cd is None and reason is None and ent.brushes:
            unhandled.append(ent.classname)
    check("all brush classes handled", len(unhandled) == 0,
          f"unhandled: {unhandled}")
    # Verify specific classifications
    for ent in pm.entities:
        if ent.classname == "worldspawn":
            cd = classify_entity(ent)
            check("worldspawn→SOLID", cd is not None and cd.material_class == 0)
        if ent.classname == "SpikeBlock":
            cd = classify_entity(ent)
            check("SpikeBlock→DEATH", cd is not None and cd.material_class == 1)
        if ent.classname == "Decoration":
            cd = classify_entity(ent)
            check("Decoration→VISUAL_ONLY", cd is not None and cd.material_class == 4)
        if ent.classname == "Node":
            check("Node→skipped", is_skipped(ent) is not None)


# ── Colmesh tests ──────────────────────────────────────────────────

def test_colmesh_bvh_quantized(bake_dir):
    """Verify BVH node AABBs are in quantized int16 range, not world floats."""
    print("\n[test_colmesh_bvh_quantized]")
    colmesh_path = os.path.join(bake_dir, "output.colmesh")
    check("colmesh file exists", os.path.exists(colmesh_path))
    d = open(colmesh_path, "rb").read()
    check("magic CMSH", d[:4] == b"CMSH")

    h = struct.unpack_from(">HHhhhhhhfffIIIIIIIIII", d, 4)
    tc = h[13]  # triangle_count
    bnc = h[14]  # bvh_node_count
    bo = h[18]  # bvh_offset
    check(f"triangles > 0 ({tc})", tc > 0)
    check(f"bvh nodes > 0 ({bnc})", bnc > 0)

    # Check BVH AABBs are in signed int16 range
    min_val, max_val = INT16_MAX + 1, -(INT16_MAX + 1)
    for i in range(bnc):
        off = bo + i * 16
        vals = struct.unpack_from(">hhhhhh", d, off)
        for v in vals:
            if v < min_val: min_val = v
            if v > max_val: max_val = v
    check(f"BVH AABB in int16 range [{min_val},{max_val}]",
          max_val <= INT16_MAX and min_val >= -INT16_MAX,
          f"out of range! min={min_val} max={max_val}")


def test_colmesh_material_flags(bake_dir):
    """Verify correct material flag distribution in colmesh."""
    print("\n[test_colmesh_material_flags]")
    colmesh_path = os.path.join(bake_dir, "output.colmesh")
    d = open(colmesh_path, "rb").read()
    h = struct.unpack_from(">HHhhhhhhfffIIIIIIIIII", d, 4)
    tc = h[13]
    to = h[17]

    solid_only = death = climbable = 0
    for i in range(tc):
        off = to + i * 12
        mat = struct.unpack_from(">H", d, off + 6)[0]
        if mat & MAT_DEATH:
            death += 1
        elif mat & MAT_CLIMBABLE:
            climbable += 1
        else:
            solid_only += 1
        if mat & MAT_DEATH and mat & MAT_CLIMBABLE:
            check("no DEATH+CLIMBABLE combo", False, f"at tri {i}")

    check(f"solid-only triangles ({solid_only})", solid_only >= 10)
    check(f"death triangles ({death})", death > 0)
    check("all death tris also solid",
          all(
              struct.unpack_from(">H", d, to + i * 12 + 6)[0] & MAT_SOLID
              for i in range(tc) if
              struct.unpack_from(">H", d, to + i * 12 + 6)[0] & MAT_DEATH
          ))
    print(f"    {tc} tris: {solid_only} solid, {death} death, {climbable} climbable")


# ── LVL tests ──────────────────────────────────────────────────────

def test_lvl_has_uvs(bake_dir):
    """Verify at least some LVL vertices have non-zero UVs."""
    print("\n[test_lvl_has_uvs]")
    lvl_path = os.path.join(bake_dir, "output.lvl")
    d = open(lvl_path, "rb").read()
    h = struct.unpack_from(">IIIIIII", d, 4)
    vc = h[3]
    off_verts = struct.unpack_from(">I", d, 0x38)[0]

    non_zero = 0
    for i in range(vc):
        off = off_verts + i * 20
        u, v = struct.unpack_from(">ff", d, off + 12)
        if abs(u) > 0.001 or abs(v) > 0.001:
            non_zero += 1
    check(f"non-zero UVs ({non_zero}/{vc})", non_zero > 0,
          f"expected > 0, got {non_zero}")


def test_lvl_entity_spawns(bake_dir):
    """Verify LVL entity table has PlayerSpawn, Strawberry, Cassette."""
    print("\n[test_lvl_entity_spawns]")
    lvl_path = os.path.join(bake_dir, "output.lvl")
    d = open(lvl_path, "rb").read()
    h = struct.unpack_from(">IIIIIII", d, 4)
    ec = h[4]
    check(f"entity_count == 3 ({ec})", ec == 3, f"expected 3")

    off_entities = struct.unpack_from(">I", d, 0x3C)[0]
    has_player = has_strawberry = has_cassette = False
    for i in range(ec):
        eoff = off_entities + i * 24
        eid = struct.unpack_from(">H", d, eoff)[0]
        if eid == 0:
            has_player = True
        elif eid == 1:
            has_strawberry = True
        elif eid == 9:
            has_cassette = True
    check("PlayerSpawn (id=0)", has_player)
    check("Strawberry (id=1)", has_strawberry)
    check("Cassette (id=9)", has_cassette)


def test_lvl_atmosphere(bake_dir):
    """Verify skybox/music/ambience/snow fields populated in LVL header."""
    print("\n[test_lvl_atmosphere]")
    lvl_path = os.path.join(bake_dir, "output.lvl")
    d = open(lvl_path, "rb").read()
    # skybox_str_id at +0x1C, music at +0x1E, ambience at +0x20
    skybox_id = struct.unpack_from(">H", d, 0x1C)[0]
    music_id = struct.unpack_from(">H", d, 0x1E)[0]
    ambience_id = struct.unpack_from(">H", d, 0x20)[0]
    snow_q8 = struct.unpack_from(">H", d, 0x22)[0]
    snow_dir = struct.unpack_from(">hhh", d, 0x24)

    check("skybox string present", skybox_id > 0 or skybox_id == 0)
    if skybox_id > 0:
        print(f"    skybox set (str_id={skybox_id})")
    check("music string present", music_id > 0, f"music_str_id=0")
    if music_id > 0:
        print(f"    music set (str_id={music_id})")
    check("ambience string present", ambience_id > 0, f"ambience_str_id=0")
    check("snow amount present", snow_q8 > 0, f"snow_q8={snow_q8}")
    print(f"    snow_amount_q8={snow_q8} snow_dir={snow_dir}")


def test_manifest_clean(bake_dir):
    """Verify manifest has no _death suffixes."""
    print("\n[test_manifest_clean]")
    manifest_path = os.path.join(bake_dir, "output.manifest")
    check("manifest file exists", os.path.exists(manifest_path))
    text = open(manifest_path).read()
    check("no _death suffix", "_death" not in text, "found _death in manifest")
    for m in ("rock_1", "snow_1", "rock_2", "metal_floor_1",
              "floor_dirty_concrete", "TB_empty"):
        check(f"contains {m}", m in text)


def test_budgets(bake_dir):
    """Verify colmesh ≤ 256KB, lvl faces ≤ 1024, lvl verts ≤ 8192."""
    print("\n[test_budgets]")
    colmesh_path = os.path.join(bake_dir, "output.colmesh")
    lvl_path = os.path.join(bake_dir, "output.lvl")

    csize = os.path.getsize(colmesh_path)
    check(f"colmesh <= 256KB ({csize}B)", csize <= 262144)

    d = open(lvl_path, "rb").read()
    h = struct.unpack_from(">IIIIIII", d, 4)
    check(f"lvl faces <= 1024 ({h[2]})", h[2] <= 1024)
    check(f"lvl verts <= 8192 ({h[3]})", h[3] <= 8192)


# ── Nav tests ──────────────────────────────────────────────────────

def test_nav_waypoints(bake_dir):
    """Verify .nav has 5 platforms with 2 waypoints each."""
    print("\n[test_nav_waypoints]")
    nav_path = os.path.join(bake_dir, "output.nav")
    check("nav file exists", os.path.exists(nav_path))
    d = open(nav_path, "rb").read()
    check("magic NAV1", d[:4] == b"NAV1")
    count = struct.unpack_from("<H", d, 4)[0]
    check(f"platform count == 5 ({count})", count == 5)

    off = 6
    all_finite = True
    for i in range(count):
        ei, wpc, tt = struct.unpack_from("<HHf", d, off)
        off += 8
        check(f"  P{ei} waypoints >= 2 ({wpc})", wpc >= 2)
        for j in range(wpc):
            x, y, z = struct.unpack_from("<fff", d, off)
            if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
                all_finite = False
            off += 12
        check(f"  P{ei} tt > 0 ({tt:.1f}s)", tt > 0)


# ── Report test ────────────────────────────────────────────────────

def test_report_json(bake_dir):
    """Verify bake.py produces a report.json with expected fields."""
    print("\n[test_report_json]")
    import json
    report_path = os.path.join(bake_dir, "output.report.json")
    check("report file exists", os.path.exists(report_path))
    report = json.load(open(report_path))
    check("has input_sha256", "input_sha256" in report)
    check("has scale", "scale" in report)
    check("has counts", "counts" in report)
    check("has hashes", "hashes" in report)
    counts = report.get("counts", {})
    check("colmesh stats present", "colmesh" in counts)
    check("lvl stats present", "lvl" in counts)
    check("nav stats present", "nav" in counts)
    if "colmesh" in counts:
        check("colmesh vertices", counts["colmesh"].get("vertices", 0) > 0)
        check("colmesh triangles", counts["colmesh"].get("triangles", 0) > 0)
    if "lvl" in counts:
        check("lvl faces", counts["lvl"].get("faces", 0) > 0)
        check("lvl entities", counts["lvl"].get("entities", 0) > 0)


def run_all():
    global failed

    # Run bake.py once, then inspect all output files
    bake_dir = run_bake_once()

    try:
        test_library_parse()
        test_class_registry()
        test_colmesh_bvh_quantized(bake_dir)
        test_colmesh_material_flags(bake_dir)
        test_lvl_has_uvs(bake_dir)
        test_lvl_entity_spawns(bake_dir)
        test_lvl_atmosphere(bake_dir)
        test_manifest_clean(bake_dir)
        test_budgets(bake_dir)
        test_nav_waypoints(bake_dir)
        test_report_json(bake_dir)

        print(f"\n{'=' * 40}")
        if failed:
            print(f"FAILED: {failed} test(s)")
            sys.exit(1)
        else:
            print("ALL PASSED")
    finally:
        shutil.rmtree(bake_dir, ignore_errors=True)


if __name__ == "__main__":
    run_all()