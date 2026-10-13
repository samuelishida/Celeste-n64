#!/usr/bin/env python3
"""Guard Tiny3D draw-state snapshot against libdragon sprite LOD undercount."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src/user/gameplay/render/textured_room_renderer.cpp"


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    upload = re.compile(
        r"rdpq_sprite_upload\(TILE0, sprite, TextureParms\(profile\)\);\s*"
        r"(?://[^\n]*\n\s*)*"
        r"const int mip_level_count = sprite_get_lod_count\(sprite\);\s*"
        r"rdpq_mode_mipmap\(\s*"
        r"mip_level_count > 1 \? MIPMAP_INTERPOLATE : MIPMAP_NONE,\s*"
        r"mip_level_count > 1 \? mip_level_count : 0\);\s*"
        r"t3d_state_set_drawflags\(draw_flags\);"
    )
    matches = list(upload.finditer(source))
    assert len(matches) == 2, (
        "both textured draw paths must reapply mip state using total sprite LOD count "
        "before Tiny3D snapshots draw flags"
    )
    assert source.count("rdpq_sprite_upload(TILE0, sprite, TextureParms(profile));") == 2
    print("mipmap_upload_contract: PASS (both paths use all sprite levels)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
