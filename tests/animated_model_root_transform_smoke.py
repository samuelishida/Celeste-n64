#!/usr/bin/env python3
"""Root-transform normalization must preserve skinned bind and clip poses."""

from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from glb_assets import read_glb, write_glb
from prepare_n64_model import _normalize_skin_root


def _fixture(path: Path) -> None:
    offset = 0.125
    values = struct.pack("<2f", 0.0, 1.0) + struct.pack(
        "<6f", 0.0, 1.0, 0.0, 0.5, 1.0, 0.0
    )
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(values)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 8},
            {"buffer": 0, "byteOffset": 8, "byteLength": 24},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2,
             "type": "SCALAR", "min": [0.0], "max": [1.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2,
             "type": "VEC3"},
        ],
        "nodes": [
            {"name": "Armature", "translation": [offset, 0.0, 0.0],
             "children": [1, 2]},
            {"name": "Master", "translation": [0.0, 1.0, 0.0],
             "children": [3]},
            {"name": "Mesh", "mesh": 0, "skin": 0},
            {"name": "Hips", "translation": [0.0, 2.0, 0.0]},
        ],
        "meshes": [{"primitives": []}],
        "skins": [{"joints": [1, 3]}],
        "animations": [{
            "name": "Idle",
            "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}],
        }],
    }
    # The fixture only exercises hierarchy normalization; the accessor's use
    # as inverseBindMatrices is intentionally irrelevant to that operation.
    write_glb(path, document, values)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="animated-root-transform-") as temp:
        path = Path(temp) / "root.glb"
        _fixture(path)
        report = _normalize_skin_root(path)
        assert report["changed"]
        document, binary = read_glb(path)
        armature = document["nodes"][0]
        master = document["nodes"][1]
        assert "translation" not in armature
        assert abs(master["translation"][0] - 0.125) < 1e-7, \
            "bind-pose root translation was dropped"
        sampler = document["animations"][0]["samplers"][0]
        accessor = document["accessors"][sampler["output"]]
        view = document["bufferViews"][accessor["bufferView"]]
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        keys = [struct.unpack_from("<3f", binary, start + index * 12)
                for index in range(accessor["count"])]
        assert abs(keys[0][0] - 0.125) < 1e-7, \
            "first animated root translation lost parent offset"
        assert abs(keys[1][0] - 0.625) < 1e-7, \
            "later animated root translation lost parent offset"
    print("animated model root-transform smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
