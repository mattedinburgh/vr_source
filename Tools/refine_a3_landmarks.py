#!/usr/bin/env python3
"""Refine A3 landmark frames with proven Vengeance rural art.

This tool is intentionally separate from the active render pipeline until the
next screenshot-review pass. It preserves the existing A3 landmark family and
only replaces selected weak frames with audited STI artwork.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path
from PIL import Image

from audit_rural_sti import decode_sti


def read_b1tc(path: Path):
    data = path.read_bytes()
    if data[:4] != b"B1TC":
        raise ValueError(f"{path}: bad B1TC magic")
    version, count = struct.unpack_from("<HH", data, 4)
    if version != 1:
        raise ValueError(f"{path}: unsupported B1TC version {version}")
    out = []
    for i in range(count):
        ox, oy, w, h, src, n = struct.unpack_from("<hhHHII", data, 8 + i * 16)
        expected = w * h * 4
        if n != expected or src + n > len(data):
            raise ValueError(f"{path}: frame {i+1} payload invalid")
        frame = Image.frombytes("RGBA", (w, h), data[src:src+n])
        out.append((frame, {"offset_x": ox, "offset_y": oy}))
    return out


def write_b1tc(path: Path, frames):
    directory_size = 8 + len(frames) * 16
    payload_offset = directory_size
    entries = []
    payloads = []
    for frame, meta in frames:
        rgba = frame.convert("RGBA").tobytes()
        entries.append((
            int(meta["offset_x"]), int(meta["offset_y"]),
            frame.width, frame.height, payload_offset, len(rgba)
        ))
        payloads.append(rgba)
        payload_offset += len(rgba)

    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(entries)))
        for entry in entries:
            fh.write(struct.pack("<hhHHII", *entry))
        for payload in payloads:
            fh.write(payload)


def sti_frame(tilesets_root: Path, rel: str, one_based: int):
    frames, meta, _ = decode_sti(tilesets_root / rel)
    idx = one_based - 1
    if idx < 0 or idx >= len(frames):
        raise ValueError(f"{rel}: frame {one_based} unavailable")
    return frames[idx].convert("RGBA"), {
        "offset_x": int(meta[idx]["offset_x"]),
        "offset_y": int(meta[idx]["offset_y"]),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns = ap.parse_args()

    target = ns.target_38 / "A3_LANDMARKS.b1tc"
    frames = read_b1tc(target)
    if len(frames) < 8:
        raise ValueError(f"{target}: expected at least 8 frames")

    # Existing semantic slots used by worlddef.cpp:
    #   4 = water tank/pump corner
    #   7 = lean-to / work shelter
    #   8 = trough / paddock furniture
    #
    # These replacements come from audited original Vengeance art, so they have
    # stronger shading/perspective than the provisional flat-pixel versions.
    replacements = {
        4: ("35/2_GENS.STI", 1),   # cylindrical utility tank/pump assembly
        7: ("53/FURN_MIX.STI", 5), # rough rural rack/shelter silhouette
        8: ("53/FURN_MIX.STI", 6), # low wooden/mesh trough-like farm fixture
    }

    for one_based, (rel, source_frame) in replacements.items():
        frames[one_based - 1] = sti_frame(ns.tilesets_root, rel, source_frame)
        print(f"landmark {one_based}: {rel} frame {source_frame}")

    write_b1tc(target, frames)
    print(f"wrote refined landmark family: {target}")


if __name__ == "__main__":
    main()
