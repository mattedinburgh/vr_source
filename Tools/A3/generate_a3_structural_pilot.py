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


def _is_overhead_asset(path: Path) -> bool:
    # TileEngine/overhead map.cpp explicitly loads TILESETS\\<id>\\T\\...
    # for the small overview renderer.  Those silhouettes are not valid
    # contracts for tactical rendering.
    return any(part.lower() == "t" for part in path.parts)


def _contract_signature(frames, meta) -> str:
    h = hashlib.sha256()
    for frame, m in zip(frames, meta):
        rgba = frame.convert("RGBA")
        h.update(struct.pack(
            "<HHhh",
            rgba.width, rgba.height,
            int(m["offset_x"]), int(m["offset_y"])
        ))
        h.update(hashlib.sha256(rgba.getchannel("A").tobytes()).digest())
    return h.hexdigest()


def resolve_source(tilesets_root: Path, filename: str) -> Path:
    # Prefer an actual A3 tactical asset if one exists.
    preferred = tilesets_root / "38" / filename
    if preferred.is_file() and not _is_overhead_asset(preferred):
        return preferred

    target = filename.lower()
    matches = sorted(
        p for p in tilesets_root.rglob("*")
        if p.is_file()
        and p.name.lower() == target
        and not _is_overhead_asset(p)
    )
    if not matches:
        raise FileNotFoundError(
            f"{filename} tactical contract not found under {tilesets_root}"
        )

    # Some Vengeance packs contain thumbnail-scale copies outside a literal T
    # directory.  Select by tactical scale, then fail closed if more than one
    # materially different full-size contract remains.
    decoded = []
    for path in matches:
        frames, meta, _ = decode_sti(path)
        total_area = sum(frame.width * frame.height for frame in frames)
        decoded.append((
            total_area,
            _contract_signature(frames, meta),
            path,
        ))

    max_area = max(area for area, _, _ in decoded)
    tactical = [entry for entry in decoded if entry[0] >= max_area * 0.50]
    signatures = {sig for _, sig, _ in tactical}
    if len(signatures) != 1:
        detail = "; ".join(
            f"{path} area={area} sig={sig[:12]}"
            for area, sig, path in tactical
        )
        raise RuntimeError(
            f"{filename}: ambiguous full tactical contracts: {detail}"
        )

    # Deterministic choice among byte/contract-equivalent copies.
    tactical.sort(key=lambda entry: (
        0 if entry[2].parent.name == "50" else 1,
        str(entry[2]).lower(),
    ))
    return tactical[0][2]


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
        ^ ((x & 0xFFFF) * 0x9E3779B1)
        ^ ((y & 0xFFFF) * 0x85EBCA77)
    )


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    return tuple(clamp(round(x * (1.0 - t) + y * t)) for x, y in zip(a, b))


def find_jsd_sibling(source: Path) -> Path | None:
    target = source.stem.lower()
    for p in source.parent.iterdir():
        if p.is_file() and p.stem.lower() == target and p.suffix.lower() == ".jsd":
            return p
    return None


def parse_jsd_semantics(path: Path | None) -> dict[int, dict]:
    """Return frame-indexed gameplay semantics without consulting artwork RGB."""
    if path is None or not path.is_file():
        return {}

    raw = path.read_bytes()
    if len(raw) < 16:
        raise ValueError(f"{path}: truncated JSD header")

    ident, n_images, n_stored, structure_size, file_flags, _, n_locs = struct.unpack_from(
        "<4sHHHB3sH", raw, 0
    )
    if ident != b"J2SD":
        raise ValueError(f"{path}: invalid JSD magic {ident!r}")

    off = 16
    if file_flags & 0x01:
        off += 16 * n_images
        off += 2 * n_locs

    result: dict[int, dict] = {}
    if not (file_flags & 0x02):
        return result

    structure_end = off + structure_size
    if structure_end > len(raw):
        raise ValueError(f"{path}: structure block exceeds file length")

    for _ in range(n_stored):
        if off + 16 > structure_end:
            raise ValueError(f"{path}: truncated DB_STRUCTURE record")
        (
            armour, hit_points, density, tile_count, flags, number,
            orientation, destruction_partner, partner_delta,
            z_x, z_y, _unused
        ) = struct.unpack_from("<BBBBIH B b b b b B", raw, off)
        off += 16

        tiles = []
        for _tile in range(tile_count):
            if off + 32 > structure_end:
                raise ValueError(f"{path}: truncated DB_STRUCTURE_TILE record")
            s_pos, rel_x, rel_y = struct.unpack_from("<hbb", raw, off)
            shape = raw[off + 4:off + 29]
            tile_flags = raw[off + 29]
            tiles.append({
                "s_pos": s_pos,
                "rel_x": rel_x,
                "rel_y": rel_y,
                "occupied_profile_cells": sum(v != 0 for v in shape),
                "profile_sum": sum(shape),
                "flags": tile_flags,
            })
            off += 32

        result[number] = {
            "flags": flags,
            "orientation": orientation,
            "tile_count": tile_count,
            "destruction_partner": destruction_partner,
            "partner_delta": partner_delta,
            "armour": armour,
            "hit_points": hit_points,
            "density": density,
            "tiles": tiles,
        }

    return result


