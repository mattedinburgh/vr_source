#!/usr/bin/env python3
"""Audit Vengeance weapon/NCTH data and actual NAS/CAF attachment install paths.

The graph intentionally models both semantic ValidAttachment() compatibility and the
NAS slot-class gate used by ValidItemAttachmentSlot().  Design warnings do not fail
the gate; broken references / hard data invariants do.
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict, deque
from pathlib import Path

IC_GUN = 0x00000002
IC_GRENADE = 0x00000100
IC_BOMB = 0x00000200
AC_SCOPE = 0x00000010
UPSTREAM_SPECIAL_NCTH_EXCEPTIONS = {1352: "Hand Mortar", 1627: "Pepper Spray"}


def parse_rows(path: Path):
    rows = []
    for elem in ET.parse(path).getroot():
        row = defaultdict(list)
        for child in elem:
            row[child.tag].append((child.text or "").strip())
        rows.append(row)
    return rows

def first(row, key, default=""):
    vals = row.get(key)
    return vals[0] if vals else default


def as_int(value, default=0):
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def as_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def values_int(row, key):
    return [as_int(v) for v in row.get(key, []) if v != ""]


def item_name(row, fallback=""):
    return first(row, "szItemName", fallback)


def weapon_name(row):
    return first(row, "szWeaponName", "?")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", required=True, type=Path)
    ap.add_argument("--csv", type=Path)
    ap.add_argument("--long-range", type=int, default=300)
    ap.add_argument("--max-adapter-depth", type=int, default=4)
    args = ap.parse_args()

    required = [
        "Weapons.xml", "Items.xml", "Attachments.xml", "Launchables.xml",
        "AttachmentSlots.xml", "IncompatibleAttachments.xml",
    ]
    errors, warnings, info = [], [], []
    for name in required:
        if not (args.data_root / name).is_file():
            errors.append(f"missing required file: {name}")
    if errors:
        for msg in errors:
            print("ERROR:", msg)
        return 2

    weapons_rows = parse_rows(args.data_root / "Weapons.xml")
    items_rows = parse_rows(args.data_root / "Items.xml")
    attach_rows = parse_rows(args.data_root / "Attachments.xml")
    launch_rows = parse_rows(args.data_root / "Launchables.xml")
    slot_rows = parse_rows(args.data_root / "AttachmentSlots.xml")
    incompat_rows = parse_rows(args.data_root / "IncompatibleAttachments.xml")

    weapons = {as_int(first(r, "uiIndex"), -1): r for r in weapons_rows if first(r, "uiIndex") != ""}
    items = {as_int(first(r, "uiIndex"), -1): r for r in items_rows if first(r, "uiIndex") != ""}

    explicit = defaultdict(set)
    duplicate_pairs = Counter()
    for r in attach_rows:
        aid = as_int(first(r, "attachmentIndex"), -1)
        host = as_int(first(r, "itemIndex"), -1)
        duplicate_pairs[(aid, host)] += 1
        if aid not in items:
            errors.append(f"Attachments.xml references missing attachment item {aid}")
        if host not in items:
            errors.append(f"Attachments.xml references missing host item {host}")
        if aid >= 0 and host >= 0:
            explicit[host].add(aid)

    for (aid, host), count in sorted(duplicate_pairs.items()):
        if count > 1:
            warnings.append(f"duplicate explicit attachment mapping {aid} -> {host} x{count}")

    launchable = defaultdict(set)
    for r in launch_rows:
        payload = as_int(first(r, "launchableIndex"), -1)
        host = as_int(first(r, "itemIndex"), -1)
        if payload not in items:
            errors.append(f"Launchables.xml references missing launchable item {payload}")
        if host not in items:
            errors.append(f"Launchables.xml references missing host item {host}")
        if payload >= 0 and host >= 0:
            launchable[host].add(payload)

    slot_defs, slot_counts = {}, Counter()
    for r in slot_rows:
        sid = as_int(first(r, "uiSlotIndex"), -1)
        slot_counts[sid] += 1
        slot_defs[sid] = {
            "class": as_int(first(r, "nasAttachmentClass"), 0),
            "layout": as_int(first(r, "nasLayoutClass"), 1),
            "multi": as_int(first(r, "fMultiShot"), 0),
        }
        if sid < 0:
            errors.append("AttachmentSlots.xml contains invalid uiSlotIndex")
    for sid, count in sorted(slot_counts.items()):
        if count > 1:
            errors.append(f"duplicate AttachmentSlots.xml uiSlotIndex {sid} x{count}")

    incompatible, incompat_counts = set(), Counter()
    for r in incompat_rows:
        a = as_int(first(r, "itemIndex"), -1)
        b = as_int(first(r, "incompatibleattachmentIndex"), -1)
        incompat_counts[(a, b)] += 1
        if a not in items or b not in items:
            errors.append(f"IncompatibleAttachments.xml references missing item in {a} -> {b}")
        if a >= 0 and b >= 0:
            incompatible.add((a, b))

    for pair, count in sorted(incompat_counts.items()):
        if count > 1:
            warnings.append(f"duplicate incompatible attachment rule {pair[0]} -> {pair[1]} x{count}")
    for a, b in sorted(incompatible):
        if (b, a) not in incompatible:
            warnings.append(f"asymmetric incompatible attachment rule {a} -> {b}; reverse rule missing")

    gun_ids = sorted(idx for idx, row in items.items() if as_int(first(row, "usItemClass"), 0) & IC_GUN)
    for idx in gun_ids:
        if idx not in weapons:
            errors.append(f"IC_GUN item {idx} {item_name(items[idx])!r} has no Weapons.xml entry")

    optic_ids = []
    for idx, row in items.items():
        mag = as_float(first(row, "ScopeMagFactor"), 0.0)
        aclass = as_int(first(row, "AttachmentClass"), 0)
        is_attachment = as_int(first(row, "Attachment"), 0) == 1
        if mag > 1.0 and (is_attachment or (aclass & AC_SCOPE)):
            optic_ids.append(idx)
            if as_int(first(row, "AimBonus"), 0) <= 0:
                warnings.append(f"magnifying optic {idx} {item_name(row)!r} has no positive AimBonus")
            if as_int(first(row, "MinRangeForAimBonus"), 0) <= 0:
                warnings.append(f"magnifying optic {idx} {item_name(row)!r} has no positive MinRangeForAimBonus")

    def item_mask(item_id, key):
        mask = 0
        for value in values_int(items.get(item_id, {}), key):
            mask |= value
        return mask

    def is_point_eligible(aid):
        row = items.get(aid)
        cls = as_int(first(row, "usItemClass"), 0) if row else 0
        special_attachable_class = bool(cls & 0x00000300)
        return bool(row) and (as_int(first(row, "Attachment"), 0) == 1 or special_attachable_class)

    def attachment_points(aid):
        return item_mask(aid, "AttachmentPoint")

    def available_points(aid):
        return item_mask(aid, "AvailableAttachmentPoint")

    def nas_class(aid):
        return as_int(first(items.get(aid, {}), "nasAttachmentClass"), 0)

    def layout_class(aid):
        value = as_int(first(items.get(aid, {}), "nasLayoutClass"), 0)
        return value if value else 1

    def semantic_valid(candidate, target, aggregate_points=0):
        if candidate == target or candidate not in items or target not in items:
            return False
        if candidate in explicit.get(target, set()):
            return True
        if not is_point_eligible(candidate):
            return False
        points = available_points(target) | aggregate_points
        return bool(points and attachment_points(candidate) and (points & attachment_points(candidate)))

    def slots_for_mask(class_mask, layout):
        result = []
        bit = 1
        for _ in range(64):
            if class_mask & bit:
                specific = [sid for sid, sd in slot_defs.items()
                            if sid > 0 and (sd["class"] & bit) and (sd["layout"] & layout)]
                chosen = specific or [sid for sid, sd in slot_defs.items()
                                      if sid > 0 and (sd["class"] & bit) and sd["layout"] == 1]
                result.extend(chosen)
            bit <<= 1
        return list(dict.fromkeys(result))

    def intrinsic_slots(item_id):
        class_mask = 0
        for candidate in items:
            if semantic_valid(candidate, item_id):
                class_mask |= nas_class(candidate)
        for payload in launchable.get(item_id, set()):
            class_mask |= nas_class(payload)
        return slots_for_mask(class_mask, layout_class(item_id))

    intrinsic_slot_cache = {idx: intrinsic_slots(idx) for idx in items}

    def available_slots(host, attached):
        aggregate = available_points(host)
        for aid in attached:
            if as_int(first(items.get(aid, {}), "Attachment"), 0) == 1:
                aggregate |= available_points(aid)

        class_mask = 0
        for candidate in explicit.get(host, set()):
            class_mask |= nas_class(candidate)
        for payload in launchable.get(host, set()):
            class_mask |= nas_class(payload)
        if aggregate:
            for candidate in items:
                if is_point_eligible(candidate) and (attachment_points(candidate) & aggregate):
                    class_mask |= nas_class(candidate)

        result = slots_for_mask(class_mask, layout_class(host))
        for aid in attached:
            if as_int(first(items.get(aid, {}), "Attachment"), 0) == 1:
                result.extend(intrinsic_slot_cache.get(aid, []))
        return list(dict.fromkeys(result))

    def slots_hold(installed, slot_ids):
        if not installed:
            return True
        choices = []
        for aid in installed:
            cls = nas_class(aid)
            legal = [sid for sid in slot_ids if slot_defs.get(sid, {}).get("class", 0) & cls]
            if not legal:
                return False
            choices.append(legal)

        match = {}
        order = sorted(range(len(installed)), key=lambda pos: len(choices[pos]))

        def augment(pos, seen):
            for sid in choices[pos]:
                if sid in seen:
                    continue
                seen.add(sid)
                if sid not in match or augment(match[sid], seen):
                    match[sid] = pos
                    return True
            return False

        return all(augment(pos, set()) for pos in order)

    def can_add(candidate, host, attached, require_slots=True):
        if candidate == host or candidate in attached or candidate not in items:
            return False
        if any((candidate, existing) in incompatible for existing in attached):
            return False

        aggregate = available_points(host)
        for existing in attached:
            if as_int(first(items.get(existing, {}), "Attachment"), 0) == 1:
                aggregate |= available_points(existing)

        semantic = any(
            semantic_valid(candidate, target, aggregate if target == host else 0)
            for target in (host,) + tuple(attached)
        )
        if not semantic:
            return False
        if not require_slots:
            return True

        # Install-time rule: the candidate must fit slots that exist before it is added.
        return slots_hold(tuple(attached) + (candidate,), available_slots(host, attached))

    adapter_ids = sorted(
        aid for aid, row in items.items()
        if as_int(first(row, "Attachment"), 0) == 1
        and aid not in optic_ids
        and (available_points(aid) or intrinsic_slot_cache.get(aid) or aid in explicit)
    )

    def optic_paths_for(host, require_slots=True):
        found = {}
        queue = deque([tuple()])
        seen = {tuple()}
        while queue:
            attached = queue.popleft()
            for oid in optic_ids:
                if oid not in found and can_add(oid, host, attached, require_slots):
                    found[oid] = attached + (oid,)
            if len(attached) >= args.max_adapter_depth:
                continue
            for aid in adapter_ids:
                if aid in attached or not can_add(aid, host, attached, require_slots):
                    continue
                state = attached + (aid,)
                if state not in seen:
                    seen.add(state)
                    queue.append(state)
        return found

    explicit_attachment_ids = {aid for aids in explicit.values() for aid in aids}
    for aid, row in sorted(items.items()):
        if attachment_points(aid) and not is_point_eligible(aid) and aid not in explicit_attachment_ids:
            warnings.append(
                f"item {aid} {item_name(row)!r} has AttachmentPoint but CAF cannot use it and no explicit mapping"
            )

    matrix = []
    slot_blocked_pairs = 0
    for idx in gun_ids:
        w = weapons.get(idx)
        if not w:
            continue
        irow = items[idx]
        nacc_raw = first(w, "nAccuracy", "")
        handling_raw = first(w, "Handling", "")
        aim_levels_raw = first(w, "ubAimLevels", "")
        nacc = as_int(nacc_raw, 0)
        handling = as_int(handling_raw, 0)
        aim_levels = as_int(aim_levels_raw, 0)
        rng = as_int(first(w, "usRange"), 0)
        auto = as_int(first(w, "bAutofireShotsPerFiveAP"), 0)
        burst = as_int(first(w, "ubShotsPerBurst"), 0)
        recoil_x = as_float(first(w, "bRecoilX"), 0.0)
        recoil_y = as_float(first(w, "bRecoilY"), 0.0)

        weapon_type = as_int(first(w, "ubWeaponType"), 0)
        if weapon_type == 0:
            info.append(f"nonstandard pseudo-gun {idx} {weapon_name(w)!r} uses reduced NCTH metadata")
        elif idx in UPSTREAM_SPECIAL_NCTH_EXCEPTIONS:
            if not nacc_raw or not handling_raw or not aim_levels_raw:
                info.append(f"upstream special-case {idx} {weapon_name(w)!r} lacks normal NCTH fields")
        else:
            if not nacc_raw or not 0 <= nacc <= 100:
                errors.append(f"gun {idx} {weapon_name(w)!r} invalid nAccuracy={nacc_raw!r}")
            if not handling_raw or handling <= 0:
                errors.append(f"gun {idx} {weapon_name(w)!r} missing/invalid Handling")
            if not aim_levels_raw or aim_levels <= 0:
                warnings.append(f"gun {idx} {weapon_name(w)!r} missing/invalid ubAimLevels")

        if rng <= 0:
            errors.append(f"gun {idx} {weapon_name(w)!r} has non-positive range")
        if (auto > 0 or burst > 1) and recoil_y <= 0:
            warnings.append(f"automatic/burst gun {idx} {weapon_name(w)!r} has no positive bRecoilY")

        semantic_paths = optic_paths_for(idx, require_slots=False)
        installable_paths = optic_paths_for(idx, require_slots=True)
        optics = sorted(installable_paths)
        semantic_only = sorted(set(semantic_paths) - set(installable_paths))
        slot_blocked_pairs += len(semantic_only)

        integrated_mag = as_float(first(irow, "ScopeMagFactor"), 0.0)
        max_attach_mag = max([as_float(first(items[o], "ScopeMagFactor"), 1.0) for o in optics] or [1.0])
        max_mag = max(1.0, integrated_mag, max_attach_mag)
        if weapon_type > 0 and rng >= args.long_range and max_mag <= 1.0:
            warnings.append(f"long-range gun {idx} {weapon_name(w)!r} range={rng} has no installable magnified-optic path")

        matrix.append({
            "id": idx,
            "weapon": weapon_name(w),
            "weapon_type": weapon_type,
            "range": rng,
            "nAccuracy": nacc if nacc_raw else "",
            "base_deviation_fraction": round((100 - nacc) / 100.0, 3) if nacc_raw else "",
            "aim_levels": aim_levels if aim_levels_raw else "",
            "handling": handling if handling_raw else "",
            "autofire_per_5ap": auto,
            "burst_size": burst,
            "recoil_x": recoil_x,
            "recoil_y": recoil_y,
            "recoil_magnitude": round(math.hypot(recoil_x, recoil_y), 3),
            "integrated_scope_mag": integrated_mag,
            "max_installable_scope_mag": max_mag,
            "installable_optics": "; ".join(item_name(items[o], str(o)) for o in optics),
            "slot_blocked_optics": "; ".join(item_name(items[o], str(o)) for o in semantic_only),
            "optic_paths": "; ".join(
                f"{item_name(items[o], str(o))}:" + ">".join(str(x) for x in installable_paths[o])
                for o in optics
            ),
        })

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8-sig") as f:
            writer = csv.DictWriter(f, fieldnames=list(matrix[0].keys()))
            writer.writeheader()
            writer.writerows(matrix)

    print(f"WEAPONS={len(weapons)} ITEMS={len(items)} IC_GUN={len(gun_ids)} OPTICS={len(optic_ids)}")
    print(f"SLOT_BLOCKED_OPTIC_PAIRS={slot_blocked_pairs}")
    print(f"ERRORS={len(errors)} WARNINGS={len(warnings)} INFO={len(info)}")
    for msg in errors:
        print("ERROR:", msg)
    for msg in warnings:
        print("WARN:", msg)
    for msg in info:
        print("INFO:", msg)
    if args.csv:
        print("CSV:", args.csv)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
