#!/usr/bin/env python3
"""Turn A3's authored POOL tile geometry into irrigated soil/crop beds.

A3 already contains 303 POOL object pieces arranged as long agricultural beds.
The geometry is useful, but the stock cyan/blue water makes the new crops read
as swimming pools/hydroponics.  This compositor preserves every frame's size,
offset and alpha while recolouring the interior to wet tropical loam and the
rim to sun-worn concrete/stone.
"""
from __future__ import annotations

import argparse
import colorsys
import struct
from pathlib import Path

from audit_rural_sti import decode_sti


def remap_pixel(r: int, g: int, b: int, a: int, x: int, y: int):
    if a == 0:
        return r, g, b, a

    mx = max(r, g, b)
    mn = min(r, g, b)
    sat = 0.0 if mx == 0 else (mx - mn) / float(mx)
    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    lum = (30 * r + 59 * g + 11 * b) // 100

    # Stock POOL water is blue/cyan. Convert it to damp furrow soil. Keep local
    # luma differences so the authored ripples/texture become soil variation.
    if sat > 0.20 and 0.44 <= h <= 0.72 and b >= r:
        noise = ((x * 17 + y * 29) % 9) - 4
        nr = max(20, min(96, int(lum * 0.58) + 26 + noise))
        ng = max(16, min(78, int(lum * 0.42) + 20 + noise // 2))
        nb = max(10, min(55, int(lum * 0.27) + 13))
        return nr, ng, nb, a

    # Low-saturation pool coping becomes dusty, warm field-bed edging.
    if sat < 0.24:
        noise = ((x * 11 + y * 7) % 7) - 3
        nr = max(35, min(180, int(lum * 0.82) + 27 + noise))
        ng = max(30, min(158, int(lum * 0.72) + 20 + noise))
        nb = max(24, min(125, int(lum * 0.55) + 14))
        return nr, ng, nb, a

    # Preserve miscellaneous dark/detail pixels but warm them slightly.
    nr = min(255, int(r * 1.03) + 3)
    ng = min(255, int(g * 0.94) + 2)
    nb = min(255, int(b * 0.78))
    return nr, ng, nb, a


def write_b1tc(path: Path, frames, meta):
    path.parent.mkdir(parents=True, exist_ok=True)
    directory_size = 8 + len(frames) * 16
    payload_offset = directory_size
    payloads = []
    entries = []

    for frame, m in zip(frames, meta):
        rgba = frame.convert("RGBA")
        px = rgba.load()
        for y in range(rgba.height):
            for x in range(rgba.width):
                px[x, y] = remap_pixel(*px[x, y], x, y)
        payload = rgba.tobytes()
        entries.append((
            int(m["offset_x"]), int(m["offset_y"]),
            rgba.width, rgba.height, payload_offset, len(payload)
        ))
        payloads.append(payload)
        payload_offset += len(payload)

    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(entries)))
        for entry in entries:
            fh.write(struct.pack("<hhHHII", *entry))
        for payload in payloads:
            fh.write(payload)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns = ap.parse_args()

    source = ns.tilesets_root / "37" / "POOL.STI"
    frames, meta, _ = decode_sti(source)
    if len(frames) != 10:
        raise ValueError(f"{source}: expected 10 POOL frames, got {len(frames)}")

    target = ns.target_38 / "A3_FIELD_BEDS.b1tc"
    write_b1tc(target, frames, meta)
    print(f"wrote {target}: {len(frames)} authored POOL frames -> warm irrigated soil beds")


if __name__ == "__main__":
    main()
