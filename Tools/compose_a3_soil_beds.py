#!/usr/bin/env python3
"""Build A3-only raised soil-bed art from the legacy POOL.STI geometry.

A3's authored field uses ANOTHERDEBRIS/pool.sti hundreds of times.  The original
blue water pixels read as swimming-pool strips in tactical view.  This converter
keeps every frame's dimensions, offsets and alpha silhouette but remaps the
visible pixels into weathered timber/earth borders and dark cultivated soil.
The result is visual-only; map placement, collision and interaction are untouched.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image

from audit_rural_sti import decode_sti


def clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def recolor_frame(frame: Image.Image, frame_index: int) -> Image.Image:
    src = frame.convert("RGBA")
    out = Image.new("RGBA", src.size, (0, 0, 0, 0))
    src_px = src.load()
    dst_px = out.load()

    for y in range(src.height):
        for x in range(src.width):
            r, g, b, a = src_px[x, y]
            if a == 0:
                continue

            hi = max(r, g, b)
            lo = min(r, g, b)
            chroma = hi - lo
            luma = (r * 3 + g * 5 + b * 2) // 10

            # Deterministic micro-variation prevents large flat colour bands.
            noise = ((x * 17 + y * 29 + frame_index * 13) % 13) - 6

            # POOL.STI's teal/blue interior becomes damp, cultivated soil.
            blue_or_teal = (b > r + 5 and g > r + 2)
            if blue_or_teal:
                nr = 42 + (luma * 38) // 100 + noise
                ng = 28 + (luma * 23) // 100 + noise // 2
                nb = 15 + (luma * 13) // 100
                # A sparse darker grain hints at furrows without changing silhouette.
                if ((x + y * 2 + frame_index) % 9) == 0:
                    nr -= 10
                    ng -= 7
                    nb -= 4
            elif chroma < 36:
                # Neutral pool coping becomes sun-bleached timber/packed-earth edging.
                nr = 62 + (luma * 48) // 100 + noise
                ng = 47 + (luma * 36) // 100 + noise // 2
                nb = 30 + (luma * 24) // 100
            else:
                # Rare coloured pixels are still warmed so nothing remains pool-blue.
                nr = 52 + (luma * 42) // 100 + noise
                ng = 36 + (luma * 29) // 100
                nb = 21 + (luma * 18) // 100

            dst_px[x, y] = (clamp(nr), clamp(ng), clamp(nb), a)

    return out


def write_b1tc(path: Path, frames, metadata) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    directory_size = 8 + len(frames) * 16
    payload_offset = directory_size
    entries = []
    payloads = []

    for frame, meta in zip(frames, metadata):
        rgba = frame.convert("RGBA").tobytes()
        entries.append((
            int(meta["offset_x"]),
            int(meta["offset_y"]),
            frame.width,
            frame.height,
            payload_offset,
            len(rgba),
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns = ap.parse_args()

    source = ns.tilesets_root / "37" / "POOL.STI"
    frames, metadata, _ = decode_sti(source)
    if len(frames) != 10:
        raise ValueError(f"{source}: expected 10 frames, found {len(frames)}")

    converted = [recolor_frame(frame, i + 1) for i, frame in enumerate(frames)]
    target = ns.target_38 / "A3_SOIL_BEDS.b1tc"
    write_b1tc(target, converted, metadata)
    print(f"wrote {target} ({len(converted)} frames) from {source}")
    for i, (frame, meta) in enumerate(zip(converted, metadata), 1):
        print(
            f"  {i:02d}: {frame.width}x{frame.height} "
            f"off={meta['offset_x']},{meta['offset_y']}"
        )


if __name__ == "__main__":
    main()
