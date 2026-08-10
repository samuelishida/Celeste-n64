#!/usr/bin/env python3
"""Full-world class policy (Inc 2).

Moves the class mapping into an explicit table used by all writers. Separates
`collision_mode` from `render_mode` and explicitly labels dynamic OG classes
as static-proxy or actor-owned rather than silently treating them as
worldspawn.

Every source brush class has an explicit policy. Unknown brush classes are
reported and fail strict full-world baking; they are never silently dropped.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple, Any

# Collision modes.
COLLISION_SOLID = "solid"
COLLISION_ONEWAY = "oneway"
COLLISION_CLIMBABLE = "climbable"
COLLISION_NONE = "none"

# Render modes.
RENDER_STATIC = "static"     # included in LVL visual geometry
RENDER_ACTOR = "actor"       # actor-owned geometry (not baked into room mesh)
RENDER_NONE = "none"         # invisible (triggers, spawn points)

# Dynamic-class handling.
DYNAMIC_STATIC_PROXY = "static-proxy"   # static collision proxy for now
DYNAMIC_ACTOR_OWNED = "actor-owned"     # actor-owned collision (future)


class ClassPolicy:
    """Explicit policy for one source brush class."""

    __slots__ = (
        "classname", "collision_mode", "render_mode", "material_flags",
        "face_filter", "dynamic", "note",
    )

    def __init__(self, classname, collision_mode, render_mode, material_flags,
                 face_filter="none", dynamic=None, note=""):
        self.classname = classname
        self.collision_mode = collision_mode
        self.render_mode = render_mode
        self.material_flags = material_flags
        self.face_filter = face_filter      # "none" | "upward"
        self.dynamic = dynamic              # None | DYNAMIC_STATIC_PROXY | DYNAMIC_ACTOR_OWNED
        self.note = note

    def to_dict(self) -> Dict[str, Any]:
        return {
            "classname": self.classname,
            "collision_mode": self.collision_mode,
            "render_mode": self.render_mode,
            "material_flags": self.material_flags,
            "face_filter": self.face_filter,
            "dynamic": self.dynamic,
            "note": self.note,
        }


# Material flag bits (matching coll_mesh.hpp).
MAT_SOLID = 0x0001
MAT_ONEWAY = 0x0002
MAT_DEATH = 0x0004
MAT_CLIMBABLE = 0x0008


def _solid(classname, flags=MAT_SOLID, **kw):
    return ClassPolicy(classname=classname, collision_mode=COLLISION_SOLID,
                       render_mode=RENDER_STATIC, material_flags=flags, **kw)


def _visual(classname, **kw):
    return ClassPolicy(classname=classname, collision_mode=COLLISION_NONE,
                       render_mode=RENDER_STATIC, material_flags=0, **kw)


def _none(classname, **kw):
    return ClassPolicy(classname=classname, collision_mode=COLLISION_NONE,
                       render_mode=RENDER_NONE, material_flags=0, **kw)


# The full-world class policy table. Every brush-bearing class in 1.map must
# appear here (or in SKIPPED_CLASSES). Dynamic classes are labeled.
CLASS_POLICIES: Dict[str, ClassPolicy] = {
    "worldspawn": _solid("worldspawn"),
    "SpikeBlock": _solid("SpikeBlock", MAT_SOLID | MAT_DEATH, face_filter="upward"),
    # DeathBlock is an invisible kill volume: it contributes global collision
    # (death) but is NOT rendered, so it is not duplicated into dozens of
    # visual cells. It remains global collision/trigger metadata.
    "DeathBlock": ClassPolicy(
        classname="DeathBlock",
        collision_mode=COLLISION_SOLID,
        render_mode=RENDER_NONE,
        material_flags=MAT_SOLID | MAT_DEATH,
        face_filter="upward",
        note="invisible kill volume; collision-only, not rendered",
    ),
    "Decoration": _visual("Decoration"),
    "FloatingDecoration": _visual("FloatingDecoration"),
    "StaticProp": _visual("StaticProp"),
    # Dynamic classes: static collision proxy for this collision-first
    # milestone. Their dynamic behavior is called out in the report.
    "TrafficBlock": _solid("TrafficBlock", dynamic=DYNAMIC_STATIC_PROXY,
                           note="moving platform; static proxy for now"),
    "FallingBlock": _solid("FallingBlock", dynamic=DYNAMIC_STATIC_PROXY,
                           note="falls when stood on; static proxy for now"),
    "FloatyBlock": _solid("FloatyBlock", dynamic=DYNAMIC_STATIC_PROXY,
                          note="floats; static proxy for now"),
    "MovingBlock": _solid("MovingBlock", dynamic=DYNAMIC_STATIC_PROXY,
                          note="moves; static proxy for now"),
    "GateBlock": _solid("GateBlock", dynamic=DYNAMIC_STATIC_PROXY,
                        note="gate; static proxy for now"),
    "CassetteBlock": _solid("CassetteBlock", dynamic=DYNAMIC_STATIC_PROXY,
                            note="cassette block; static proxy for now"),
    "BreakBlock": _solid("BreakBlock", dynamic=DYNAMIC_STATIC_PROXY,
                         note="breakable; static proxy for now"),
    "DoubleDashPuzzleBlock": _solid("DoubleDashPuzzleBlock", dynamic=DYNAMIC_STATIC_PROXY,
                                    note="puzzle block; static proxy for now"),
    # Point entities (no brushes) — policy for completeness.
    "PlayerSpawn": _none("PlayerSpawn"),
    "Strawberry": _none("Strawberry"),
    "Refill": _none("Refill"),
    "Spring": _none("Spring"),
    "Cassette": _none("Cassette"),
    "Node": _none("Node"),
    "FixedCamera": _none("FixedCamera"),
    "EndingArea": _none("EndingArea"),
    "SignPost": _none("SignPost"),
    "Coin": _none("Coin"),
    "Feather": _none("Feather"),
    "Chimney": _none("Chimney"),
    "IntroCar": _none("IntroCar"),
    "Granny": _none("Granny"),
    "Theo": _none("Theo"),
    "Badeline": _none("Badeline"),
    "func_group": _none("func_group"),
}


# Classes intentionally unsupported (no geometry, no collision). These are
# reported, not silently dropped.
SKIPPED_CLASSES: Dict[str, str] = {
    "Node": "pathfinding node, no geometry",
    "func_group": "TrenchBroom layer container, no geometry",
    "StaticProp": "visual-only point entity, no collision",
    "Coin": "needs coin runtime (future)",
    "Feather": "needs feather runtime (future)",
    "SignPost": "needs sign/dialog runtime (future)",
    "IntroCar": "cutscene entity, no geometry",
    "Granny": "NPC, no geometry",
    "Theo": "NPC, no geometry",
    "Badeline": "NPC, no geometry",
    "Chimney": "needs chimney runtime (future)",
    "FixedCamera": "camera hint, no geometry",
    "EndingArea": "trigger volume, no geometry",
}


def policy_for(classname: str) -> Optional[ClassPolicy]:
    """Return the ClassPolicy for a classname, or None if unknown."""
    return CLASS_POLICIES.get(classname)


def is_skipped(classname: str) -> Optional[str]:
    """Return the skip reason if the class is intentionally unsupported."""
    return SKIPPED_CLASSES.get(classname)


def validate_policies(parsed_map, strict: bool = True) -> List[str]:
    """Validate that every brush-bearing entity has an explicit policy.

    Returns a list of error strings. In strict mode, an unknown brush-bearing
    class is a hard failure (caller must raise). In non-strict mode, the
    errors are returned for reporting.
    """
    errors: List[str] = []
    for ei, ent in enumerate(parsed_map.entities):
        if not ent.brushes:
            continue
        if policy_for(ent.classname) is None and is_skipped(ent.classname) is None:
            errors.append(
                f"entity {ei} class '{ent.classname}' has {len(ent.brushes)} "
                f"brushes but no policy and no skip reason"
            )
    return errors


def summarize_policies() -> Dict[str, Dict[str, Any]]:
    """Return a deterministic policy summary for the report."""
    out: Dict[str, Dict[str, Any]] = {}
    for name in sorted(CLASS_POLICIES):
        out[name] = CLASS_POLICIES[name].to_dict()
    return out
