#!/usr/bin/env python3
"""Build a deterministic remaster queue for the northern/top six strategic rows.

The script is data-driven: it scans the actual Vengeance map directory and
SectorNames.xml, so future map work is based on the installed campaign rather
than a hard-coded vanilla JA2 list.
"""
from __future__ import annotations

import argparse
import csv
import re
import xml.etree.ElementTree as ET
from pathlib import Path


SECTOR_RE = re.compile(r"^([A-Pa-p])(\d{1,2})(?:_b(\d+))?\.dat$", re.I)


def parse_sector_names(path: Path):
    root = ET.parse(path).getroot()
    out = {}
    for node in root.findall(".//SECTOR"):
        grid = (node.findtext("SectorGrid") or "").strip().upper()
        if not grid:
            continue
        out[grid] = {
            "unexplored": (node.findtext("szUnexploredName") or "").strip(),
            "explored": (node.findtext("szExploredName") or "").strip(),
            "detailed": (node.findtext("szDetailedExploredName") or "").strip(),
            "water_type": (node.findtext("sWaterType") or "").strip(),
            "natural_dirt": (node.findtext("usNaturalDirt") or "").strip(),
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps-root", required=True, type=Path)
    ap.add_argument("--sector-names", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--last-row", default="F")
    ns = ap.parse_args()

    last_row = ns.last_row.upper()
    if last_row < "A" or last_row > "P":
        raise SystemExit("--last-row must be A..P")

    names = parse_sector_names(ns.sector_names)
    rows = []

    for p in sorted(ns.maps_root.iterdir(), key=lambda q: q.name.lower()):
        if not p.is_file():
            continue
        m = SECTOR_RE.match(p.name)
        if not m:
            continue
        row, col_s, underground_s = m.groups()
        row = row.upper()
        col = int(col_s)
        if row > last_row:
            continue

        sector = f"{row}{col}"
        info = names.get(sector, {})
        underground = int(underground_s) if underground_s else 0
        rows.append({
            "sector": sector,
            "row": row,
            "column": col,
            "level": underground,
            "map_file": p.name,
            "campaign_name": info.get("explored") or info.get("unexplored") or "Unknown",
            "water_type": info.get("water_type", ""),
            "natural_dirt": info.get("natural_dirt", ""),
            "priority": "surface" if underground == 0 else "underground-followup",
            "status": "A3-active" if sector == "A3" and underground == 0 else "queued",
        })

    rows.sort(key=lambda r: (ord(r["row"]), r["column"], r["level"]))

    ns.out.parent.mkdir(parents=True, exist_ok=True)
    with ns.out.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "sector", "row", "column", "level", "map_file",
                "campaign_name", "water_type", "natural_dirt",
                "priority", "status",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {len(rows)} map entries to {ns.out}")
    for r in rows:
        print(
            f"{r['sector']}{' B'+str(r['level']) if r['level'] else '':>4} "
            f"{r['campaign_name']:<24} {r['map_file']}"
        )


if __name__ == "__main__":
    main()
