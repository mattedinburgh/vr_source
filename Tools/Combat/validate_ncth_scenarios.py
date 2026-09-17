#!/usr/bin/env python3
"""Validate deterministic NCTH scenario CSVs against their provenance manifest."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path


def as_int(v, default=0):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return default


def as_float(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def sha256_file(path: Path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--skip-input-hashes", action="store_true")
    args = ap.parse_args()

    errors, warnings = [], []
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    with args.csv.open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        errors.append("scenario CSV is empty")
    if manifest.get("rows") != len(rows):
        errors.append(f"manifest rows={manifest.get('rows')} but CSV rows={len(rows)}")

    min_cth = as_float(manifest.get("limits", {}).get("min_cth"), 0.0)
    max_cth = as_float(manifest.get("limits", {}).get("max_cth"), 99.0)
    expected_mercs = set(manifest.get("mercs", []))
    expected_weapons = {str(x["id"]): x["weapon"] for x in manifest.get("weapon_selection", [])}
    actual_mercs = {r["merc"] for r in rows}
    actual_weapons = {r["weapon_id"]: r["weapon"] for r in rows}
    if expected_mercs and actual_mercs != expected_mercs:
        errors.append(f"merc set mismatch expected={sorted(expected_mercs)} actual={sorted(actual_mercs)}")
    if expected_weapons and actual_weapons != expected_weapons:
        errors.append(f"weapon set mismatch expected={expected_weapons} actual={actual_weapons}")

    identity_fields = (
        "merc_id", "weapon_id", "optic_mode", "optic_id", "path", "stance",
        "range_tiles", "aim_clicks",
    )
    seen = set()
    zero_rows = {}
    config_counts = Counter()
    mode_counts = Counter()
    penalty_counts = Counter()
    for idx, row in enumerate(rows, start=2):
        ident = tuple(row.get(k, "") for k in identity_fields)
        if ident in seen:
            errors.append(f"duplicate scenario identity at CSV line {idx}: {ident}")
        seen.add(ident)

        value = as_int(row.get("neutral_ncth"), -9999)
        base = as_float(row.get("base_ncth"), -9999)
        cap = as_float(row.get("aim_cap"), -9999)
        float_final = as_float(row.get("neutral_ncth_float"), value)
        clicks = as_int(row.get("aim_clicks"), -1)
        levels = as_int(row.get("allowed_aim_levels"), -1)
        scope_penalty = as_float(row.get("scope_penalty_pct"), 0.0)
        hit_pct = as_int(row.get("neutral_aperture_hit_pct"), -1)
        aperture = as_float(row.get("neutral_aperture"), -1.0)
        real_mag = as_float(row.get("real_scope_mag"), -1.0)
        effective_mag = as_float(row.get("effective_scope_mag"), -1.0)
        accuracy = as_float(row.get("effective_accuracy"), -1.0)
        gun_range = as_float(row.get("weapon_range_units"), -1.0)
        bullet_dev = as_float(row.get("bullet_deviation_radius"), -1.0)
        mode = row.get("optic_mode", "")
        mag = as_float(row.get("scope_mag"), 1.0)
        highest_mag = as_float(row.get("highest_mounted_scope_mag"), mag)

        if not (min_cth <= value <= max_cth):
            errors.append(f"line {idx}: neutral_ncth={value} outside {min_cth}..{max_cth}")
        if not (0 <= hit_pct <= 100):
            errors.append(f"line {idx}: aperture hit pct {hit_pct} outside 0..100")
        if aperture < 0 or real_mag < 1.0 or effective_mag < 1.0 or not (0 <= accuracy <= 100) or gun_range < 0 or bullet_dev < 0:
            errors.append(f"line {idx}: invalid second-stage NCTH fields")
        if mode in ("1x_unmounted", "1x_optic_mounted") and (abs(real_mag - 1.0) > 0.001 or abs(effective_mag - 1.0) > 0.001):
            errors.append(f"line {idx}: 1x mode has non-1x real/effective magnification {real_mag}/{effective_mag}")
        if mode in ("optic_active", "integrated_optic") and real_mag - mag > 0.001:
            errors.append(f"line {idx}: real scope mag {real_mag} exceeds selected optic {mag}")
        if not (0 <= clicks <= levels <= 8):
            errors.append(f"line {idx}: invalid aim clicks/levels {clicks}/{levels}")
        if clicks not in (0, levels):
            errors.append(f"line {idx}: generator row is neither unaimed nor full-aim ({clicks}/{levels})")
        if clicks == 0:
            expected = int(max(min_cth, min(max_cth, base)))
            if value != expected:
                errors.append(f"line {idx}: unaimed engine value {value} != truncated/clamped base {expected}")
            if abs(cap - base) > 0.001:
                errors.append(f"line {idx}: unaimed cap {cap} != base {base}")
            zero_key = tuple(row.get(k, "") for k in identity_fields[:-1])
            zero_rows[zero_key] = row
        else:
            zero_key = tuple(row.get(k, "") for k in identity_fields[:-1])
            zero = zero_rows.get(zero_key)
            if zero is None:
                warnings.append(f"line {idx}: full-aim row appeared before/missing unaimed pair {zero_key}")
            else:
                zero_value = as_int(zero.get("neutral_ncth"), value)
                if value < zero_value:
                    errors.append(f"line {idx}: full aim {value} below unaimed {zero_value}")
            if value > int(cap):
                errors.append(f"line {idx}: engine value {value} exceeds truncated aim cap {int(cap)}")
            if float_final + 0.001 < base or float_final - 0.001 > cap:
                errors.append(f"line {idx}: float final {float_final} outside base/cap {base}..{cap}")

            if mode == "1x_unmounted" and abs(scope_penalty) > 0.001:
                errors.append(f"line {idx}: unmounted 1x has scope penalty {scope_penalty}")
            if mode == "1x_optic_mounted" and highest_mag > 1.0 and scope_penalty >= 0:
                errors.append(f"line {idx}: mounted-but-inactive optic lacks expected negative penalty")
            if mode in ("optic_active", "integrated_optic") and mag > 1.0 and scope_penalty > 0:
                errors.append(f"line {idx}: active magnified optic has positive close-range penalty {scope_penalty}")

        cfg = tuple(row.get(k, "") for k in ("merc_id", "weapon_id", "optic_mode", "optic_id", "path"))
        config_counts[cfg] += 1
        mode_counts[mode] += 1
        if scope_penalty < 0:
            penalty_counts[mode] += 1

    expected_per_config = len(manifest.get("stances", [])) * len(manifest.get("ranges_tiles", [])) * 2
    if expected_per_config:
        bad = [(cfg, n) for cfg, n in config_counts.items() if n != expected_per_config]
        if bad:
            errors.append(f"{len(bad)} config groups do not have expected {expected_per_config} rows; first={bad[:3]}")

    if not args.skip_input_hashes:
        def verify_record(label, record):
            path = Path(record["path"])
            if not path.is_file():
                errors.append(f"manifest input missing: {label}: {path}")
                return
            digest = sha256_file(path)
            if digest != record.get("sha256"):
                errors.append(f"manifest input drift: {label}: {path}")
        inputs = manifest.get("input_files", {})
        for key in ("items", "weapons", "merc_profiles", "compatibility_csv"):
            if key in inputs:
                verify_record(key, inputs[key])
        for key in ("cth_ini", "runtime_ini", "skills_ini"):
            for i, record in enumerate(inputs.get(key, [])):
                verify_record(f"{key}[{i}]", record)

    print(f"ROWS={len(rows)} UNIQUE={len(seen)} CONFIGS={len(config_counts)}")
    print("MODES=" + ", ".join(f"{k}:{v}" for k, v in sorted(mode_counts.items())))
    print("NEGATIVE_SCOPE_PENALTIES=" + ", ".join(f"{k}:{v}" for k, v in sorted(penalty_counts.items())))
    print(f"ERRORS={len(errors)} WARNINGS={len(warnings)}")
    for msg in errors[:50]:
        print("ERROR:", msg)
    for msg in warnings[:20]:
        print("WARN:", msg)
    if len(errors) > 50:
        print(f"ERROR: ... {len(errors)-50} more")
    if len(warnings) > 20:
        print(f"WARN: ... {len(warnings)-20} more")
    return 2 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
