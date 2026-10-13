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
DYNAMIC_STATIC_PROXY = "static-proxy"   # static collision proxy; actor render mesh
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
    render_mode = kw.pop("render_mode", RENDER_STATIC)
    return ClassPolicy(classname=classname, collision_mode=COLLISION_SOLID,
                       render_mode=render_mode, material_flags=flags, **kw)


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
    # OG SpikeBlock is an actor-backed AABB trigger, not a Solid. Keep source
    # brush geometry visible as a temporary proxy, but never use its faces as
    # collision or kill surfaces; MPP4 carries the actor volume + direction.
    "SpikeBlock": ClassPolicy(
        classname="SpikeBlock",
        collision_mode=COLLISION_NONE,
        render_mode=RENDER_STATIC,
        material_flags=0,
        note="actor-backed AABB hazard; static brush remains visual proxy only",
    ),
    # DeathBlock is an invisible actor-backed AABB trigger, not collision.
    "DeathBlock": ClassPolicy(
        classname="DeathBlock",
        collision_mode=COLLISION_NONE,
        render_mode=RENDER_NONE,
        material_flags=0,
        note="invisible actor-backed AABB kill volume",
    ),
    "Decoration": _visual("Decoration"),
    "FloatingDecoration": _visual("FloatingDecoration"),
    "StaticProp": _visual("StaticProp"),
    # Dynamic classes keep initial collision geometry in the global CMSH, but
    # render only from actor-local DSOL so no frozen visual duplicate remains.
    "TrafficBlock": _solid("TrafficBlock", render_mode=RENDER_ACTOR,
                           dynamic=DYNAMIC_STATIC_PROXY,
                           note="moving actor; actor-local render and static collision proxy"),
    "FallingBlock": _solid("FallingBlock", render_mode=RENDER_ACTOR,
                           dynamic=DYNAMIC_STATIC_PROXY,
                           note="falling actor; actor-local render and static collision proxy"),
    "FloatyBlock": _solid("FloatyBlock", render_mode=RENDER_ACTOR,
                          dynamic=DYNAMIC_STATIC_PROXY,
                          note="floating actor; actor-local render and static collision proxy"),
    "MovingBlock": _solid("MovingBlock", render_mode=RENDER_ACTOR,
                          dynamic=DYNAMIC_STATIC_PROXY,
                          note="moving actor; actor-local render and static collision proxy"),
    "GateBlock": _solid("GateBlock", render_mode=RENDER_ACTOR,
                        dynamic=DYNAMIC_STATIC_PROXY,
                        note="gate actor; actor-local render and static collision proxy"),
    "CassetteBlock": _solid("CassetteBlock", render_mode=RENDER_ACTOR,
                            dynamic=DYNAMIC_STATIC_PROXY,
                            note="cassette actor; actor-local render and static collision proxy"),
    "BreakBlock": _solid("BreakBlock", render_mode=RENDER_ACTOR,
                         dynamic=DYNAMIC_STATIC_PROXY,
                         note="breakable actor; actor-local render and static collision proxy"),
    "DoubleDashPuzzleBlock": _solid("DoubleDashPuzzleBlock", render_mode=RENDER_ACTOR,
                                    dynamic=DYNAMIC_STATIC_PROXY,
                                    note="puzzle actor; actor-local render and static collision proxy"),
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
    # TrenchBroom group/layer entities can own brush children. OG recursively
    # collects those brushes into the static-solid set under worldspawn.
    "func_group": _solid("func_group", note="child brushes join static world solids"),
}


# Classes intentionally unsupported (no geometry, no collision). These are
# reported, not silently dropped.
SKIPPED_CLASSES: Dict[str, str] = {
    "Node": "pathfinding node, no geometry",
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
