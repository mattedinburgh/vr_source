#!/usr/bin/env python3
"""
Independent A3 structural-output verifier.

This deliberately does not import the A3 generator. It re-parses STI/JSD/B1TC
contracts independently so the production tool is not grading its own output.

Hard rules:
- no substitute tactical contract when the canonical source is ambiguous/missing;
- frame count, dimensions, offsets and alpha footprint are immutable;
- JSD-backed frames without explicit semantics stay byte-identical RGBA;
- unsupported door semantics fail closed;
- explicit original-art fallback means the override file MUST NOT exist.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from PIL import Image

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from audit_rural_sti import decode_sti  # noqa: E402


WALLS = {
    "BUILD_39": "build_39.sti",
    "BUILD_31": "build_31.sti",
    "BUILD_40": "build_40.sti",
    "BUILD_35": "build_35.sti",
}
FLOORS = {
    "WELFLOR3": "welflor3.sti",
    "P_FLOOR3": "p-floor3.sti",
    "WELFLOR1": "welflor1.sti",
    "WELFLOR2": "welflor2.sti",
}
ROOFS = {
    "W_ROOF1": "w-roof1.sti",
    "SLANT_11": "slant_11.sti",
    "SLANT_13": "slant_13.sti",
}

STRUCTURE_SLIDINGDOOR = 0x00040000
STRUCTURE_DOOR = 0x00080000
STRUCTURE_DDOOR_LEFT = 0x00400000
STRUCTURE_DDOOR_RIGHT = 0x00800000
STRUCTURE_ANYDOOR = (
    STRUCTURE_SLIDINGDOOR
    | STRUCTURE_DOOR
    | STRUCTURE_DDOOR_LEFT
    | STRUCTURE_DDOOR_RIGHT
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def is_overhead(path: Path) -> bool:
    return any(part.lower() == "t" for part in path.parts)


def contract_signature(frames, meta) -> str:
    h = hashlib.sha256()
    for frame, m in zip(frames, meta):
        rgba = frame.convert("RGBA")
        h.update(struct.pack(
            "<HHhh",
            rgba.width, rgba.height,
            int(m["offset_x"]), int(m["offset_y"]),
        ))
        h.update(hashlib.sha256(rgba.getchannel("A").tobytes()).digest())
    return h.hexdigest()


def resolve_source(root: Path, filename: str) -> Path:
    preferred = root / "38" / filename
    if preferred.is_file() and not is_overhead(preferred):
        return preferred

    target = filename.lower()
    candidates = sorted(
        p for p in root.rglob("*")
        if p.is_file() and p.name.lower() == target and not is_overhead(p)
    )
    if not candidates:
        raise FileNotFoundError(filename)

    decoded = []
    for path in candidates:
        frames, meta, _ = decode_sti(path)
        area = sum(frame.width * frame.height for frame in frames)
        decoded.append((area, contract_signature(frames, meta), path))

    max_area = max(area for area, _, _ in decoded)
    tactical = [row for row in decoded if row[0] >= max_area * 0.50]
    signatures = {sig for _, sig, _ in tactical}
    if len(signatures) != 1:
        detail = "; ".join(
            f"{path} area={area} sig={sig[:12]}"
            for area, sig, path in tactical
        )
        raise RuntimeError(
            f"{filename}: ambiguous full tactical contracts: {detail}"
        )

    tactical.sort(key=lambda row: (
        0 if row[2].parent.name == "50" else 1,
        str(row[2]).lower(),
    ))
    return tactical[0][2]


def find_jsd(source: Path) -> Path:
    target = source.stem.lower()
    matches = [
        p for p in source.parent.iterdir()
        if p.is_file() and p.stem.lower() == target and p.suffix.lower() == ".jsd"
    ]
    if len(matches) != 1:
        raise FileNotFoundError(f"unique JSD sibling for {source}")
    return matches[0]


def parse_jsd(path: Path) -> dict[int, int]:
    raw = path.read_bytes()
    if len(raw) < 16:
        raise ValueError(f"{path}: truncated JSD")
    ident, n_images, n_stored, structure_size, file_flags, _, n_locs = struct.unpack_from(
        "<4sHHHB3sH", raw, 0
    )
    if ident != b"J2SD":
        raise ValueError(f"{path}: invalid JSD magic {ident!r}")

    off = 16
    if file_flags & 0x01:
        off += 16 * n_images
        off += 2 * n_locs

    if not (file_flags & 0x02):
        return {}

    structure_end = off + structure_size
    if structure_end > len(raw):
        raise ValueError(f"{path}: structure block exceeds file")

    result: dict[int, int] = {}
    for _ in range(n_stored):
        if off + 16 > structure_end:
            raise ValueError(f"{path}: truncated structure record")
        (
            _armour, _hp, _density, tile_count, flags, number,
            _orientation, _partner, _delta, _zx, _zy, _unused,
        ) = struct.unpack_from("<BBBBIH B b b b b B", raw, off)
        off += 16 + 32 * tile_count
        if off > structure_end:
            raise ValueError(f"{path}: truncated structure tiles")
        result[number] = flags
    return result


def decode_b1tc(path: Path):
    raw = path.read_bytes()
    if len(raw) < 8 or raw[:4] != b"B1TC":
        raise ValueError(f"{path}: invalid B1TC header")
    version, count = struct.unpack_from("<HH", raw, 4)
    if version != 1:
        raise ValueError(f"{path}: unsupported B1TC version {version}")

    table_end = 8 + count * 16
    if table_end > len(raw):
        raise ValueError(f"{path}: truncated frame table")

    frames = []
    meta = []
    expected_payload = table_end
    for i in range(count):
        off = 8 + i * 16
        ox, oy, width, height, payload, length = struct.unpack_from(
            "<hhHHII", raw, off
        )
        if payload != expected_payload:
            raise ValueError(
                f"{path} frame {i}: payload offset {payload} != {expected_payload}"
            )
        expected_length = width * height * 4
        if length != expected_length:
            raise ValueError(
                f"{path} frame {i}: payload length {length} != {expected_length}"
            )
        end = payload + length
        if end > len(raw):
            raise ValueError(f"{path} frame {i}: payload outside file")
        frames.append(Image.frombytes("RGBA", (width, height), raw[payload:end]))
        meta.append({
            "offset_x": ox,
            "offset_y": oy,
            "width": width,
            "height": height,
        })
        expected_payload = end

    if expected_payload != len(raw):
        raise ValueError(f"{path}: trailing bytes after final frame")
    return frames, meta


def verify_family(root: Path, out_root: Path, family: str, filename: str,
                  kind: str, fallbacks: set[str]) -> dict:
    out = out_root / f"VR_A3_{family}.b1tc"
    try:
        source = resolve_source(root, filename)
    except FileNotFoundError:
        if family.upper() not in fallbacks:
            raise
        if out.exists():
            raise ValueError(
                f"{family}: fallback approved but stale override exists: {out}"
            )
        return {
            "family": family,
            "kind": kind,
            "status": "ORIGINAL-FALLBACK",
            "source": None,
            "output": None,
        }

    if family.upper() in fallbacks:
        raise ValueError(
            f"{family}: fallback declared even though canonical source exists: {source}"
        )
    if not out.is_file():
        raise FileNotFoundError(f"{family}: generated output missing: {out}")

    legacy, legacy_meta, _ = decode_sti(source)
    authored, authored_meta = decode_b1tc(out)
    if len(legacy) != len(authored):
        raise ValueError(
            f"{family}: frame count {len(legacy)} -> {len(authored)}"
        )

    for i, (a, b, ma, mb) in enumerate(
        zip(legacy, authored, legacy_meta, authored_meta)
    ):
        aa = a.convert("RGBA")
        bb = b.convert("RGBA")
        if aa.size != bb.size:
            raise ValueError(f"{family} frame {i}: dimensions changed")
        if (
            int(ma["offset_x"]), int(ma["offset_y"])
        ) != (
            int(mb["offset_x"]), int(mb["offset_y"])
        ):
            raise ValueError(f"{family} frame {i}: offsets changed")
        if aa.getchannel("A").tobytes() != bb.getchannel("A").tobytes():
            raise ValueError(f"{family} frame {i}: alpha footprint changed")

    unknown = []
    door_frames = []
    if kind in ("wall", "roof"):
        jsd = find_jsd(source)
        semantics = parse_jsd(jsd)
        door_frames = sorted(
            idx for idx, flags in semantics.items()
            if flags & STRUCTURE_ANYDOOR
        )
        if door_frames:
            raise ValueError(
                f"{family}: unsupported door semantics {door_frames}"
            )

        unknown = sorted(set(range(len(legacy))) - set(semantics))
        for i in unknown:
            if (
                legacy[i].convert("RGBA").tobytes()
                != authored[i].convert("RGBA").tobytes()
            ):
                raise ValueError(
                    f"{family} frame {i}: unknown JSD-backed frame was re-authored"
                )

    return {
        "family": family,
        "kind": kind,
        "status": "PASS",
        "source": str(source),
        "output": str(out),
        "frames": len(legacy),
        "unknown_passthrough": unknown,
        "door_frames": door_frames,
        "source_sha256": sha256_file(source),
        "output_sha256": sha256_file(out),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tilesets-root", required=True, type=Path)
    ap.add_argument("--out-root", required=True, type=Path)
    ap.add_argument("--allow-fallback", action="append", default=[])
    ap.add_argument("--json", type=Path)
    ns = ap.parse_args()

    fallbacks = {name.upper() for name in ns.allow_fallback}
    specs = [
        *((name, fn, "wall") for name, fn in WALLS.items()),
        *((name, fn, "floor") for name, fn in FLOORS.items()),
        *((name, fn, "roof") for name, fn in ROOFS.items()),
    ]

    rows = []
    errors = []
    for family, filename, kind in specs:
        try:
            row = verify_family(
                ns.tilesets_root, ns.out_root, family, filename, kind, fallbacks
            )
            rows.append(row)
            print(
                f"{row['status']:17} {family:10} "
                f"frames={row.get('frames', '-')} "
                f"unknown={row.get('unknown_passthrough', '-')}"
            )
        except Exception as exc:
            errors.append(f"{family}: {exc}")
            print(f"FAIL              {family:10} {exc}")

    report = {
        "status": "FAIL" if errors else "PASS",
        "families": rows,
        "errors": errors,
        "explicit_fallbacks": sorted(fallbacks),
    }
    if ns.json:
        ns.json.parent.mkdir(parents=True, exist_ok=True)
        ns.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if errors:
        print("A3 independent structural audit: FAIL")
        for err in errors:
            print("  - " + err)
        return 2

    print("A3 independent structural audit: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
