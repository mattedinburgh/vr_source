#!/usr/bin/env python3
"""Convert validated neutral NCTH scenarios into transparent role-fit comparisons.

The model is intentionally accuracy/ergonomics-only: it does not invent damage, AP,
ammo, reliability, suppression, or battlefield LOS values that are absent from the
scenario oracle. Built-in role weights are defaults and can be replaced with JSON.
"""
from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

DEFAULT_ROLES = {
    "assault": {
        "weapon_types": [1, 2, 3, 4, 6, 8],
        "range_weights": {10: 0.45, 20: 0.40, 35: 0.15},
        "stance_weights": {"stand": 0.50, "crouch": 0.40, "prone": 0.10},
        "component_weights": {"aimed": 0.40, "snapshot": 0.35, "handling": 0.15, "recoil": 0.05, "scope": 0.05},
    },
    "rifleman": {
        "weapon_types": [3, 4, 6],
        "range_weights": {10: 0.15, 20: 0.35, 35: 0.35, 50: 0.15},
        "stance_weights": {"stand": 0.35, "crouch": 0.45, "prone": 0.20},
        "component_weights": {"aimed": 0.55, "snapshot": 0.20, "handling": 0.10, "recoil": 0.10, "scope": 0.05},
    },
    "marksman": {
        "weapon_types": [4, 5, 6],
        "range_weights": {20: 0.10, 35: 0.35, 50: 0.55},
        "stance_weights": {"stand": 0.15, "crouch": 0.45, "prone": 0.40},
        "component_weights": {"aimed": 0.72, "snapshot": 0.08, "handling": 0.07, "recoil": 0.08, "scope": 0.05},
    },
    "sniper": {
        "weapon_types": [5, 4],
        "range_weights": {35: 0.30, 50: 0.70},
        "stance_weights": {"stand": 0.05, "crouch": 0.25, "prone": 0.70},
        "component_weights": {"aimed": 0.82, "snapshot": 0.03, "handling": 0.04, "recoil": 0.06, "scope": 0.05},
    },
}

DEFAULT_SCALES = {"handling": 20.0, "recoil": 20.0, "scope_penalty": 20.0}


