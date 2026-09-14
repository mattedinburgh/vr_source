#!/usr/bin/env python3
"""Preserve canonical authored A3 while transplanting the proven Map Factory snapshot."""

from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: merge_map_factory_snapshot.py TARGET_WORLDDEF CANONICAL_WORLDDEF")

target_path = Path(sys.argv[1])
canonical_path = Path(sys.argv[2])

target = target_path.read_text(encoding="utf-8")
canonical = canonical_path.read_text(encoding="utf-8")

start_marker = "static const A3_FARM_VISUAL_PIECE gA3FieldBreach[]"
end_marker = "static void EnsureA3FarmCowPlacements( void )"

def segment(text: str, label: str):
    start = text.find(start_marker)
    end = text.find(end_marker)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit(f"{label}: could not locate authored A3 block boundaries")
    if text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{label}: duplicate A3 start marker")
    if text.find(end_marker, end + 1) >= 0:
        raise SystemExit(f"{label}: duplicate A3 end marker")
    return start, end

t_start, t_end = segment(target, "Map Factory snapshot")
c_start, c_end = segment(canonical, "canonical worlddef")

canonical_a3 = canonical[c_start:c_end]
merged = target[:t_start] + canonical_a3 + target[t_end:]

required_factory_markers = (
    "SECTOR_VISUAL_MAPFACTORY_MILITARY",
    "MapFactoryProfileForLeaf",
    "IsMapFactoryVisualProfileValue",
    "MapFactoryGradeTrueColorSurface",
    "GetMapFactoryPaletteGradedCount",
)
for marker in required_factory_markers:
    if marker not in merged:
        raise SystemExit(f"Map Factory marker lost during merge: {marker}")

required_a3_markers = (
    "gA3FieldBandWide",
    "gA3FieldBandDeep",
    "gA3DomesticYard",
    "gA3RepairApron",
    "FARM AUTHORED V3",
)
for marker in required_a3_markers:
    if marker not in merged:
        raise SystemExit(f"Canonical A3 V3 marker lost during merge: {marker}")

target_path.write_text(merged, encoding="utf-8", newline="\n")
print("Merged proven Map Factory worlddef with canonical authored A3 V3 block.")
