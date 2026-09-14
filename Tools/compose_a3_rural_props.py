#!/usr/bin/env python3
"""Compose higher-quality A3 visual-only prop B1TC families from existing Vengeance STI art."""
from __future__ import annotations
import argparse
import struct
from pathlib import Path

from audit_rural_sti import decode_sti

def write_b1tc(path: Path, selected):
    path.parent.mkdir(parents=True, exist_ok=True)
    directory_size = 8 + len(selected) * 16
    offset = directory_size
    entries = []
    payloads = []
    for frame, meta, source in selected:
        rgba = frame.convert("RGBA").tobytes()
        entries.append((
            int(meta["offset_x"]), int(meta["offset_y"]),
            frame.width, frame.height, offset, len(rgba)
        ))
        payloads.append(rgba)
        offset += len(rgba)
    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(entries)))
        for ox,oy,w,h,src,n in entries:
            fh.write(struct.pack("<hhHHII", ox,oy,w,h,src,n))
        for p in payloads:
            fh.write(p)

def select(root: Path, specs):
    cache = {}
    out = []
    for rel, one_based in specs:
        p = root / rel
        if rel not in cache:
            frames, meta, _ = decode_sti(p)
            cache[rel] = (frames, meta)
        frames, meta = cache[rel]
        i = one_based - 1
        if i < 0 or i >= len(frames):
            raise ValueError(f"{rel}: frame {one_based} unavailable")
        out.append((frames[i], meta[i], rel))
    return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns=ap.parse_args()

    # Wood-yard slot: high-quality crates, pallets and stacked supplies.
    wood = [
        ("10/CratewPallet.sti", 8),
        ("10/CratewPallet.sti", 2),
        ("10/CratewPallet.sti", 3),
        ("10/CratewPallet.sti", 4),
        ("10/CratewPallet.sti", 5),
        ("10/CratewPallet.sti", 6),
        ("10/CratewPallet.sti", 7),
        ("10/CratewPallet.sti", 1),
        ("10/CratewPallet.sti", 9),
        ("10/CratewPallet.sti", 10),
    ]

    # General farm-junk slot. Active A3 blocks primarily use frames 1,3,5,6,7;
    # keep those as plausible small farmyard clutter rather than oversized machinery.
    junk = [
        ("10/CratewPallet.sti", 2),  # 1 small crate
        ("10/CratewPallet.sti", 5),  # 2 weathered crate
        ("10/CratewPallet.sti", 8),  # 3 stacked sacks/supplies
        ("35/DRUM_01.STI", 7),       # 4 small upright drum
        ("35/DRUM_01.STI", 5),       # 5 horizontal drum
        ("35/DRUM_01.STI", 6),       # 6 larger barrel/pipe cluster
        ("50/I_CRATES.STI", 24),     # 7 small barrel
        ("10/CratewPallet.sti", 3),  # 8 crate
        ("35/DRUM_01.STI", 1),       # 9 pipe/utility clutter
        ("50/I_CRATES.STI", 20),     # 10 rusty utility tank
    ]

    # Edge weeds: preserve the original professional JA2 silhouettes wholesale.
    weeds = [("53/FL_WEED.STI", i) for i in range(1,13)]

    families = {
        "A3_WOOD_YARD.b1tc": wood,
        "A3_FARM_JUNK.b1tc": junk,
        "A3_EDGE_WEEDS.b1tc": weeds,
    }

    for name, specs in families.items():
        selected = select(ns.tilesets_root, specs)
        target = ns.target_38 / name
        write_b1tc(target, selected)
        print(f"wrote {target} ({len(selected)} frames)")
        for i,(_,meta,source) in enumerate(selected,1):
            print(f"  {i:02d}: {source} frame={specs[i-1][1]} "
                  f"{meta['width']}x{meta['height']} off={meta['offset_x']},{meta['offset_y']}")

if __name__ == "__main__":
    main()
