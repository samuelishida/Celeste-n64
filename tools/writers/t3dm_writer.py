#!/usr/bin/env python3
"""T3DM writer — generates T3DM from ParsedMap via GLB intermediate.

Placeholder: GLB generation from brush geometry is not yet implemented.
The pipeline currently produces LVL + colmesh for rendering and collision;
T3DM-based room rendering is a future increment.

This module will be expanded when GLB authoring is ready.
"""

import sys
from pathlib import Path
from typing import Optional

# Import library
sys.path.insert(0, str(Path(__file__).parent.parent))
from ogmap_lib import ParsedMap


class T3dmStats:
    """Statistics from T3DM generation."""
    def __init__(self):
        self.chunks: int = 0
        self.vertices: int = 0
        self.indices: int = 0


def write_t3dm(
    parsed_map: ParsedMap,
    out_path: str,
    scale: float = 0.2,
    eps: float = 1e-4,
    toolchain_dir: Optional[str] = None,
    strict: bool = False
) -> T3dmStats:
    """Write T3DM file from ParsedMap.

    Not yet implemented. The pipeline currently produces LVL + colmesh
    for rendering and collision. T3DM-based room rendering is a future
    increment that requires GLB authoring from brush geometry.

    Raises:
        NotImplementedError: Always, until GLB generation is implemented.
    """
    raise NotImplementedError(
        "T3DM GLB generation is not yet implemented. "
        "The pipeline produces LVL + colmesh for rendering and collision; "
        "T3DM-based room rendering is a future increment."
    )
