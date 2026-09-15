#!/usr/bin/env python3
"""Compose A3-only dark irrigated-soil art from the authored tropical water family.

The south farm beds are built from WATER1 land tiles bounded by POOL/ANOTHERDEBRIS
pieces. Recolouring only the pool edge therefore leaves a bright blue interior.
This preserves every TR_WATER frame's dimensions, offsets and alpha but turns the
water pixels into damp tropical loam. Terrain identity stays WATER1 for now; this
pass is deliberately visual-only until gameplay movement is audited.
"""
from __future__ import annotations

import argparse
import colorsys
import struct
from pathlib import Path

from audit_rural_sti import decode_sti


def clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def remap(frame, frame_index: int):
    rgba = frame.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            mx, mn = max(r, g, b), min(r, g, b)
            sat = 0.0 if mx == 0 else (mx - mn) / float(mx)
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            lum = (30 * r + 59 * g + 11 * b) // 100
            noise = ((x * 19 + y * 31 + frame_index * 7) % 15) - 7

            # Tropical water/cyan becomes damp, nearly-black loam with warm highlights.
            if (0.42 <= h <= 0.75 and sat > 0.16) or b > r + 8:
                nr = 30 + (lum * 31) // 100 + noise
                ng = 22 + (lum * 22) // 100 + noise // 2
                nb = 12 + (lum * 12) // 100
                # Faint longitudinal texture reads as shallow furrows rather than flat paint.
                if ((x + frame_index * 3) % 11) in (0, 1):
                    nr -= 6
                    ng -= 5
                    nb -= 2
            else:
                # Foam/neutral pixels become packed, slightly drier soil.
                nr = 45 + (lum * 42) // 100 + noise
                ng = 31 + (lum * 30) // 100 + noise // 2
                nb = 17 + (lum * 18) // 100

            px[x, y] = (clamp(nr), clamp(ng), clamp(nb), a)
    return rgba


def write_b1tc(path: Path, frames, meta):
    path.parent.mkdir(parents=True, exist_ok=True)
    offset = 8 + len(frames) * 16
    entries, payloads = [], []
    for frame, m in zip(frames, meta):
        raw = frame.convert("RGBA").tobytes()
        entries.append((int(m["offset_x"]), int(m["offset_y"]),
                        frame.width, frame.height, offset, len(raw)))
        payloads.append(raw)
        offset += len(raw)
    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(entries)))
        for e in entries:
            fh.write(struct.pack("<hhHHII", *e))
        for raw in payloads:
            fh.write(raw)


def resolve_water_source(tilesets_root: Path, preferred_tileset: Path) -> Path:
    """Find the real TR_WATER source in the fetched Vengeance tile library.

    Tileset 38 is an A3 composition target and does not necessarily carry every
    legacy tropical source STI. Prefer it when present, then fall back to the
    actual source family elsewhere in the same disposable Data-Maps-Tiles tree.
    """
    for name in ("TR_WATER.STI", "tr_water.sti"):
        candidate = preferred_tileset / name
        if candidate.is_file():
            return candidate

    matches = sorted(
        p for p in tilesets_root.rglob("*")
        if p.is_file() and p.name.lower() == "tr_water.sti"
    )
    if not matches:
        raise FileNotFoundError(
            f"TR_WATER.STI was not found anywhere under fetched tilesets root: {tilesets_root}"
        )

    # Deterministic choice. All candidates are from the freshly fetched,
    # disposable map/tile repository; never consult the live game installation.
    source = matches[0]
    print(f"Tileset 38 has no TR_WATER.STI; using authored source {source}")
    return source


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns = ap.parse_args()

    source = resolve_water_source(ns.tilesets_root, ns.target_38)
    frames, meta, _ = decode_sti(source)
    if not frames:
        raise ValueError(f"{source}: no frames decoded")

    out_frames = [remap(frame, i + 1) for i, frame in enumerate(frames)]
    target = ns.target_38 / "A3_WET_SOIL.b1tc"
    write_b1tc(target, out_frames, meta)
    print(f"wrote {target}: {len(out_frames)} tropical-water frames -> dark irrigated soil")


if __name__ == "__main__":
    main()