def f(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def i(v, default=0):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return default


def normalize(weights):
    weights = {k: float(v) for k, v in weights.items() if float(v) > 0}
    total = sum(weights.values())
    if total <= 0:
        raise ValueError("weight map has no positive weights")
    return {k: v / total for k, v in weights.items()}


def load_roles(path):
    if not path:
        return DEFAULT_ROLES, DEFAULT_SCALES
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    roles = data.get("roles", data)
    scales = dict(DEFAULT_SCALES)
    scales.update(data.get("scales", {})) if isinstance(data, dict) else None
    return roles, scales


def weighted_metric(rows_by_key, range_weights, stance_weights, full_aim, field, transform=lambda x: x):
    total = 0.0
    used = 0.0
    for rng, rw in range_weights.items():
        for stance, sw in stance_weights.items():
            row = rows_by_key.get((int(rng), stance, full_aim))
            if row is None:
                continue
            w = rw * sw
            total += transform(f(row.get(field))) * w
            used += w
    return total / used if used else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenarios", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--roles-json", type=Path)
    ap.add_argument("--role", action="append", default=[])
    ap.add_argument("--merc", action="append", default=[])
    ap.add_argument("--weapon", action="append", default=[])
    ap.add_argument("--csv", required=True, type=Path)
    args = ap.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    roles, scales = load_roles(args.roles_json)
    if args.role:
        missing = [x for x in args.role if x not in roles]
        if missing:
            raise SystemExit(f"unknown roles: {', '.join(missing)}")
        roles = {k: roles[k] for k in args.role}

    with args.scenarios.open(encoding="utf-8-sig", newline="") as fh:
        rows = list(csv.DictReader(fh))
    if args.merc:
        keys = {x.casefold() for x in args.merc}
        rows = [r for r in rows if r["merc"].casefold() in keys]
    if args.weapon:
        keys = {x.casefold() for x in args.weapon}
        exact = [r for r in rows if r["weapon"].casefold() in keys or r["weapon_id"] in args.weapon]
        if exact:
            rows = exact
        else:
            rows = [r for r in rows if any(x in r["weapon"].casefold() for x in keys)]
    if not rows:
        raise SystemExit("no scenario rows selected")

    groups = defaultdict(list)
    for row in rows:
        key = (row["merc_id"], row["merc"], row["weapon_id"], row["weapon"], row["optic_mode"], row["optic_id"], row["optic"], row["path"])
        groups[key].append(row)

    output = []
    for role_name, spec in roles.items():
        rw = normalize({int(k): v for k, v in spec["range_weights"].items()})
        sw = normalize(spec["stance_weights"])
        cw = normalize(spec["component_weights"])
        for key, grows in groups.items():
            if spec.get("weapon_types") and i(grows[0].get("weapon_type")) not in {int(x) for x in spec["weapon_types"]}:
                continue
            grid = {}
            for row in grows:
                levels = i(row["allowed_aim_levels"])
                clicks = i(row["aim_clicks"])
                full = clicks == levels and clicks > 0
                grid[(i(row["range_tiles"]), row["stance"], full)] = row
            aimed = weighted_metric(grid, rw, sw, True, "neutral_aperture_hit_pct")
            snapshot = weighted_metric(grid, rw, sw, False, "neutral_aperture_hit_pct")
            close_penalty = weighted_metric(grid, rw, sw, True, "scope_penalty_pct", lambda x: max(0.0, -x))
            if aimed is None or snapshot is None or close_penalty is None:
                continue
            sample = grows[0]
            handling = f(sample["handling"])
            recoil = f(sample["recoil_magnitude"])
            components = {
                "aimed": max(0.0, min(1.0, aimed / 100.0)),
                "snapshot": max(0.0, min(1.0, snapshot / 100.0)),
                "handling": 1.0 / (1.0 + max(0.0, handling) / float(scales["handling"])),
                "recoil": 1.0 / (1.0 + max(0.0, recoil) / float(scales["recoil"])),
                "scope": 1.0 / (1.0 + max(0.0, close_penalty) / float(scales["scope_penalty"])),
            }
            score = 100.0 * sum(cw.get(name, 0.0) * value for name, value in components.items())
            output.append({
                "role": role_name,
                "merc_id": key[0], "merc": key[1], "weapon_id": key[2], "weapon": key[3], "weapon_type": grows[0]["weapon_type"], "weapon_type": grows[0]["weapon_type"],
                "optic_mode": key[4], "optic_id": key[5], "optic": key[6], "path": key[7],
                "role_fit_index": round(score, 3),
                "weighted_full_aim_hit_pct": round(aimed, 3),
                "weighted_snapshot_hit_pct": round(snapshot, 3),
                "weighted_scope_penalty": round(close_penalty, 3),
                "handling": round(handling, 3), "recoil_magnitude": round(recoil, 3),
                "aimed_component": round(components["aimed"] * 100, 3),
                "snapshot_component": round(components["snapshot"] * 100, 3),
                "handling_component": round(components["handling"] * 100, 3),
                "recoil_component": round(components["recoil"] * 100, 3),
                "scope_component": round(components["scope"] * 100, 3),
            })

    output.sort(key=lambda r: (r["role"], r["merc"], -r["role_fit_index"], r["weapon"], r["optic_mode"], r["optic_id"]))
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", encoding="utf-8-sig", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(output[0].keys()))
        writer.writeheader()
        writer.writerows(output)

    print(f"ROLES={len(roles)} CONFIGS={len(groups)} ROWS={len(output)}")
    print("ROLE_NAMES=" + ",".join(roles))
    print("MODEL=relative role-fit index from validated NCTH aperture-hit evaluation + handling/recoil/scope ergonomics; no damage/AP/ammo/LOS assumptions")
    print("CSV:", args.csv)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
