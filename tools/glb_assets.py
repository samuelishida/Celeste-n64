#!/usr/bin/env python3
"""Strict, dependency-light GLB v2 helpers used by asset conversion."""

from __future__ import annotations

import io
import json
import struct
from pathlib import Path
from typing import Any

from PIL import Image


JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942
SUPPORTED_MIME = {"image/png", "image/jpeg", "image/webp"}


class GlbError(ValueError):
    """Malformed or unsupported GLB input."""


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF":
        raise GlbError(f"{path}: GLB header missing")
    version, declared_length = struct.unpack_from("<II", data, 4)
    if version != 2 or declared_length != len(data):
        raise GlbError(f"{path}: unsupported GLB version/length")
    cursor = 12
    document: dict[str, Any] | None = None
    binary: bytes | None = None
    while cursor < len(data):
        if cursor + 8 > len(data):
            raise GlbError(f"{path}: truncated chunk header")
        chunk_length, chunk_type = struct.unpack_from("<II", data, cursor)
        begin = cursor + 8
        end = begin + chunk_length
        if end > len(data):
            raise GlbError(f"{path}: chunk exceeds file")
        chunk = data[begin:end]
        if chunk_type == JSON_CHUNK:
            if document is not None:
                raise GlbError(f"{path}: duplicate JSON chunk")
            try:
                document = json.loads(chunk.rstrip(b" \t\r\n\0").decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise GlbError(f"{path}: invalid JSON chunk: {exc}") from exc
            if not isinstance(document, dict):
                raise GlbError(f"{path}: JSON root must be object")
        elif chunk_type == BIN_CHUNK:
            if binary is not None:
                raise GlbError(f"{path}: duplicate BIN chunk")
            binary = chunk
        cursor = end
    if document is None or binary is None:
        raise GlbError(f"{path}: JSON and BIN chunks required")
    return document, binary


def write_glb(path: Path, document: dict[str, Any], binary: bytes) -> None:
    raw_json = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    raw_json += b" " * ((-len(raw_json)) % 4)
    raw_bin = binary + b"\0" * ((-len(binary)) % 4)
    payload = bytearray(b"glTF")
    payload.extend(struct.pack("<II", 2, 12 + 8 + len(raw_json) + 8 + len(raw_bin)))
    payload.extend(struct.pack("<II", len(raw_json), JSON_CHUNK))
    payload.extend(raw_json)
    payload.extend(struct.pack("<II", len(raw_bin), BIN_CHUNK))
    payload.extend(raw_bin)
    path.write_bytes(payload)


def _embedded_bytes(document: dict[str, Any], binary: bytes, image_index: int, source: Path) -> tuple[bytes, str]:
    images = document.get("images")
    if not isinstance(images, list) or not 0 <= image_index < len(images):
        raise GlbError(f"{source}: image index {image_index} out of range")
    image = images[image_index]
    if not isinstance(image, dict):
        raise GlbError(f"{source}: image {image_index} is not an object")
    if image.get("uri") is not None:
        raise GlbError(f"{source}: external image URI is not allowed for image {image_index}")
    view_index = image.get("bufferView")
    mime = image.get("mimeType")
    if not isinstance(view_index, int) or not isinstance(mime, str):
        raise GlbError(f"{source}: image {image_index} requires embedded bufferView/mimeType")
    if mime not in SUPPORTED_MIME:
        raise GlbError(f"{source}: unsupported embedded image MIME {mime!r}")
    views = document.get("bufferViews")
    if not isinstance(views, list) or not 0 <= view_index < len(views):
        raise GlbError(f"{source}: image {image_index} bufferView {view_index} invalid")
    view = views[view_index]
    if not isinstance(view, dict):
        raise GlbError(f"{source}: image {image_index} bufferView malformed")
    begin = int(view.get("byteOffset", 0))
    size = int(view.get("byteLength", 0))
    end = begin + size
    if begin < 0 or size <= 0 or end > len(binary):
        raise GlbError(f"{source}: image {image_index} bytes out of BIN range")
    return binary[begin:end], mime


def extract_embedded_image(source: Path, image_index: int, output: Path) -> Path:
    """Extract exact encoded bytes from embedded GLB image bufferView."""
    document, binary = read_glb(source)
    encoded, _ = _embedded_bytes(document, binary, image_index, source)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(encoded)
    return output


def embedded_image_info(source: Path, image_index: int) -> tuple[int, int, str]:
    """Return decoded width, height, and declared MIME for embedded image."""
    document, binary = read_glb(source)
    encoded, mime = _embedded_bytes(document, binary, image_index, source)
    try:
        with Image.open(io.BytesIO(encoded)) as image:
            width, height = image.size
    except Exception as exc:  # Pillow emits several format-specific errors.
        raise GlbError(f"{source}: image {image_index} decode failed: {exc}") from exc
    if width <= 0 or height <= 0:
        raise GlbError(f"{source}: image {image_index} has empty dimensions")
    return width, height, mime
