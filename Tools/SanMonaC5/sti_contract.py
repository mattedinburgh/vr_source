#!/usr/bin/env python3
"""Minimal read-only JA2 STCI/STI parser for native VHD asset production.

Supports indexed 8-bit ETRLE-compressed STI files, which are the tactical tile
format used by the San Mona C5 pilot. This reader is intentionally narrow:
it extracts frame geometry, offsets, palette-derived RGBA pixels and appdata
without modifying the source asset.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

from PIL import Image

STCI_INDEXED = 0x0008
STCI_ETRLE_COMPRESSED = 0x0020
STCI_HEADER_SIZE = 64
STCI_SUBIMAGE_SIZE = 16

@dataclass(frozen=True)
class STIFrame:
    width: int
    height: int
    offset_x: int
    offset_y: int
    rgba: Image.Image

@dataclass(frozen=True)
class STIFile:
    frames: list[STIFrame]
    appdata: bytes
    flags: int
    depth: int

def _u16(b: bytes, off: int) -> int:
    return struct.unpack_from("<H", b, off)[0]

def _u32(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]

def read_sti(path: str | Path) -> STIFile:
    path = Path(path)
    raw = path.read_bytes()
    if len(raw) < STCI_HEADER_SIZE or raw[:4] != b"STCI":
        raise ValueError(f"{path}: not an STCI/STI file")

    stored_size = _u32(raw, 8)
    flags = _u32(raw, 16)
    if not (flags & STCI_INDEXED):
        raise ValueError(f"{path}: only indexed STI is supported (flags=0x{flags:08x})")
    if not (flags & STCI_ETRLE_COMPRESSED):
        raise ValueError(f"{path}: only ETRLE STI is supported (flags=0x{flags:08x})")

    colours = _u32(raw, 24)
    count = _u16(raw, 28)
    depth = raw[44]
    appdata_size = _u32(raw, 45)

    if colours <= 0 or colours > 256:
        raise ValueError(f"{path}: invalid palette size {colours}")
    if count <= 0:
        raise ValueError(f"{path}: STI has no subimages")
    if depth != 8:
        raise ValueError(f"{path}: expected 8-bit indexed STI, got {depth}")

    pos = STCI_HEADER_SIZE
    palette_bytes = colours * 3
    if pos + palette_bytes > len(raw):
        raise ValueError(f"{path}: truncated palette")
    palette = []
    for i in range(colours):
        r, g, b = raw[pos + i*3: pos + i*3 + 3]
        palette.append((r, g, b, 255))
    pos += palette_bytes

    table_bytes = count * STCI_SUBIMAGE_SIZE
    if pos + table_bytes > len(raw):
        raise ValueError(f"{path}: truncated subimage table")

    entries = []
    for i in range(count):
        off = pos + i * STCI_SUBIMAGE_SIZE
        data_off, data_len, ox, oy, h, w = struct.unpack_from("<IIhhHH", raw, off)
        if w <= 0 or h <= 0:
            raise ValueError(f"{path}: invalid frame {i} size {w}x{h}")
        entries.append((data_off, data_len, ox, oy, w, h))
    pos += table_bytes

    pixel_data_start = pos
    pixel_data_end = pixel_data_start + stored_size
    if pixel_data_end > len(raw):
        raise ValueError(f"{path}: truncated pixel payload")

    frames: list[STIFrame] = []
    for idx, (data_off, data_len, ox, oy, w, h) in enumerate(entries):
        start = pixel_data_start + data_off
        end = start + data_len
        if start < pixel_data_start or end > pixel_data_end:
            raise ValueError(f"{path}: frame {idx} payload outside STI pixel block")

        payload = raw[start:end]
        rgba = bytearray(w * h * 4)
        p = 0

        for y in range(h):
            x = 0
            while True:
                if p >= len(payload):
                    raise ValueError(f"{path}: frame {idx} ended before row {y}")
                code = payload[p]
                p += 1
                if code == 0:
                    break

                n = code & 0x7F
                if n == 0:
                    raise ValueError(f"{path}: frame {idx} has invalid zero-length run")
                if x + n > w:
                    raise ValueError(f"{path}: frame {idx} row {y} overruns width")

                if code & 0x80:
                    x += n
                    continue

                if p + n > len(payload):
                    raise ValueError(f"{path}: frame {idx} opaque run is truncated")
                for k in range(n):
                    pal_idx = payload[p + k]
                    if pal_idx >= len(palette):
                        raise ValueError(f"{path}: frame {idx} palette index {pal_idx} out of range")
                    r, g, b, a = palette[pal_idx]
                    dst = ((y * w) + x + k) * 4
                    rgba[dst:dst+4] = bytes((r, g, b, a))
                p += n
                x += n

            # Some old assets terminate short scanlines with transparent tail.
            if x > w:
                raise ValueError(f"{path}: frame {idx} invalid scanline width")

        img = Image.frombytes("RGBA", (w, h), bytes(rgba))
        frames.append(STIFrame(w, h, ox, oy, img))

    appdata_start = pixel_data_end
    appdata_end = appdata_start + appdata_size
    if appdata_end > len(raw):
        raise ValueError(f"{path}: truncated appdata")
    appdata = raw[appdata_start:appdata_end]

    return STIFile(frames=frames, appdata=appdata, flags=flags, depth=depth)