def coherent_material(
    mask: Image.Image,
    base: tuple[int, int, int],
    family_seed: int,
    offset_x: int,
    offset_y: int,
    *,
    vertical_weather: bool,
    face_bias: int = 0,
    frame_phase: int = 0,
) -> Image.Image:
    """Family-level material field anchored to frame geometry, not legacy RGB."""
    w, h = mask.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    mp = mask.load()

    old_paints = (
        (92, 125, 116),   # desaturated green
        (147, 103, 84),   # old salmon undercoat
        (119, 125, 91),   # olive repaint
        (165, 151, 120),  # faded cream
    )
    old_paint = old_paints[(family_seed >> 7) & 3]

    for y in range(h):
        for x in range(w):
            alpha = mp[x, y]
            if not alpha:
                continue

            wx = x + offset_x
            wy = y + offset_y
            coarse = ((pixel_hash(family_seed, wx // 4, wy // 4) >> 9) & 15) - 7
            fine = ((pixel_hash(family_seed ^ frame_phase, wx, wy) >> 19) & 7) - 3
            delta = coarse + fine // 2 + face_bias
            colour = tuple(clamp(v + delta) for v in base)

            if vertical_weather:
                rel = y / max(1.0, h - 1.0)
                # Humid tropical building: sun-faded top, splashback and algae/dirt
                # at the bottom. The bands are consistent across the whole family.
                colour = mix(colour, (216, 207, 181), max(0.0, 0.11 - rel * 0.08))
                if rel > 0.58:
                    colour = mix(colour, old_paint, 0.045)
                if rel > 0.76:
                    t = (rel - 0.76) / 0.24
                    colour = mix(colour, (67, 72, 58), 0.07 + 0.15 * t)

            op[x, y] = (*colour, alpha)

    return out


def composite_shape(out: Image.Image, mask: Image.Image, draw_fn) -> None:
    layer = Image.new("RGBA", out.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    draw_fn(d)
    la = layer.getchannel("A")
    clipped = Image.new("L", out.size, 0)
    cp, lp, mp = clipped.load(), la.load(), mask.load()
    for y in range(out.height):
        for x in range(out.width):
            cp[x, y] = (lp[x, y] * mp[x, y]) // 255
    layer.putalpha(clipped)
    out.alpha_composite(layer)


def add_contract_edges(out: Image.Image, mask: Image.Image) -> None:
    """Give alpha geometry readable sun/shadow edges without legacy-pixel cues."""
    w, h = mask.size
    mp = mask.load()
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    lp = layer.load()
    for y in range(h):
        for x in range(w):
            if not mp[x, y]:
                continue
            left_open = x == 0 or not mp[x - 1, y]
            up_open = y == 0 or not mp[x, y - 1]
            right_open = x == w - 1 or not mp[x + 1, y]
            down_open = y == h - 1 or not mp[x, y + 1]
            if left_open or up_open:
                lp[x, y] = (226, 216, 190, min(54, mp[x, y]))
            elif right_open or down_open:
                lp[x, y] = (45, 45, 40, min(58, mp[x, y]))
    out.alpha_composite(layer)


STRUCTURE_OPEN = 0x00000002
STRUCTURE_WALL = 0x00010000
STRUCTURE_WALLNWINDOW = 0x00020000


def render_wall_aux(mask: Image.Image, family: str, index: int) -> Image.Image:
    """Non-JSD wall-family frames are auxiliary/shadow shapes, not facades."""
    seed = seed32("a3-wall-aux", family)
    w, h = mask.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op, mp = out.load(), mask.load()
    for y in range(h):
        for x in range(w):
            a = mp[x, y]
            if not a:
                continue
            n = ((pixel_hash(seed, x // 3, y // 3) >> 14) & 7) - 3
            # Neutral, slightly warm shadow/intensity art. No old pixel colour.
            c = clamp(54 + n)
            op[x, y] = (c, clamp(c - 2), clamp(c - 7), a)
    out.putalpha(mask.copy())
    return out


def draw_window(
    out: Image.Image,
    mask: Image.Image,
    orientation: int,
    is_open: bool,
    family_seed: int,
    variant: int,
) -> None:
    w, h = mask.size
    if w < 12 or h < 24:
        return

    x0, x1 = int(w * 0.23), int(w * 0.77)
    y0, y1 = int(h * 0.29), int(h * 0.64)
    skew = max(1, w // 12)

    if orientation in (2, 4):
        poly = [(x0 + skew, y0), (x1, y0 + 2), (x1 - skew, y1), (x0, y1 - 2)]
    else:
        poly = [(x0, y0 + 2), (x1 - skew, y0), (x1, y1 - 2), (x0 + skew, y1)]

    frame_colour = (77, 67, 52, 235)
    sill_colour = (183, 170, 140, 175)
    interior = (22, 27, 25, 245) if is_open else (44, 67, 67, 225)

    def window_layer(d: ImageDraw.ImageDraw) -> None:
        d.polygon(poly, fill=interior)
        d.line(poly + [poly[0]], fill=frame_colour, width=2)
        # A mid-century rural frame: simple, practical construction.
        mx = sum(p[0] for p in poly) // 4
        my0 = (poly[0][1] + poly[1][1]) // 2 + 2
        my1 = (poly[2][1] + poly[3][1]) // 2 - 1
        if not is_open:
            if variant == 1:
                # Second authored window variant: welded security bars are common
                # on modest rural/commercial buildings. Keep them sparse enough
                # not to turn the opening into visual noise at JA2 scale.
                left = min(p[0] for p in poly) + 3
                right = max(p[0] for p in poly) - 3
                top = max(0, min(p[1] for p in poly) + 2)
                bottom = min(h - 1, max(p[1] for p in poly) - 2)
                for bx in (left, (left + right) // 2, right):
                    d.line((bx, top, bx, bottom),
                           fill=(58, 58, 52, 205), width=1)
                d.line((left, (top + bottom) // 2, right, (top + bottom) // 2),
                       fill=(58, 58, 52, 180), width=1)
            else:
                d.line((mx, my0, mx, my1), fill=(112, 103, 82, 190), width=1)
                # Muted sky reflection is newly authored and intentionally subtle.
                d.line((poly[0][0] + 2, poly[0][1] + 3,
                        poly[1][0] - 2, poly[1][1] + 4),
                       fill=(128, 154, 149, 85), width=1)
        else:
            # JSD says this is the open partner. Give it a visibly open dark void
            # plus a narrow hinged/shutter edge without changing the alpha contract.
            side_x = min(p[0] for p in poly) + 2 if orientation in (1, 4) else max(p[0] for p in poly) - 2
            d.line((side_x, my0, side_x, my1),
                   fill=(103, 78, 57, 220), width=2)
        d.line((poly[3][0], poly[3][1] + 1,
                poly[2][0], poly[2][1] + 1),
               fill=sill_colour, width=1)

    composite_shape(out, mask, window_layer)


def render_wall(
    mask: Image.Image,
    base: tuple[int, int, int],
    family: str,
    index: int,
    flavour: str,
    meta: dict,
    semantic: dict | None,
) -> Image.Image:
    if semantic is None:
        return render_wall_aux(mask, family, index)

    orientation = int(semantic.get("orientation", 0))
    # Soft directional separation only; JA2's actual light/shade system remains
    # authoritative. This just prevents every facade orientation looking flat.
    face_bias = {1: 3, 2: -4, 3: -1, 4: 4}.get(orientation, 0)
    family_seed = seed32("a3-wall-family", family)

    out = coherent_material(
        mask, base, family_seed,
        int(meta["offset_x"]), int(meta["offset_y"]),
        vertical_weather=True,
        face_bias=face_bias,
        frame_phase=0,
    )

    flags = int(semantic.get("flags", 0))
    if flags & STRUCTURE_WALLNWINDOW:
        window_variant = (index - 35) % 3 if 35 <= index <= 46 else 0
        draw_window(
            out, mask, orientation,
            bool(flags & STRUCTURE_OPEN),
            family_seed,
            window_variant,
        )

    if int(semantic.get("tile_count", 1)) > 1:
        # Multi-tile wall/corner pieces need a readable construction seam.
        w, h = mask.size
        x = int(w * (0.63 if orientation in (1, 4) else 0.37))

        def corner_seam(d: ImageDraw.ImageDraw) -> None:
            d.line((x, max(1, h // 12), x, h - 2), fill=(50, 48, 42, 72), width=1)
            if x + 1 < w:
                d.line((x + 1, max(1, h // 12), x + 1, h - 2),
                       fill=(219, 205, 174, 45), width=1)

        composite_shape(out, mask, corner_seam)

    add_contract_edges(out, mask)
    out.putalpha(mask.copy())
    return out


def render_floor(
    mask: Image.Image,
    base: tuple[int, int, int],
    family: str,
    index: int,
    flavour: str,
    meta: dict,
) -> Image.Image:
    family_seed = seed32("a3-floor-family", family)
    out = coherent_material(
        mask, base, family_seed,
        int(meta["offset_x"]), int(meta["offset_y"]),
        vertical_weather=False,
        # Floors are continuous surfaces. Never add per-frame material phase:
        # repeated tiles must share one world/geometry-anchored field.
        frame_phase=0,
    )

    w, h = mask.size
    ox, oy = int(meta["offset_x"]), int(meta["offset_y"])
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    lp, mp = layer.load(), mask.load()

    for y in range(h):
        for x in range(w):
            if not mp[x, y]:
                continue
            wx, wy = x + ox, y + oy
            if flavour == "terracotta":
                # Isometric ceramic/terracotta joints, geometry-anchored.
                mortar = ((wx + 2 * wy) % 10 == 0) or ((2 * wx - wy) % 18 == 0)
                if mortar:
                    lp[x, y] = (78, 62, 51, 62)
            else:
                # Broad concrete trowel/joint traces. Very low contrast so floors
                # stay readable under items and mercs.
                joint = ((wx + 2 * wy) % 29 == 0)
                if joint:
                    lp[x, y] = (72, 69, 61, 34)

    layer.putalpha(Image.eval(layer.getchannel("A"), lambda a: a))
    # Clip overlay alpha to the original floor silhouette.
    la = layer.getchannel("A")
    cp = Image.new("L", (w, h), 0)
    cpp, lap, mpp = cp.load(), la.load(), mask.load()
    for y in range(h):
        for x in range(w):
            cpp[x, y] = (lap[x, y] * mpp[x, y]) // 255
    layer.putalpha(cp)
    out.alpha_composite(layer)

    # Do not outline each floor sprite: that exposes the engine tile grid.
    out.putalpha(mask.copy())
    return out


def render_roof(
    mask: Image.Image,
    base: tuple[int, int, int],
    family: str,
    index: int,
    flavour: str,
    meta: dict,
    semantic: dict | None,
) -> Image.Image:
    family_seed = seed32("a3-roof-family", family)
    out = coherent_material(
        mask, base, family_seed,
        int(meta["offset_x"]), int(meta["offset_y"]),
        vertical_weather=False,
        frame_phase=0,
    )

    w, h = mask.size
    ox, oy = int(meta["offset_x"]), int(meta["offset_y"])
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    lp, mp = layer.load(), mask.load()

    for y in range(h):
        for x in range(w):
            if not mp[x, y]:
                continue
            wx, wy = x + ox, y + oy

            if flavour == "tile":
                # Sun-aged clay tile courses. Both axes are screen-isometric so
                # the pattern follows the slanted roof rather than the sprite box.
                course = (wy % 7 == 0)
                joint = ((wx + 2 * wy) % 12 == 0)
                if course:
                    lp[x, y] = (91, 58, 45, 72)
                elif joint:
                    lp[x, y] = (105, 66, 49, 42)
            else:
                # Galvanised/corrugated farm roofing. Ribs and broad sheet seams
                # are geometry-anchored and therefore stable across all frames.
                rib = (wx + 2 * wy) % 6
                seam = (wx + 2 * wy) % 30
                if seam in (0, 1):
                    lp[x, y] = (87, 61, 48, 92)
                elif rib == 0:
                    lp[x, y] = (54, 57, 54, 82)
                elif rib == 1:
                    lp[x, y] = (193, 183, 157, 36)

    la = layer.getchannel("A")
    cp = Image.new("L", (w, h), 0)
    cpp, lap, mpp = cp.load(), la.load(), mask.load()
    for y in range(h):
        for x in range(w):
            cpp[x, y] = (lap[x, y] * mpp[x, y]) // 255
    layer.putalpha(cp)
    out.alpha_composite(layer)

    # Sun bleaching along the upper-facing roof edge is family-wide, not a
    # per-frame repair decal.
    def roof_edge(d: ImageDraw.ImageDraw) -> None:
        y = max(0, h // 8)
        d.line((0, y, w, y), fill=(224, 207, 170, 34), width=max(1, h // 32))

    composite_shape(out, mask, roof_edge)
    add_contract_edges(out, mask)
    out.putalpha(mask.copy())
    return out


def validate_generated_contract(
    family: str,
    source: Path,
    legacy_frames: list[Image.Image],
    generated: list[Image.Image],
    meta: list[dict],
    semantics: dict[int, dict],
) -> dict:
    """Fail closed if authored art changes any tactical sprite contract."""
    if len(generated) != len(legacy_frames):
        raise ValueError(
            f"{family}: frame count changed {len(legacy_frames)} -> {len(generated)}"
        )
    if len(meta) != len(legacy_frames):
        raise ValueError(
            f"{family}: metadata/frame mismatch meta={len(meta)} frames={len(legacy_frames)}"
        )

    alpha_hash = hashlib.sha256()
    opaque_pixels = 0
    for i, (legacy, authored) in enumerate(zip(legacy_frames, generated)):
        legacy_rgba = legacy.convert("RGBA")
        authored_rgba = authored.convert("RGBA")
        if authored_rgba.size != legacy_rgba.size:
            raise ValueError(
                f"{family} frame {i}: dimensions changed "
                f"{legacy_rgba.size} -> {authored_rgba.size}"
            )

        legacy_alpha = legacy_rgba.getchannel("A").tobytes()
        authored_alpha = authored_rgba.getchannel("A").tobytes()
        if authored_alpha != legacy_alpha:
            raise ValueError(f"{family} frame {i}: alpha footprint changed")

        alpha_hash.update(authored_alpha)
        opaque_pixels += sum(a != 0 for a in authored_alpha)

    bad_semantic_frames = sorted(i for i in semantics if i < 0 or i >= len(generated))
    if bad_semantic_frames:
        raise ValueError(
            f"{family}: JSD references out-of-range frame(s) {bad_semantic_frames} "
            f"for {len(generated)} images"
        )

    return {
        "contract_signature": _contract_signature(legacy_frames, meta),
        "alpha_sha256": alpha_hash.hexdigest(),
        "opaque_pixels": opaque_pixels,
        "semantic_frames": len(semantics),
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    }


def file_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for block in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


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


def contact_sheet(
    path: Path,
    family: str,
    frames: list[Image.Image],
    labels: list[str] | None = None,
) -> None:
    if not frames:
        return
    cols = 5
    cell_w = max(112, max(f.width for f in frames) + 20)
    cell_h = max(104, max(f.height for f in frames) + 34)
    rows = math.ceil(len(frames) / cols)
    sheet = Image.new("RGBA", (cols * cell_w, rows * cell_h), (36, 36, 36, 255))
    d = ImageDraw.Draw(sheet)

    for i, frame in enumerate(frames):
        cx = (i % cols) * cell_w
        cy = (i // cols) * cell_h
        d.rectangle((cx, cy, cx + cell_w - 1, cy + cell_h - 1),
                    outline=(80, 80, 80, 255))
        x = cx + (cell_w - frame.width) // 2
        y = cy + 24 + (cell_h - 30 - frame.height) // 2
        sheet.alpha_composite(frame, (x, y))
        label = labels[i] if labels and i < len(labels) else ""
        title = f"{family} #{i+1}" + (f" {label}" if label else "")
        d.text((cx + 4, cy + 3), title, fill=(230, 230, 230, 255))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(path, "PNG")


def generate_family(tilesets_root: Path, out_root: Path, qa_root: Path,
                    family: str, filename: str, base: tuple[int, int, int],
                    flavour: str, kind: str) -> dict:
    source = resolve_source(tilesets_root, filename)
    legacy_frames, meta, _ = decode_sti(source)
    if not legacy_frames:
        raise ValueError(f"{source}: no frames")

    jsd_path = find_jsd_sibling(source) if kind in ("wall", "roof") else None
    if kind in ("wall", "roof") and jsd_path is None:
        raise FileNotFoundError(
            f"{family}: structural family has no canonical JSD sibling for {source}"
        )
    semantics = parse_jsd_semantics(jsd_path)
    generated = []
    labels = []

    for i, legacy in enumerate(legacy_frames):
        mask = legacy.convert("RGBA").getchannel("A")
        semantic = semantics.get(i)

        if kind == "wall":
            frame = render_wall(mask, base, family, i, flavour, meta[i], semantic)
            if semantic is None:
                label = "aux/shadow"
            else:
                flags = int(semantic.get("flags", 0))
                if flags & STRUCTURE_WALLNWINDOW:
                    if flags & STRUCTURE_OPEN:
                        label = "window-open"
                    else:
                        window_variant = (i - 35) % 3 if 35 <= i <= 46 else 0
                        label = "window-barred" if window_variant == 1 else "window-glazed"
                elif int(semantic.get("tile_count", 1)) > 1:
                    label = f"wall-corner o{semantic.get('orientation', 0)}"
                else:
                    label = f"wall o{semantic.get('orientation', 0)}"
        elif kind == "floor":
            frame = render_floor(mask, base, family, i, flavour, meta[i])
            label = flavour
        else:
            frame = render_roof(mask, base, family, i, flavour, meta[i], semantic)
            label = "slanted-roof" if semantic else flavour

        generated.append(frame)
        labels.append(label)

    contract = validate_generated_contract(
        family, source, legacy_frames, generated, meta, semantics
    )

    out = out_root / f"VR_A3_{family}.b1tc"
    write_b1tc(out, generated, meta)
    artifact_sha256 = file_sha256(out)
    qa_path = qa_root / f"VR_A3_{family}.png"
    contact_sheet(qa_path, family, generated, labels)

    semantic_counts = {}
    for label in labels:
        semantic_counts[label] = semantic_counts.get(label, 0) + 1

    return {
        "family": family,
        "kind": kind,
        "source_contract": str(source),
        "jsd_contract": str(jsd_path) if jsd_path else "",
        "semantic_counts": semantic_counts,
        "frames": len(generated),
        "output": str(out),
        "qa": str(qa_path),
        "artifact_sha256": artifact_sha256,
        **contract,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--out-root", required=True, type=Path)
    ap.add_argument("--qa-root", type=Path)
    ap.add_argument(
        "--strict",
        action="store_true",
        help="production gate: fail if any declared structural family cannot be generated",
    )
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
        "Contract verification: REQUIRED before artifact write",
        f"Production strict mode: {'YES' if ns.strict else 'NO'}",
        "",
    ]
    for r in results:
        jsd_note = f" jsd={r['jsd_contract']}" if r.get("jsd_contract") else ""
        sem_note = f" semantics={r['semantic_counts']}" if r.get("semantic_counts") else ""
        lines.append(
            f"{r['family']}: {r['kind']} {r['frames']} frames <- "
            f"{r['source_contract']}{jsd_note}{sem_note} "
            f"contract={r['contract_signature'][:16]} "
            f"alpha={r['alpha_sha256'][:16]} "
            f"artifact={r['artifact_sha256'][:16]}"
        )
    for family, filename, reason in missing:
        lines.append(f"{family}: SKIPPED missing source contract {filename}")
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if ns.strict and missing:
        detail = ", ".join(family for family, _, _ in missing)
        raise SystemExit(
            f"A3 production gate FAILED: missing canonical contracts for {detail}"
        )

    print(f"generated {len(results)} structural families; skipped {len(missing)}")
    print("contract verification: PASS")
    print(f"manifest: {manifest}")
    print(f"QA sheets: {qa_root}")


if __name__ == "__main__":
    main()
