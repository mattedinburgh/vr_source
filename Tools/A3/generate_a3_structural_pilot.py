#!/usr/bin/env python3
"""
A3 structural-art pilot for the Map Visual Overhaul workstream.

The legacy STI is a geometry contract only.  This tool reads:
  - frame count/order;
  - frame dimensions and offsets;
  - transparent/non-transparent footprint.

It NEVER samples or remaps legacy RGB.  Every visible RGB pixel in the generated
B1TC families is newly authored.  The generated files are deliberately
quarantined: no engine routing is changed by this script.

Target visual language:
  A3 / Oronegro outskirts — poor but functioning tropical agricultural compound,
  late-20th-century Latin-American construction, repaired rather than picturesque:
  faded stucco, exposed block/brick, damp lower walls, worn concrete/terracotta
  floors, patched corrugated or sun-aged roof surfaces.

Usage:
  python Tools/A3/generate_a3_structural_pilot.py ^
      --tilesets-root <Data-Maps-Tiles/Tilesets> ^
      --out-root <scratch-output>

Requires Pillow (already used by the map authoring tools).
"""

from __future__ import annotations

import argparse
import hashlib
import math
import random
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from audit_rural_sti import decode_sti  # noqa: E402


WALLS = {
    "BUILD_39": ("build_39.sti", (192, 178, 142), "cream"),
    "BUILD_31": ("build_31.sti", (160, 102, 82), "salmon"),
    "BUILD_40": ("build_40.sti", (102, 145, 132), "mint"),
    "BUILD_35": ("build_35.sti", (176, 142, 84), "ochre"),
}

FLOORS = {
    "WELFLOR3": ("welflor3.sti", (132, 119, 96), "concrete"),
    "P_FLOOR3": ("p-floor3.sti", (151, 105, 74), "terracotta"),
    "WELFLOR1": ("welflor1.sti", (142, 132, 112), "concrete"),
    "WELFLOR2": ("welflor2.sti", (125, 111, 91), "concrete"),
}

ROOFS = {
    "W_ROOF1": ("w-roof1.sti", (115, 103, 88), "corrugated"),
    "SLANT_11": ("slant_11.sti", (148, 92, 65), "tile"),
    "SLANT_13": ("slant_13.sti", (113, 103, 88), "patched"),
}


def clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def seed32(*parts: object) -> int:
    raw = "|".join(map(str, parts)).encode("utf-8")
    return int.from_bytes(hashlib.sha256(raw).digest()[:4], "little")


def resolve_source(tilesets_root: Path, filename: str) -> Path:
    # Prefer the A3 tileset itself.  Vengeance can inherit the source art from
    # another tileset, so search the fetched tile library when it is absent.
    preferred = tilesets_root / "38" / filename
    if preferred.is_file():
        return preferred

    target = filename.lower()
    matches = sorted(
        p for p in tilesets_root.rglob("*")
        if p.is_file() and p.name.lower() == target
    )
    if not matches:
        raise FileNotFoundError(f"{filename} not found under {tilesets_root}")
    return matches[0]


