#!/usr/bin/env python3
"""Extract named JA2 STI contracts directly from classic SLF archives.

This is intentionally read-only and narrow.  It exists for the San Mona VHD2
pipeline so the self-hosted render PC can recover frame/alpha/offset contracts
from the user's installed game without building ja2export and without checking
legacy art into Git.

JA2 SLF layout (little-endian, MSVC packing):
  LIBHEADER  = 532 bytes
  DIRENTRY   = 280 bytes
Directory entries live at EOF - iEntries * 280.
Only FILE_OK (state 0) records are considered.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

LIBHEADER_SIZE = 532
DIRENTRY_SIZE = 280
FILE_OK = 0


def cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("latin-1", errors="replace")


def read_slf_index(path: Path):
    size = path.stat().st_size
    if size < LIBHEADER_SIZE:
        return None

    with path.open("rb") as f:
        header = f.read(LIBHEADER_SIZE)
        if len(header) != LIBHEADER_SIZE:
            return None

        entries = struct.unpack_from("<i", header, 512)[0]
        if entries <= 0 or entries > 1_000_000:
            return None

        table_size = entries * DIRENTRY_SIZE
        table_pos = size - table_size
        if table_pos < LIBHEADER_SIZE:
            return None

        f.seek(table_pos)
        out = []
        for _ in range(entries):
            raw = f.read(DIRENTRY_SIZE)
            if len(raw) != DIRENTRY_SIZE:
                return None

            name = cstr(raw[:256]).replace("/", "\\")
            offset = struct.unpack_from("<I", raw, 256)[0]
            length = struct.unpack_from("<I", raw, 260)[0]
            state = raw[264]

            if state != FILE_OK or not name:
                continue
            if offset + length > table_pos:
                # A valid payload must end before the directory table.
                continue
            out.append((name, offset, length))

        return {
            "library_name": cstr(header[:256]),
            "mount_path": cstr(header[256:512]),
            "entries": out,
        }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-root", required=True)
    ap.add_argument("--out-root", required=True)
    ap.add_argument("--target", action="append", required=True,
                    help="target filename, e.g. build_01.sti; repeatable")
    args = ap.parse_args()

    game_root = Path(args.game_root)
    out_root = Path(args.out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    targets = {Path(t).name.lower(): t for t in args.target}
    found = {}
    scanned = []

    slfs = sorted(game_root.rglob("*.slf"))
    for slf in slfs:
        index = read_slf_index(slf)
        if not index:
            continue
        scanned.append(str(slf))

        for name, offset, length in index["entries"]:
            base = Path(name).name.lower()
            if base not in targets or base in found:
                continue

            with slf.open("rb") as f:
                f.seek(offset)
                payload = f.read(length)
            if len(payload) != length:
                raise SystemExit(f"Truncated payload for {name} in {slf}")

            target_name = targets[base]
            dst = out_root / target_name
            dst.write_bytes(payload)
            found[base] = {
                "target": target_name,
                "archive": str(slf),
                "entry": name,
                "offset": offset,
                "length": length,
                "output": str(dst),
            }
            print(f"EXTRACTED {target_name} <- {slf.name}:{name} ({length} bytes)")

        if len(found) == len(targets):
            break

    missing = [targets[k] for k in targets if k not in found]
    manifest = {
        "game_root": str(game_root),
        "archives_scanned": len(scanned),
        "found": list(found.values()),
        "missing": missing,
    }
    (out_root / "slf_contract_manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8"
    )

    if missing:
        raise SystemExit("Missing SLF contracts: " + ", ".join(missing))


if __name__ == "__main__":
    main()
