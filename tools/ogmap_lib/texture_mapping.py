#!/usr/bin/env python3
"""
Texture mapping utilities for OG Map processing.

Computes UV coordinates from Quake face texture parameters.
"""

from typing import Tuple, Dict
from . import Vec3, vdot, vcross, vnormalize


def compute_uv(
    point: Vec3,
    face: Dict,
    scale: float = 0.2
) -> Tuple[float, float]:
    """Compute UV coordinates from Quake face parameters.

    Quake UVs: project point onto the face's texture axis, apply shift/rotation/scale.

    Args:
        point: Vertex position in Quake space (before game-space transform)
        face: Face dict with 'normal', 'p1', 'rotation', 'scale_u', 'scale_v',
              'shift_u', 'shift_v', 'texture'
        scale: World scale factor (unused in UV computation, kept for compatibility)

    Returns:
        (u, v) texture coordinates in texture-repeat units
    """
    normal = face["normal"]
    p1 = face["p1"]

    rotation_deg = face.get("rotation", 0.0)
    scale_u = face.get("scale_u", 1.0)
    scale_v = face.get("scale_v", 1.0)
    shift_u = face.get("shift_u", 0.0)
    shift_v = face.get("shift_v", 0.0)

    # Handle zero scale
    if scale_u == 0:
        scale_u = 1.0
    if scale_v == 0:
        scale_v = 1.0

    # Determine base axis from dominant normal component (Quake convention)
    if abs(normal[0]) > abs(normal[1]) and abs(normal[0]) > abs(normal[2]):
        # X-normal face: U=Z, V=Y
        if normal[0] > 0:
            axis_u = (0, 0, -1)  # -Z
            axis_v = (0, -1, 0)   # -Y
        else:
            axis_u = (0, 0, 1)    # Z
            axis_v = (0, -1, 0)    # -Y
    elif abs(normal[1]) > abs(normal[2]):
        # Y-normal face: U=X, V=Z
        if normal[1] > 0:
            axis_u = (1, 0, 0)
            axis_v = (0, 0, -1)
        else:
            axis_u = (1, 0, 0)
            axis_v = (0, 0, 1)
    else:
        # Z-normal face: U=X, V=-Y (Quake convention)
        if normal[2] > 0:
            axis_u = (1, 0, 0)
            axis_v = (0, -1, 0)
        else:
            axis_u = (1, 0, 0)
            axis_v = (0, 1, 0)

    # Apply rotation if specified
    if rotation_deg != 0:
        import math
        theta = math.radians(rotation_deg)
        cos_t = math.cos(theta)
        sin_t = math.sin(theta)
        # Rotate axes (must use original axis_u before overwriting)
        old_axis_u = axis_u
        axis_u = (
            old_axis_u[0] * cos_t + axis_v[0] * sin_t,
            old_axis_u[1] * cos_t + axis_v[1] * sin_t,
            old_axis_u[2] * cos_t + axis_v[2] * sin_t
        )
        axis_v = (
            -old_axis_u[0] * sin_t + axis_v[0] * cos_t,
            -old_axis_u[1] * sin_t + axis_v[1] * cos_t,
            -old_axis_u[2] * sin_t + axis_v[2] * cos_t
        )

    # Project point onto texture axes
    u = vdot(point, axis_u) / scale_u + shift_u
    v = vdot(point, axis_v) / scale_v + shift_v

    return (u, v)