def masked_base(mask: Image.Image, base: tuple[int, int, int], seed: int,
                vertical_weather: bool = True) -> Image.Image:
    w, h = mask.size
    rng = random.Random(seed)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = out.load()
    mp = mask.load()

    coarse = {}
    for y in range(h):
        for x in range(w):
            a = mp[x, y]
            if not a:
                continue
            cell = (x // 5, y // 5)
            if cell not in coarse:
                coarse[cell] = rng.randint(-8, 8)
            fine = ((x * 17 + y * 31 + seed) & 7) - 3
            light = int((h - 1 - y) * 5 / max(1, h - 1)) if vertical_weather else 0
            damp = -int(max(0, y - h * 0.72) * 14 / max(1.0, h * 0.28)) if vertical_weather else 0
            d = coarse[cell] + fine + light + damp
            px[x, y] = (
                clamp(base[0] + d),
                clamp(base[1] + d),
                clamp(base[2] + d),
                a,
            )
    return out


def composite_shape(out: Image.Image, mask: Image.Image, draw_fn) -> None:
    layer = Image.new("RGBA", out.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    draw_fn(d)
    # Avoid numpy dependency: multiply alpha values manually.
    la = layer.getchannel("A")
    clipped = Image.new("L", out.size, 0)
    cp, lp, mp = clipped.load(), la.load(), mask.load()
    for y in range(out.height):
        for x in range(out.width):
            cp[x, y] = (lp[x, y] * mp[x, y]) // 255
    layer.putalpha(clipped)
    out.alpha_composite(layer)


def render_wall(mask: Image.Image, base: tuple[int, int, int], family: str,
                index: int, flavour: str) -> Image.Image:
    seed = seed32("a3-wall", family, index)
    rng = random.Random(seed)
    w, h = mask.size
    out = masked_base(mask, base, seed, True)

    # Broad failed-render patch: contiguous, purposeful construction history.
    if w >= 8 and h >= 8 and index % 3 != 1:
        cx = int(w * (0.30 + 0.35 * rng.random()))
        cy = int(h * (0.48 + 0.25 * rng.random()))
        rx = max(2, int(w * (0.12 + 0.07 * rng.random())))
        ry = max(2, int(h * (0.11 + 0.09 * rng.random())))

        def plaster_failure(d: ImageDraw.ImageDraw) -> None:
            d.ellipse((cx-rx, cy-ry, cx+rx, cy+ry), fill=(111, 74, 53, 205))
            # Newly drawn masonry courses; they are not inferred from old RGB.
            brick_h = max(2, ry // 2)
            for yy in range(cy-ry+1, cy+ry, brick_h):
                d.line((cx-rx, yy, cx+rx, yy), fill=(65, 49, 40, 150), width=1)
                shift = brick_h if ((yy // brick_h) & 1) else 0
                for xx in range(cx-rx+shift, cx+rx, max(3, brick_h * 2)):
                    d.line((xx, yy, xx, min(cy+ry, yy+brick_h)),
                           fill=(68, 50, 40, 130), width=1)

        composite_shape(out, mask, plaster_failure)

    # Sparse cracks, biased from edges/bottom rather than wallpaper noise.
    def cracks(d: ImageDraw.ImageDraw) -> None:
        count = max(1, min(4, (w * h) // 900))
        for k in range(count):
            x = rng.randrange(max(1, w // 8), max(2, w - max(1, w // 8)))
            y = rng.randrange(max(1, h // 4), max(2, h - 1))
            pts = [(x, y)]
            for _ in range(rng.randint(2, 4)):
                x += rng.randint(-3, 3)
                y += rng.randint(2, 5)
                pts.append((x, min(h - 1, y)))
            d.line(pts, fill=(72, 65, 56, 105), width=1)

    composite_shape(out, mask, cracks)

    # Damp/splash staining at the base is common in the humid farm environment.
    def damp_band(d: ImageDraw.ImageDraw) -> None:
        y0 = int(h * 0.76)
        d.rectangle((0, y0, w, h), fill=(58, 69, 55, 32))
        for _ in range(max(2, w // 12)):
            x = rng.randrange(0, max(1, w))
            d.ellipse((x-2, y0-2, x+3, min(h, y0+rng.randint(2, 7))),
                      fill=(50, 64, 51, rng.randint(18, 42)))

    composite_shape(out, mask, damp_band)
    return out


def render_floor(mask: Image.Image, base: tuple[int, int, int], family: str,
                 index: int, flavour: str) -> Image.Image:
    seed = seed32("a3-floor", family, index)
    rng = random.Random(seed)
    w, h = mask.size
    out = masked_base(mask, base, seed, False)

    def wear(d: ImageDraw.ImageDraw) -> None:
        # A few large worn patches and hairline joints: legible material, no confetti.
        for _ in range(max(1, (w * h) // 1000)):
            cx = rng.randrange(0, max(1, w))
            cy = rng.randrange(0, max(1, h))
            rx = max(2, rng.randrange(2, max(3, w // 5 + 1)))
            ry = max(1, rng.randrange(1, max(2, h // 5 + 1)))
            d.ellipse((cx-rx, cy-ry, cx+rx, cy+ry), fill=(76, 69, 58, 45))

        if flavour == "terracotta":
            step = max(5, min(w, h) // 3)
            for x in range(-h, w + h, step):
                d.line((x, 0, x + h, h), fill=(89, 66, 54, 70), width=1)
                d.line((x + step // 2, h, x + h + step // 2, 0),
                       fill=(89, 66, 54, 55), width=1)
        else:
            if index % 4 == 0:
                d.line((w // 5, h - 2, w // 2, h // 2, w * 4 // 5, h // 3),
                       fill=(69, 65, 58, 80), width=1)

    composite_shape(out, mask, wear)
    return out


def render_roof(mask: Image.Image, base: tuple[int, int, int], family: str,
                index: int, flavour: str) -> Image.Image:
    seed = seed32("a3-roof", family, index)
    rng = random.Random(seed)
    w, h = mask.size
    out = masked_base(mask, base, seed, False)

    def roof_detail(d: ImageDraw.ImageDraw) -> None:
        if flavour in ("corrugated", "patched"):
            pitch = max(3, min(7, max(3, w // 12)))
            for x in range(-h, w + h, pitch):
                d.line((x, h, x + h, 0), fill=(65, 66, 62, 75), width=1)
                d.line((x + 1, h, x + h + 1, 0), fill=(188, 177, 150, 34), width=1)
            # A small replacement sheet on some frames: practical repair.
            if index % 4 == 0 and w > 10 and h > 8:
                x0 = int(w * 0.47)
                y0 = int(h * 0.38)
                d.polygon(
                    [(x0, y0), (min(w-1, x0+w//4), max(0, y0-h//7)),
                     (min(w-1, x0+w//4+h//6), min(h-1, y0+h//5)),
                     (min(w-1, x0+h//6), min(h-1, y0+h//4))],
                    fill=(93, 104, 98, 130),
                )
        else:  # aged clay tile language
            row = max(3, h // 7)
            for y in range(row, h, row):
                d.line((0, y, w, y), fill=(91, 61, 49, 85), width=1)
                off = (y // row) & 1
                for x in range((row // 2) if off else 0, w, max(4, row)):
                    d.line((x, max(0, y-row), x, y), fill=(91, 61, 49, 55), width=1)

        # Sun bleaching and grime accumulation.
        d.line((0, max(0, h // 6), w, max(0, h // 6)),
               fill=(220, 205, 169, 26), width=max(1, h // 12))

    composite_shape(out, mask, roof_detail)
    return out


def write_b1tc(path: Path, frames: list[Image.Image], meta: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload_offset = 8 + 16 * len(frames)
    entries = []
    payloads = []

    for frame, m in zip(frames, meta):
        rgba = frame.convert("RGBA")
        raw = rgba.tobytes()
        entries.append((
            int(m["offset_x"]), int(m["offset_y"]),
            rgba.width, rgba.height, payload_offset, len(raw)
        ))
        payloads.append(raw)
        payload_offset += len(raw)

    with path.open("wb") as fh:
        fh.write(b"B1TC")
        fh.write(struct.pack("<HH", 1, len(entries)))
        for e in entries:
            fh.write(struct.pack("<hhHHII", *e))
        for raw in payloads:
            fh.write(raw)


def contact_sheet(path: Path, family: str, frames: list[Image.Image]) -> None:
    if not frames:
        return
    cols = 5
    cell_w = max(96, max(f.width for f in frames) + 20)
    cell_h = max(96, max(f.height for f in frames) + 28)
    rows = math.ceil(len(frames) / cols)
    sheet = Image.new("RGBA", (cols * cell_w, rows * cell_h), (36, 36, 36, 255))
    d = ImageDraw.Draw(sheet)

    for i, frame in enumerate(frames):
        cx = (i % cols) * cell_w
        cy = (i // cols) * cell_h
        # Checker-free neutral QA background makes alpha mistakes obvious.
        d.rectangle((cx, cy, cx + cell_w - 1, cy + cell_h - 1),
                    outline=(80, 80, 80, 255))
        x = cx + (cell_w - frame.width) // 2
        y = cy + 18 + (cell_h - 24 - frame.height) // 2
        sheet.alpha_composite(frame, (x, y))
        d.text((cx + 4, cy + 3), f"{family} #{i+1}", fill=(230, 230, 230, 255))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(path, "PNG")


def generate_family(tilesets_root: Path, out_root: Path, qa_root: Path,
                    family: str, filename: str, base: tuple[int, int, int],
                    flavour: str, kind: str) -> dict:
    source = resolve_source(tilesets_root, filename)
    legacy_frames, meta, _ = decode_sti(source)
    if not legacy_frames:
        raise ValueError(f"{source}: no frames")

    generated = []
    for i, legacy in enumerate(legacy_frames):
        mask = legacy.convert("RGBA").getchannel("A")
        if kind == "wall":
            frame = render_wall(mask, base, family, i, flavour)
        elif kind == "floor":
            frame = render_floor(mask, base, family, i, flavour)
        else:
            frame = render_roof(mask, base, family, i, flavour)
        generated.append(frame)

    out = out_root / f"VR_A3_{family}.b1tc"
    write_b1tc(out, generated, meta)
    contact_sheet(qa_root / f"VR_A3_{family}.png", family, generated)
    return {
        "family": family,
        "kind": kind,
        "source_contract": str(source),
        "frames": len(generated),
        "output": str(out),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--out-root", required=True, type=Path)
    ap.add_argument("--qa-root", type=Path)
    ns = ap.parse_args()

    qa_root = ns.qa_root or (ns.out_root / "_qa")
    results = []
    missing = []

    def attempt(family, filename, base, flavour, kind):
        try:
            results.append(generate_family(
                ns.tilesets_root, ns.out_root, qa_root,
                family, filename, base, flavour, kind
            ))
        except FileNotFoundError as exc:
            # A few Ja2Set entries are inherited from legacy libraries that are
            # not checked into vr_gamedir. Never substitute a different frame
            # contract just to make the pilot complete.
            missing.append((family, filename, str(exc)))
            print(f"SKIP {family}: source contract unavailable ({filename})")

    for family, (filename, base, flavour) in WALLS.items():
        attempt(family, filename, base, flavour, "wall")
    for family, (filename, base, flavour) in FLOORS.items():
        attempt(family, filename, base, flavour, "floor")
    for family, (filename, base, flavour) in ROOFS.items():
        attempt(family, filename, base, flavour, "roof")

    manifest = ns.out_root / "A3_STRUCTURAL_PILOT_MANIFEST.txt"
    lines = [
        "A3 STRUCTURAL PILOT - QUARANTINED / NOT ROUTED",
        "Legacy RGB sampled: NO",
        "Legacy contract retained: frame count, dimensions, offsets, alpha footprint",
        "",
    ]
    for r in results:
        lines.append(
            f"{r['family']}: {r['kind']} {r['frames']} frames <- {r['source_contract']}"
        )
    for family, filename, reason in missing:
        lines.append(f"{family}: SKIPPED missing source contract {filename}")
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"generated {len(results)} structural families; skipped {len(missing)}")
    print(f"manifest: {manifest}")
    print(f"QA sheets: {qa_root}")


if __name__ == "__main__":
    main()
