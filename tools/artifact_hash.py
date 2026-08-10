#!/usr/bin/env python3
"""Deterministic content hash for map-pack artifacts (Inc 4).

Defines one small deterministic content hash (CRC32) over each artifact and
the source map. The writer records hash plus byte size; the N64 loader uses
the same implementation to reject stale/truncated DFS files.

Human-readable reports may additionally include SHA-256, but the runtime
contract does not depend on a host-only hash.
"""

import zlib
from pathlib import Path


def crc32_bytes(data: bytes) -> int:
    """CRC32 of raw bytes (matches the C++ artifact_hash implementation)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def crc32_file(path) -> int:
    """CRC32 of a file's contents."""
    h = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h = zlib.crc32(chunk, h) & 0xFFFFFFFF
    return h


def artifact_hash(path) -> dict:
    """Return {crc32, size_bytes, sha256} for a file."""
    data = Path(path).read_bytes()
    return {
        "crc32": crc32_bytes(data),
        "size_bytes": len(data),
        "sha256": __import__("hashlib").sha256(data).hexdigest(),
    }
