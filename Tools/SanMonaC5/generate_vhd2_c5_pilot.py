#!/usr/bin/env python3
"""
San Mona C5 native VHD2 pilot asset generator.

Production rule:
- legacy structural RGB pixels are NOT copied/upscaled into the new artwork;
- the legacy STI is only a technical/semantic contract: frame count, dimensions,
  alpha footprint, offsets and coarse opening/trim luminance;
- every visible output RGB pixel is newly synthesised at native 2x;
- gameplay identity remains in the original STI/JSD/map references.

Output is native VHD2 B1TC (32-bit RGBA) with doubled dimensions/offsets.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from PIL import Image
from sti_contract import read_sti

PALETTES = {
    "wall": [
        (185, 153, 93),
        (82, 137, 135),
        (164, 104, 91),
        (120, 137, 94),
        (185, 176, 144),
    ],
    "road": [(105, 98, 85), (115, 104, 88), (91, 88, 82)],
    "pave": [(142, 128, 104), (132, 121, 103), (151, 137, 112)],
    "roof": [(125, 75, 52), (102, 85, 68), (119, 96, 75)],
    "ground": [(143, 120, 80), (132, 111, 76), (153, 128, 88)],
}

STRUCTURAL = {
    "COBBLE_ROAD": ("road", "Cobble_Road.sti"),
    "COBBLE_ROAD_PIECES": ("road", "Cobble_Road_Pieces.sti"),
    "PAVE_B": ("pave", "Pave_b.sti"),
    "BUILD_24": ("wall", "build_24.sti"),
    "BUILD_21": ("wall", "build_21.sti"),
    "BUILD_01": ("wall", "build_01.sti"),
    "BUILD_06": ("wall", "build_06.sti"),
    "SLANT_12": ("roof", "slant_12.sti"),
    "FLAT_R1": ("roof", "flat_r1.sti"),
    "FLAT_R2": ("roof", "flat_r2.sti"),
    "FLAT_R3": ("roof", "flat_r3.sti"),
    "SGRASS1": ("ground", "sgrass1.sti"),
}

def h32(*parts: object) -> int:
    s = "|".join(map(str, parts)).encode("utf-8")
    return int.from_bytes(hashlib.sha256(s).digest()[:4], "little")

def clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v

def mix(a, b, t):
    return tuple(clamp(int(round(a[i] * (1.0 - t) + b[i] * t))) for i in range(3))

def sample_semantic(src: Image.Image, x2: int, y2: int):
    x = min(src.width - 1, x2 // 2)
    y = min(src.height - 1, y2 // 2)
    r, g, b, a = src.getpixel((x, y))
    lum = (r * 30 + g * 59 + b * 11) // 100
    return lum, a

def paint_frame(src: Image.Image, family: str, kind: str, index: int) -> Image.Image:
    src = src.convert("RGBA")
    w, h = src.size
    out = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    px = out.load()

    seed = h32(family, index)
    base = PALETTES[kind][seed % len(PALETTES[kind])]

    patch_x = (seed >> 5) % max(1, w * 2)
    patch_y = (seed >> 13) % max(1, h * 2)
    patch_w = max(8, (w * 2) // 4)
    patch_h = max(6, (h * 2) // 5)

    for y in range(h * 2):
        for x in range(w * 2):
            lum, a = sample_semantic(src, x, y)
            if a < 128:
                continue

            n = h32(seed, x // 2, y // 2)
            fine = ((n >> 8) & 31) - 15
            c = base

            if kind == "road":
                stone = 0.86 + (((n >> 16) & 31) - 15) / 180.0
                c = tuple(clamp(int(v * stone)) for v in base)

                # Uneven municipal stone/block paving: dusty joints, local cracks,
                # oil/vehicle wear and piece-meal resurfacing.
                if ((x + (y // 2) + (seed & 7)) % 15) in (0, 1):
                    c = mix(c, (63, 59, 53), 0.34)
                if ((2 * x - y + ((seed >> 9) & 15)) % 29) == 0:
                    c = mix(c, (49, 46, 42), 0.55)
                dx, dy = x - patch_x, y - patch_y
                if dx * dx * 2 + dy * dy * 3 < max(40, patch_w * patch_h):
                    c = mix(c, (48, 45, 39), 0.22)
                if (n & 255) < 24:
                    c = mix(c, (171, 145, 101), 0.22)

            elif kind == "pave":
                c = tuple(clamp(v + fine // 2) for v in base)
                if ((x + seed) % 38) in (0, 1) or ((y + (seed >> 4)) % 28) == 0:
                    c = mix(c, (84, 80, 72), 0.35)
                if (n & 511) < 12:
                    c = mix(c, (75, 67, 57), 0.38)
                if y > h * 3 // 2 and (n & 15) < 3:
                    c = mix(c, (112, 92, 68), 0.18)

            elif kind == "ground":
                c = tuple(clamp(v + fine) for v in base)
                if (n & 63) < 15:
                    c = mix(c, (96, 91, 62), 0.18)
                if (n & 1023) < 10:
                    c = mix(c, (73, 91, 51), 0.62)

            elif kind == "wall":
                # Legacy luminance is used only to keep openings/trim in the correct
                # semantic zone. All output colours/material pixels are new.
                if lum < 42:
                    c = (33, 36, 34)
                elif lum < 78:
                    c = mix(base, (65, 62, 55), 0.55)
                else:
                    c = tuple(clamp(v + fine // 2) for v in base)
                    c = mix(c, (201, 190, 164), 0.08 + ((n >> 24) & 7) / 100.0)

                    # Failed render exposing red masonry.
                    if (n & 1023) < 17 and y > (h * 2) // 5:
                        brick = (139 + ((n >> 12) & 15), 69, 49)
                        c = mix(c, brick, 0.66)

                    # Lower-wall dust/grime and occasional damp/drainage streaks.
                    if y > int(h * 2 * 0.76):
                        c = mix(c, (67, 61, 49), 0.16)
                    if ((x + (seed & 31)) % 47) < 2 and y > (h * 2) // 4:
                        c = mix(c, (70, 78, 61), 0.17)

                if lum >= 78 and ((3 * x + y + (seed & 31)) % 173) == 0:
                    c = mix(c, (69, 61, 52), 0.52)

            elif kind == "roof":
                if lum < 45:
                    c = (47, 44, 40)
                else:
                    c = tuple(clamp(v + fine // 2) for v in base)
                    corr = (x + 2 * y + (seed & 15)) % 14
                    if corr in (0, 1):
                        c = mix(c, (68, 62, 55), 0.23)
                    if (n & 255) < 40:
                        c = mix(c, (151, 68, 39), 0.30)

                    # Mismatched repair sheet / tar patch.
                    if patch_x <= x < patch_x + patch_w and patch_y <= y < patch_y + patch_h:
                        patch = [
                            (83, 102, 102),
                            (111, 81, 61),
                            (75, 91, 96),
                        ][(seed >> 21) % 3]
                        c = mix(c, patch, 0.42)

            px[x, y] = (c[0], c[1], c[2], 255)

    return out

def write_b1tc(path: Path, frames, offsets):
    path.parent.mkdir(parents=True, exist_ok=True)
    count = len(frames)
    directory_size = 8 + count * 16
    payloads = []
    entries = []
    cursor = directory_size

    for img, (ox, oy) in zip(frames, offsets):
        rgba = img.convert("RGBA").tobytes()
        w, h = img.size
        entries.append((ox * 2, oy * 2, w, h, cursor, len(rgba)))
        payloads.append(rgba)
        cursor += len(rgba)

    with path.open("wb") as f:
        f.write(b"B1TC")
        f.write(struct.pack("<HH", 1, count))
        for ox, oy, w, h, off, length in entries:
            f.write(struct.pack("<hhHHII", ox, oy, w, h, off, length))
        for payload in payloads:
            f.write(payload)

def contact_sheet(frames, path: Path, limit=20):
    chosen = frames[:limit]
    if not chosen:
        return

    thumb_w, thumb_h = 240, 180
    cols = 4
    rows = math.ceil(len(chosen) / cols)
    sheet = Image.new("RGB", (cols * thumb_w, rows * thumb_h), (22, 22, 22))

    for i, img in enumerate(chosen):
        rgba = img.convert("RGBA")
        bg = Image.new("RGBA", rgba.size, (48, 48, 48, 255))
        bg.alpha_composite(rgba)
        bg.thumbnail((thumb_w - 8, thumb_h - 8), Image.Resampling.LANCZOS)
        x = (i % cols) * thumb_w + (thumb_w - bg.width) // 2
        y = (i // cols) * thumb_h + (thumb_h - bg.height) // 2
        sheet.paste(bg.convert("RGB"), (x, y))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sti-root", required=True, help="directory containing canonical STI contract files")
    ap.add_argument("--out-root", required=True)
    ap.add_argument("--families", nargs="*", default=[
        "COBBLE_ROAD",
        "COBBLE_ROAD_PIECES",
        "PAVE_B",
        "BUILD_24",
        "SLANT_12",
    ])
    args = ap.parse_args()

    sti_root = Path(args.sti_root)
    out_root = Path(args.out_root)
    requested = [x.upper() for x in args.families]

    manifest = {
        "sector": "C5",
        "profile": "San Mona Strip — native VHD2 scratch structural pilot",
        "render_scale": 2,
        "source_rgb_reused": False,
        "source_usage": "technical frame/alpha/offset contract plus coarse semantic luminance only",
        "families": [],
    }

    for family in requested:
        if family not in STRUCTURAL:
            raise SystemExit(f"Unknown C5 family: {family}")

        kind, filename = STRUCTURAL[family]
        src_path = sti_root / filename
        if not src_path.exists():
            raise SystemExit(f"Missing canonical STI contract: {src_path}")

        contract = read_sti(src_path)
        source_frames = [f.rgba for f in contract.frames]
        offsets = [(f.offset_x, f.offset_y) for f in contract.frames]
        new_frames = [
            paint_frame(im, family, kind, i)
            for i, im in enumerate(source_frames)
        ]

        dst = out_root / "VHD2" / "TILESETS" / "18" / f"{family}.b1tc"
        write_b1tc(dst, new_frames, offsets)
        contact_sheet(new_frames, out_root / "review" / f"{family}_contact.png")

        manifest["families"].append({
            "name": family,
            "source_sti": filename,
            "kind": kind,
            "frames": len(new_frames),
            "appdata_bytes": len(contract.appdata),
            "output": str(dst.relative_to(out_root)).replace("\\", "/"),
            "sha256": hashlib.sha256(dst.read_bytes()).hexdigest(),
        })

    if not manifest["families"]:
        raise SystemExit("No C5 structural families were generated.")

    (out_root / "SAN_MONA_C5_VHD2_MANIFEST.json").write_text(
        json.dumps(manifest, indent=2),
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
