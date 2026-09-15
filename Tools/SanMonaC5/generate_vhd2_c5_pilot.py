#!/usr/bin/env python3
"""
San Mona C5 native VHD2 structural pilot generator.

ART CONTRACT
------------
The legacy STI is *not* source art.  It is read only to preserve:
  - frame count/order;
  - transparent footprint;
  - frame dimensions and offsets;
  - coarse large-scale semantic darkness where a wall family needs to retain
    a doorway/window/trim zone.

Every visible RGB pixel in the VHD2 output is newly authored by this generator.
No legacy RGB pixel is copied, enlarged, sharpened or texture-overlaid.

The material language is intentionally specific to San Mona C5:
dry late-20th-century Latin-American commercial/vice strip, sun-faded stucco,
failed render exposing brick, patched corrugated roofing, improvised repairs,
dusty municipal paving, vehicle/oil history and neglected maintenance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageChops
from sti_contract import read_sti


# One structural family should read as one construction/material family.
# Do not randomly recolour every frame: adjacent pieces must stitch visually.
FAMILY_BASE = {
    "BUILD_24": (184, 148, 88),      # faded ochre stucco
    "BUILD_21": (78, 132, 132),      # oxidised/faded turquoise
    "BUILD_01": (171, 109, 92),      # dusty salmon
    "BUILD_06": (183, 174, 142),     # dirty cream
    "SLANT_12": (126, 118, 103),      # sun-dulled galvanised corrugated roofing
    "FLAT_R1": (103, 91, 76),
    "FLAT_R2": (109, 91, 74),
    "FLAT_R3": (97, 86, 74),
    "COBBLE_ROAD": (111, 101, 85),
    "COBBLE_ROAD_PIECES": (111, 101, 85),
    "PAVE_B": (143, 129, 105),
    "SGRASS1": (142, 119, 80),
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


def fast_hash32(v: int) -> int:
    v &= 0xFFFFFFFF
    v ^= v >> 16
    v = (v * 0x7FEB352D) & 0xFFFFFFFF
    v ^= v >> 15
    v = (v * 0x846CA68B) & 0xFFFFFFFF
    v ^= v >> 16
    return v & 0xFFFFFFFF


def pixel_hash(seed: int, x: int, y: int) -> int:
    return fast_hash32(
        seed
        ^ ((x * 0x045D9F3B) & 0xFFFFFFFF)
        ^ ((y * 0x119DE1F3) & 0xFFFFFFFF)
    )


def clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def mix(a, b, t):
    return tuple(
        clamp(int(round(a[i] * (1.0 - t) + b[i] * t)))
        for i in range(3)
    )


def alpha_mask_2x(src: Image.Image) -> Image.Image:
    """Nearest-neighbour 2x alpha is the exact authored sprite footprint."""
    return src.getchannel("A").resize(
        (src.width * 2, src.height * 2),
        Image.Resampling.NEAREST,
    )


def large_semantic_dark_mask(src: Image.Image) -> Image.Image:
    """
    Preserve only broad dark semantic zones (door/window/trim masses).

    Heavy blur deliberately suppresses the tiny legacy texture/stripe detail that
    made the first pilot look like a recolour.  The output is a coarse mask, not
    legacy art.
    """
    rgb = src.convert("RGB")
    gray = rgb.convert("L")
    # Blur in legacy resolution, then scale.  Radius tracks frame size.
    radius = max(1.4, min(src.width, src.height) / 14.0)
    gray = gray.filter(ImageFilter.GaussianBlur(radius=radius))
    gray = gray.resize((src.width * 2, src.height * 2), Image.Resampling.BILINEAR)

    # Only retain genuinely dark *large* areas.
    dark = gray.point(lambda p: 255 if p < 49 else 0, mode="L")
    # Majority-like smoothing: close small holes, discard isolated narrow stripes.
    dark = dark.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(7))
    return dark


def material_noise(base, seed, x, y, strength=8):
    n = pixel_hash(seed, x // 2, y // 2)
    delta = int(((n >> 12) & 31) - 15) * strength // 15
    return tuple(clamp(v + delta) for v in base)


def apply_masked_colour(img: Image.Image, mask: Image.Image, colour, opacity: int):
    overlay = Image.new("RGBA", img.size, (*colour, opacity))
    clipped = ImageChops.multiply(mask, img.getchannel("A"))
    overlay.putalpha(ImageChops.multiply(clipped, Image.new("L", img.size, opacity)))
    img.alpha_composite(overlay)


def jagged_ellipse_mask(size, cx, cy, rx, ry, seed, roughness=0.16):
    """Low-frequency irregular blob used for contiguous failed-stucco patches."""
    w, h = size
    mask = Image.new("L", size, 0)
    p = mask.load()
    for y in range(max(0, cy - ry - 3), min(h, cy + ry + 4)):
        for x in range(max(0, cx - rx - 3), min(w, cx + rx + 4)):
            if rx <= 0 or ry <= 0:
                continue
            dx = (x - cx) / float(rx)
            dy = (y - cy) / float(ry)
            d = dx * dx + dy * dy
            # Block noise keeps the edge irregular without pepper noise.
            n = pixel_hash(seed, x // 6, y // 6)
            edge = 1.0 + ((((n >> 8) & 255) / 255.0) - 0.5) * roughness * 2.0
            if d <= edge:
                p[x, y] = 255
    return mask.filter(ImageFilter.GaussianBlur(0.55))


def render_wall(src: Image.Image, family: str, index: int) -> Image.Image:
    w2, h2 = src.width * 2, src.height * 2
    alpha = alpha_mask_2x(src)
    dark = large_semantic_dark_mask(src)
    base = FAMILY_BASE.get(family, (184, 148, 88))
    family_seed = h32("wall-family", family)
    frame_seed = fast_hash32(family_seed ^ (index * 0x9E3779B9))

    out = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    px = out.load()
    a_px = alpha.load()
    d_px = dark.load()

    # Broad old repaint band: a construction-history cue, not a random texture.
    old_paints = [(71, 125, 127), (158, 93, 78), (116, 130, 87), (177, 163, 128)]
    old_paint = old_paints[(family_seed >> 7) % len(old_paints)]
    band_y = int(h2 * (0.55 + ((family_seed >> 16) & 15) / 100.0))

    for y in range(h2):
        for x in range(w2):
            if a_px[x, y] < 128:
                continue

            n = pixel_hash(family_seed, x // 3, y // 3)
            c = material_noise(base, family_seed, x, y, 7)

            # Sun bleaching on exposed upper plaster.
            bleach = max(0.0, 0.16 - (y / max(1.0, h2)) * 0.07)
            c = mix(c, (211, 198, 169), bleach)

            # A faint earlier paint layer survives near the lower facade.
            if y >= band_y and ((n >> 21) & 7) < 5:
                c = mix(c, old_paint, 0.10)

            # Lower-wall dust / splashback / street grime is broad and continuous.
            if y > int(h2 * 0.73):
                t = (y - h2 * 0.73) / max(1.0, h2 * 0.27)
                c = mix(c, (74, 64, 50), 0.10 + 0.18 * t)

            # Broad semantic darkness only: door/window/trim mass survives without
            # carrying legacy micro-detail.
            if d_px[x, y] > 128:
                c = mix(c, (38, 45, 43), 0.72)

            px[x, y] = (*c, 255)

    # One or two contiguous plaster-failure patches on selected frames.
    # Patch placement is broad enough to read as history, not red confetti.
    brick_mode = (frame_seed % 7) in (1, 4, 6)
    patch_count = (1 + (1 if (frame_seed & 31) == 0 and w2 > 34 else 0)) if brick_mode else 0
    for pidx in range(patch_count):
        ps = fast_hash32(frame_seed ^ (pidx * 0xA511E9B3))
        rx = max(4, w2 // (5 + (ps & 1)))
        ry = max(5, h2 // (5 + ((ps >> 2) & 1)))
        cx = rx + ((ps >> 6) % max(1, w2 - 2 * rx))
        cy_min = max(ry, int(h2 * 0.34))
        cy = cy_min + ((ps >> 14) % max(1, h2 - cy_min - ry))
        patch = jagged_ellipse_mask((w2, h2), cx, cy, rx, ry, ps)
        patch = ImageChops.multiply(patch, alpha)
        # Avoid covering large semantic dark zones.
        patch = ImageChops.subtract(patch, dark)

        brick = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
        bp = brick.load()
        pm = patch.load()
        mortar = (94, 73, 59)
        for y in range(h2):
            row = y // 5
            shift = 5 if (row & 1) else 0
            for x in range(w2):
                if pm[x, y] < 48:
                    continue
                bx = (x + shift) % 10
                by = y % 5
                n = pixel_hash(ps, x // 3, y // 3)
                if by == 0 or bx == 0:
                    c = mortar
                else:
                    c = (
                        134 + ((n >> 4) & 15),
                        66 + ((n >> 12) & 9),
                        46 + ((n >> 20) & 8),
                    )
                bp[x, y] = (*c, pm[x, y])
        out.alpha_composite(brick)

    # Some walls were patched rather than left with exposed masonry.  Use a
    # broad, irregular cement/plaster repair on a different subset of frames so
    # the whole wall family does not look stamped from one damage decal.
    if not brick_mode and (frame_seed % 5) in (0, 2) and w2 > 14 and h2 > 14:
        rs = fast_hash32(frame_seed ^ 0x6A09E667)
        rrx = max(4, w2 // 6)
        rry = max(5, h2 // 6)
        rcx = rrx + ((rs >> 5) % max(1, w2 - 2 * rrx))
        rcy = max(rry, int(h2 * 0.35)) + ((rs >> 13) % max(1, h2 - max(rry, int(h2 * 0.35)) - rry))
        repair_mask = jagged_ellipse_mask((w2, h2), rcx, rcy, rrx, rry, rs, 0.13)
        repair_mask = ImageChops.multiply(repair_mask, alpha)
        repair_mask = ImageChops.subtract(repair_mask, dark)
        repair_colour = (170, 164, 148) if (rs & 1) else (154, 149, 135)
        repair = Image.new("RGBA", out.size, (*repair_colour, 0))
        repair.putalpha(repair_mask.point(lambda q: int(q * 0.74)))
        out.alpha_composite(repair)

    # Sparse *continuous* cracks: short hairline paths rather than speckles.
    draw_layer = Image.new("RGBA", out.size, (0, 0, 0, 0))
    dr = ImageDraw.Draw(draw_layer)
    crack_count = 1 if h2 >= 20 else 0
    for cidx in range(crack_count):
        cs = fast_hash32(frame_seed ^ 0xC2B2AE35 ^ cidx)
        x = int((cs >> 4) % max(1, w2))
        y = int(h2 * (0.18 + ((cs >> 12) & 31) / 100.0))
        pts = [(x, y)]
        for step in range(4):
            x += [-2, -1, 1, 2][(cs >> (step * 2)) & 3]
            y += 3 + ((cs >> (18 + step)) & 1)
            pts.append((x, y))
        dr.line(pts, fill=(64, 57, 49, 90), width=1)

    crack_layer_alpha = ImageChops.multiply(draw_layer.getchannel("A"), alpha)
    draw_layer.putalpha(crack_layer_alpha)
    out.alpha_composite(draw_layer)
    out.putalpha(alpha)
    return out


def cobble_colour(base, seed, x, y):
    # Isometric-ish staggered block pattern.  Main pattern is family-global so
    # repeated road pieces share the same material grammar.
    row_h = 7
    stone_w = 13
    row = y // row_h
    yy = y % row_h
    shift = (stone_w // 2) if row & 1 else 0
    xx = (x + shift) % stone_w

    cell_x = (x + shift) // stone_w
    cell_y = row
    cell_hash = pixel_hash(seed, cell_x, cell_y)
    delta = ((cell_hash >> 9) & 31) - 15
    c = tuple(clamp(v + delta // 2) for v in base)

    # Mortar/recessed joints.
    if yy <= 1 or xx <= 1:
        c = mix(c, (58, 55, 50), 0.54)
    elif yy == row_h - 1:
        c = mix(c, (74, 69, 60), 0.28)

    # Worn stone centre and dusty mortar edges.
    n = pixel_hash(seed ^ 0x517CC1B7, x // 2, y // 2)
    if (n & 255) < 18:
        c = mix(c, (174, 148, 105), 0.20)
    return c


def render_road(src: Image.Image, family: str, index: int) -> Image.Image:
    w2, h2 = src.width * 2, src.height * 2
    alpha = alpha_mask_2x(src)
    a = alpha.load()
    base = FAMILY_BASE[family]
    seed = h32("san-mona-cobble", family)
    fs = fast_hash32(seed ^ (index * 0x9E3779B9))

    out = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    p = out.load()

    # Large repair/oil zone, intentionally frame-sparse.
    patch_on = (fs & 7) in (0, 3)
    rx = max(7, w2 // 5)
    ry = max(4, h2 // 5)
    cx = rx + ((fs >> 5) % max(1, w2 - 2 * rx))
    cy = ry + ((fs >> 13) % max(1, h2 - 2 * ry))

    for y in range(h2):
        for x in range(w2):
            if a[x, y] < 128:
                continue
            c = cobble_colour(base, seed, x, y)

            # Dust accumulates toward visual outer edges of the sprite.
            edge = min(x, y, w2 - 1 - x, h2 - 1 - y)
            if edge < 4:
                c = mix(c, (166, 139, 96), 0.24 * (4 - edge) / 4.0)

            if patch_on:
                dx = (x - cx) / float(rx)
                dy = (y - cy) / float(ry)
                if dx * dx + dy * dy < 1.0:
                    c = mix(c, (45, 43, 39), 0.25)

            p[x, y] = (*c, 255)

    # One coherent asphalt/concrete patch on a minority of frames.
    if (fs & 15) == 2 and w2 > 20 and h2 > 12:
        patch = jagged_ellipse_mask(
            (w2, h2), cx, cy, max(6, rx), max(4, ry), fs ^ 0xD1B54A32, 0.11
        )
        patch = ImageChops.multiply(patch, alpha)
        repair = Image.new("RGBA", (w2, h2), (58, 57, 54, 0))
        repair.putalpha(patch.point(lambda q: int(q * 0.72)))
        out.alpha_composite(repair)

    out.putalpha(alpha)
    return out


def render_pave(src: Image.Image, family: str, index: int) -> Image.Image:
    w2, h2 = src.width * 2, src.height * 2
    alpha = alpha_mask_2x(src)
    a = alpha.load()
    base = FAMILY_BASE[family]
    seed = h32("san-mona-pave", family)
    fs = fast_hash32(seed ^ index)

    out = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    p = out.load()

    # Large slab divisions, subtle aggregate, corner grime.
    sx = max(12, w2 // 2)
    sy = max(8, h2 // 2)
    for y in range(h2):
        for x in range(w2):
            if a[x, y] < 128:
                continue
            c = material_noise(base, seed, x, y, 6)
            if (x % sx) <= 1 or (y % sy) <= 1:
                c = mix(c, (80, 76, 68), 0.32)
            n = pixel_hash(seed ^ 0x85EBCA6B, x // 2, y // 2)
            if (n & 511) < 10:
                c = mix(c, (84, 77, 65), 0.26)
            edge = min(x, y, w2 - 1 - x, h2 - 1 - y)
            if edge < 3:
                c = mix(c, (121, 101, 73), 0.18)
            p[x, y] = (*c, 255)

    # Broad faded/stained zone on selected slabs.  This breaks the repeated
    # "perfect diamond with a cross" look while keeping seams and footprint exact.
    if (fs & 7) in (1, 5) and w2 > 12 and h2 > 8:
        pcx = max(4, min(w2 - 4, int(w2 * (0.35 + ((fs >> 8) & 15) / 60.0))))
        pcy = max(3, min(h2 - 3, int(h2 * (0.38 + ((fs >> 16) & 7) / 40.0))))
        pmask = jagged_ellipse_mask((w2, h2), pcx, pcy, max(4, w2 // 6), max(3, h2 // 5), fs ^ 0xBB67AE85, 0.10)
        pmask = ImageChops.multiply(pmask, alpha)
        stain = Image.new("RGBA", out.size, (91, 82, 68, 0))
        stain.putalpha(pmask.point(lambda q: int(q * 0.24)))
        out.alpha_composite(stain)

    # One hairline crack or cement repair, not a cross on every tile.
    dl = Image.new("RGBA", out.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(dl)
    if (fs & 3) != 0:
        x0 = int(w2 * (0.22 + ((fs >> 3) & 15) / 50.0))
        y0 = max(1, int(h2 * 0.15))
        pts = [
            (x0, y0),
            (x0 - 2, int(h2 * 0.38)),
            (x0 + 1, int(h2 * 0.58)),
            (x0 - 1, int(h2 * 0.83)),
        ]
        d.line(pts, fill=(64, 61, 55, 92), width=1)
    dl.putalpha(ImageChops.multiply(dl.getchannel("A"), alpha))
    out.alpha_composite(dl)
    out.putalpha(alpha)
    return out


def render_roof(src: Image.Image, family: str, index: int) -> Image.Image:
    w2, h2 = src.width * 2, src.height * 2
    alpha = alpha_mask_2x(src)
    a = alpha.load()
    base = FAMILY_BASE.get(family, (113, 86, 67))
    family_seed = h32("san-mona-roof", family)
    fs = fast_hash32(family_seed ^ (index * 0x9E3779B9))

    out = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    p = out.load()

    # Broad sheet panels.  Corrugation is fine-scale detail within those sheets;
    # rust concentrates at seams/fasteners rather than appearing as random dots.
    panel_w = max(16, min(28, w2 // 3 if w2 >= 48 else 18))
    corr_pitch = 7

    patch_x = int(w2 * (0.18 + ((fs >> 8) & 31) / 90.0))
    patch_y = int(h2 * (0.20 + ((fs >> 15) & 15) / 75.0))
    patch_w = max(10, w2 // 4)
    patch_h = max(8, h2 // 3)
    patch_on = (fs & 7) in (1, 4)

    for y in range(h2):
        for x in range(w2):
            if a[x, y] < 128:
                continue

            panel = x // panel_w
            panel_seed = fast_hash32(family_seed ^ panel * 0x85EBCA6B)
            age = ((panel_seed >> 8) & 31) - 15
            c = tuple(clamp(v + age // 2) for v in base)

            # Real patched roofs mix sheet ages: some panels retain zinc-grey,
            # some are oxidised, and only a minority are heavily rusted.
            panel_age = (panel_seed >> 20) & 7
            if panel_age in (0, 1):
                c = mix(c, (151, 148, 135), 0.34)
            elif panel_age == 2:
                c = mix(c, (99, 105, 101), 0.22)
            elif panel_age == 3:
                c = mix(c, (137, 76, 48), 0.24)

            # Directional sheet ribbing in screen space.
            rib = (x + 2 * y + (family_seed & 7)) % corr_pitch
            if rib == 0:
                c = mix(c, (52, 50, 47), 0.35)
            elif rib == 1:
                c = mix(c, (191, 172, 143), 0.10)

            # Panel seam: darker with rust bleed.
            seam = x % panel_w
            if seam <= 1:
                c = mix(c, (73, 53, 43), 0.48)
            elif seam <= 3:
                c = mix(c, (150, 69, 39), 0.23)

            # Rust streaks descend from sparse fastener lines.
            fastener_col = (panel * panel_w + panel_w // 2)
            dist = abs(x - fastener_col)
            if dist <= 1 and y > (panel_seed % max(1, h2 // 2)):
                c = mix(c, (139, 61, 36), 0.30)

            # Mismatched replacement sheet is a coherent rectangle, not noise.
            if patch_on and patch_x <= x < patch_x + patch_w and patch_y <= y < patch_y + patch_h:
                patch_colours = [(77, 99, 101), (103, 82, 64), (74, 89, 94)]
                pc = patch_colours[(fs >> 22) % len(patch_colours)]
                c = mix(c, pc, 0.66)
                if (x - patch_x) in (0, 1) or (y - patch_y) in (0, 1):
                    c = mix(c, (48, 46, 43), 0.42)

            # Dry dust on lower-facing edge.
            if y > int(h2 * 0.80):
                c = mix(c, (156, 132, 96), 0.09)

            p[x, y] = (*c, 255)

    # Small tar repair on occasional frames.
    if (fs & 15) == 6 and w2 > 18 and h2 > 12:
        cx = max(5, min(w2 - 5, patch_x + patch_w // 2))
        cy = max(4, min(h2 - 4, patch_y + patch_h // 2))
        tar_mask = jagged_ellipse_mask((w2, h2), cx, cy, max(4, patch_w // 3), max(3, patch_h // 4), fs)
        tar_mask = ImageChops.multiply(tar_mask, alpha)
        tar = Image.new("RGBA", out.size, (44, 43, 41, 0))
        tar.putalpha(tar_mask.point(lambda q: int(q * 0.78)))
        out.alpha_composite(tar)

    out.putalpha(alpha)
    return out


def render_ground(src: Image.Image, family: str, index: int) -> Image.Image:
    w2, h2 = src.width * 2, src.height * 2
    alpha = alpha_mask_2x(src)
    a = alpha.load()
    base = FAMILY_BASE.get(family, (142, 119, 80))
    seed = h32("san-mona-ground", family)
    fs = fast_hash32(seed ^ index)
    out = Image.new("RGBA", (w2, h2), (0, 0, 0, 0))
    p = out.load()

    for y in range(h2):
        for x in range(w2):
            if a[x, y] < 128:
                continue
            c = material_noise(base, seed, x, y, 9)
            n = pixel_hash(seed ^ 0x27D4EB2F, x // 2, y // 2)
            if (n & 127) < 18:
                c = mix(c, (98, 91, 64), 0.18)
            # Central San Mona: sparse hardy weeds, not lush farm vegetation.
            if (n & 2047) < 7:
                c = mix(c, (68, 83, 48), 0.58)
            p[x, y] = (*c, 255)

    out.putalpha(alpha)
    return out


def paint_frame(src: Image.Image, family: str, kind: str, index: int) -> Image.Image:
    src = src.convert("RGBA")
    if kind == "wall":
        return render_wall(src, family, index)
    if kind == "road":
        return render_road(src, family, index)
    if kind == "pave":
        return render_pave(src, family, index)
    if kind == "roof":
        return render_roof(src, family, index)
    if kind == "ground":
        return render_ground(src, family, index)
    raise ValueError(f"unsupported structural kind: {kind}")


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
        bg.thumbnail((thumb_w - 8, thumb_h - 8), Image.Resampling.NEAREST)
        x = (i % cols) * thumb_w + (thumb_w - bg.width) // 2
        y = (i // cols) * thumb_h + (thumb_h - bg.height) // 2
        sheet.paste(bg.convert("RGB"), (x, y))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--sti-root",
        required=True,
        help="directory containing canonical STI contract files",
    )
    ap.add_argument("--out-root", required=True)
    ap.add_argument(
        "--families",
        nargs="*",
        default=[
            "COBBLE_ROAD",
            "COBBLE_ROAD_PIECES",
            "PAVE_B",
            "BUILD_24",
            "SLANT_12",
        ],
    )
    args = ap.parse_args()

    sti_root = Path(args.sti_root)
    out_root = Path(args.out_root)
    requested = [x.upper() for x in args.families]

    manifest = {
        "sector": "C5",
        "profile": "San Mona Strip — native VHD2 scratch structural pilot",
        "render_scale": 2,
        "source_rgb_reused": False,
        "source_usage": (
            "technical frame/alpha/offset contract; broad wall semantic darkness only"
        ),
        "art_direction": (
            "dry late-20th-century Latin-American vice/commercial strip; "
            "weathered stucco, exposed brick, patched corrugated roofing, "
            "dusty municipal paving, improvised repair history"
        ),
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
