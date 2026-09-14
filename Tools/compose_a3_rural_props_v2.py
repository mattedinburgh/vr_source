#!/usr/bin/env python3
"""Candidate second-pass A3 rural prop compositor.

Prepared from the extended STI audit.  It intentionally lives beside, rather
than replacing, the active compositor until the current in-engine render is
reviewed.  The next iteration can promote this file after screenshot QA.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from audit_rural_sti import decode_sti


def write_b1tc(path: Path, selected):
    path.parent.mkdir(parents=True, exist_ok=True)
    payload_offset = 8 + len(selected) * 16
    directory = []
    payloads = []
    for frame, meta, source in selected:
        rgba = frame.convert("RGBA").tobytes()
        directory.append((
            int(meta["offset_x"]), int(meta["offset_y"]),
            frame.width, frame.height, payload_offset, len(rgba),
        ))
        payloads.append(rgba)
        payload_offset += len(rgba)

    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(directory)))
        for entry in directory:
            fh.write(struct.pack("<hhHHII", *entry))
        for payload in payloads:
            fh.write(payload)


def select(root: Path, specs):
    cache = {}
    out = []
    for rel, one_based in specs:
        if rel not in cache:
            cache[rel] = decode_sti(root / rel)[:2]
        frames, meta = cache[rel]
        i = one_based - 1
        if not 0 <= i < len(frames):
            raise ValueError(f"{rel}: frame {one_based} unavailable")
        out.append((frames[i], meta[i], rel))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--target-38", required=True, type=Path)
    ns = ap.parse_args()

    # Keep the clean crate/pallet family; these silhouettes already pass visual QA.
    wood_specs = [
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

    # Farm-junk semantics are aligned to the frames worlddef.cpp actually uses.
    # This replaces the first-pass "mostly crates and drums" look with a real
    # working-yard vocabulary: tool benches, machinery, boxes and barrels.
    junk_specs = [
        ("10/CratewPallet.sti", 2),  # 1: small crate
        ("10/CratewPallet.sti", 5),  # 2: weathered crate
        ("58/WORKSHOP.STI", 9),      # 3: tool/work bench
        ("58/WORKSHOP.STI", 4),      # 4: long workshop table
        ("58/WORKSHOP.STI", 3),      # 5: compact machine/tool unit
        ("53/FURN_MIX.STI", 3),      # 6: drums / farm utility clutter
        ("35/DRUM_01.STI", 7),       # 7: upright barrel
        ("10/CratewPallet.sti", 3),  # 8: crate
        ("58/WORKSHOP.STI", 5),      # 9: small supply box
        ("58/WORKSHOP.STI", 7),      # 10: stacked cartons/supplies
    ]

    weed_specs = [("53/FL_WEED.STI", i) for i in range(1, 13)]

    families = {
        "A3_WOOD_YARD.b1tc": wood_specs,
        "A3_FARM_JUNK.b1tc": junk_specs,
        "A3_EDGE_WEEDS.b1tc": weed_specs,
    }

    for name, specs in families.items():
        selected = select(ns.tilesets_root, specs)
        target = ns.target_38 / name
        write_b1tc(target, selected)
        print(f"wrote {target} ({len(selected)} frames)")
        for i, (_, meta, source) in enumerate(selected, 1):
            print(
                f"  {i:02d}: {source} frame={specs[i-1][1]} "
                f"{meta['width']}x{meta['height']} "
                f"off={meta['offset_x']},{meta['offset_y']}"
            )


if __name__ == "__main__":
    main()
