#!/usr/bin/env python3
"""Deterministic neutral-scenario NCTH calculator for VR weapon/merc comparisons.

This mirrors the stable, data-driven core of CalcNewChanceToHitGun(): attribute
base/cap, handling, stance, active optic item modifiers, scope-too-close penalty,
Sniper/Gunslinger cap bonus, and diminishing aim clicks. Dynamic battlefield
terms (LOS/cover, shock, injury, morale, target movement, backgrounds, spotters,
etc.) remain engine-regression inputs and are deliberately not guessed here.
"""
from __future__ import annotations

import argparse
import configparser
import csv
import hashlib
import json
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

GUN_PISTOL, GUN_M_PISTOL, GUN_SMG, GUN_RIFLE = 1, 2, 3, 4
GUN_SN_RIFLE, GUN_AS_RIFLE, GUN_LMG, GUN_SHOTGUN = 5, 6, 7, 8
SNIPER_NT, RANGER_NT, GUNSLINGER_NT = 3, 4, 5
STANCE_KEY = {"stand": "STANDING", "crouch": "CROUCHING", "prone": "PRONE"}
STANCE_XML = {"stand": "STAND_MODIFIERS", "crouch": "CROUCH_MODIFIERS", "prone": "PRONE_MODIFIERS"}

DEFAULTS = {
    "NORMAL_SHOOTING_DISTANCE": 70.0, "SCOPE_RANGE_MULTIPLIER": 0.7,
    "BASE_EXP": 3.0, "BASE_MARKS": 1.0, "BASE_DEX": 1.0, "BASE_WIS": 1.0,
    "BASE_DRAW_COST": 2.0, "BASE_STANDING_STANCE": 2.0, "BASE_CROUCHING_STANCE": 3.0,
    "BASE_PRONE_STANCE": 4.0, "AIM_EXP": 1.0, "AIM_MARKS": 3.0,
    "AIM_DEX": 2.0, "AIM_WIS": 1.0, "AIM_DRAW_COST": 1.0,
    "AIM_STANDING_STANCE": 1.5, "AIM_CROUCHING_STANCE": 1.25, "AIM_PRONE_STANCE": 1.0,
    "AIM_TOO_CLOSE_SCOPE": -4.0, "AIM_TOO_CLOSE_THRESHOLD": 0.8,
    "DEGREES_MAXIMUM_APERTURE": 15.0, "IRON_SIGHT_PERFORMANCE_BONUS": 20.0,
    "IRON_SIGHTS_MAX_APERTURE_USE_GRADIENT": 1.0, "IRON_SIGHTS_MAX_APERTURE_MODIFIER": 3.0,
    "SCOPE_EFFECTIVENESS_MULTIPLIER": 1.1, "SCOPE_EFFECTIVENESS_MINIMUM": 50.0,
    "SCOPE_EFFECTIVENESS_MINIMUM_RANGER": 80.0, "SCOPE_EFFECTIVENESS_MINIMUM_MARKSMAN": 90.0,
    "SCOPE_EFFECTIVENESS_MINIMUM_SNIPER": 100.0, "MAX_EFFECTIVE_RANGE_MULTIPLIER": 1.4,
    "MAX_EFFECTIVE_RANGE_REDUCTION": 0.5, "MAX_EFFECTIVE_USE_GRADIENT": 1.0,
    "MAX_BULLET_DEV": 5.0, "RANGE_EFFECTS_DEV": 1.0,
}

def ntext(node, tag, default=""):
    child = node.find(tag) if node is not None else None
    return child.text.strip() if child is not None and child.text else default


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


def load_ini(path):
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"))
    cp.optionxform = str.upper
    cp.read(path, encoding="utf-8-sig")
    return cp


def existing_paths(paths):
    return [Path(path) for path in paths if Path(path).is_file()]


def live_vr_config_inputs(game_root, mod_root=None):
    """Mirror the merged VFS inputs relevant to this neutral NCTH oracle.

    JA2 registers CTHConstants.ini, Ja2_Options.ini and Skills_Settings.INI for
    VFS merging. Vengeance also supplies .Override files. The order below is
    low-to-high precedence for the shipped Vengeance profile stack.
    """
    root = Path(game_root)
    mod = Path(mod_root) if mod_root else root
    return {
        "cth": existing_paths([
            root / "Data" / "CTHConstants.ini",
            root / "Data-1.13" / "CTHConstants.ini",
            mod / "Data-AIMv53" / "CTHConstants.ini",
            mod / "Data-Vengeance" / "CTHConstants.Override",
        ]),
        "runtime": existing_paths([
            root / "Data" / "Ja2_Options.INI",
            root / "Data-1.13" / "Ja2_Options.INI",
            mod / "Data-Vengeance" / "Ja2_Options.INI",
            mod / "Data-Vengeance" / "Ja2_Options.Override",
        ]),
        "skills": existing_paths([
            root / "Data" / "Skills_Settings.INI",
            root / "Data-1.13" / "Skills_Settings.INI",
            mod / "Data-Vengeance" / "Skills_Settings.INI",
        ]),
    }


def sha256_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def source_record(path):
    path = Path(path)
    return {"path": str(path), "sha256": sha256_file(path), "size": path.stat().st_size}


def load_constants(paths):
    out = dict(DEFAULTS)
    key_sections = {
        "NORMAL_SHOOTING_DISTANCE": "General", "SCOPE_RANGE_MULTIPLIER": "General",
        "DEGREES_MAXIMUM_APERTURE": "General", "IRON_SIGHT_PERFORMANCE_BONUS": "General",
        "SCOPE_EFFECTIVENESS_MULTIPLIER": "General", "SCOPE_EFFECTIVENESS_MINIMUM": "General",
        "SCOPE_EFFECTIVENESS_MINIMUM_RANGER": "General", "SCOPE_EFFECTIVENESS_MINIMUM_MARKSMAN": "General",
        "SCOPE_EFFECTIVENESS_MINIMUM_SNIPER": "General",
        "MAX_EFFECTIVE_RANGE_MULTIPLIER": "Shooting Mechanism", "MAX_EFFECTIVE_RANGE_REDUCTION": "Shooting Mechanism",
        "MAX_BULLET_DEV": "Shooting Mechanism",
        "BASE_EXP": "Base CTH", "BASE_MARKS": "Base CTH", "BASE_DEX": "Base CTH", "BASE_WIS": "Base CTH",
        "BASE_DRAW_COST": "Base CTH", "BASE_STANDING_STANCE": "Base CTH",
        "BASE_CROUCHING_STANCE": "Base CTH", "BASE_PRONE_STANCE": "Base CTH",
        "AIM_EXP": "Aiming CTH", "AIM_MARKS": "Aiming CTH",
        "AIM_DEX": "Aiming CTH", "AIM_WIS": "Aiming CTH", "AIM_DRAW_COST": "Aiming CTH",
        "AIM_STANDING_STANCE": "Aiming CTH", "AIM_CROUCHING_STANCE": "Aiming CTH",
        "AIM_PRONE_STANCE": "Aiming CTH",
        "AIM_TOO_CLOSE_SCOPE": "Aiming CTH", "AIM_TOO_CLOSE_THRESHOLD": "Aiming CTH",
    }
    for path in paths:
        cp = load_ini(path)
        for key, sec in key_sections.items():
            if cp.has_option(sec, key):
                out[key] = as_float(cp.get(sec, key), out[key])
        for key, sec in (("MAX_EFFECTIVE_USE_GRADIENT", "Shooting Mechanism"), ("RANGE_EFFECTS_DEV", "Shooting Mechanism"), ("IRON_SIGHTS_MAX_APERTURE_USE_GRADIENT", "General")):
            if cp.has_option(sec, key):
                out[key] = 1.0 if cp.getboolean(sec, key) else 0.0
        if cp.has_option("General", "IRON_SIGHTS_MAX_APERTURE_MODIFIER"):
            out["IRON_SIGHTS_MAX_APERTURE_MODIFIER"] = as_float(cp.get("General", "IRON_SIGHTS_MAX_APERTURE_MODIFIER"), out["IRON_SIGHTS_MAX_APERTURE_MODIFIER"])
    return out

def load_runtime_limits(paths):
    values = {"min_cth": 1.0, "max_cth": 99.0}
    for path in paths:
        cp = load_ini(path)
        section = "Tactical Gameplay Settings"
        if cp.has_option(section, "MINIMUM_POSSIBLE_CTH"):
            values["min_cth"] = as_float(cp.get(section, "MINIMUM_POSSIBLE_CTH"), values["min_cth"])
        if cp.has_option(section, "MAXIMUM_POSSIBLE_CTH"):
            values["max_cth"] = as_float(cp.get(section, "MAXIMUM_POSSIBLE_CTH"), values["max_cth"])
    return values

def load_skills(paths):
    values = {
        "sniper_bonus": 10, "sniper_clicks": 1,
        "gunslinger_bonus": 5, "gunslinger_clicks": 1, "ranger_clicks": 1,
    }
    key_map = {
        "sniper_bonus": ("Sniper", "AIMING_BONUS_PER_CLICK_RIFLES"),
        "sniper_clicks": ("Sniper", "POSSIBLE_AIM_CLICK_ADDED_RIFLES"),
        "gunslinger_bonus": ("Gunslinger", "AIMING_BONUS_PER_CLICK_HANDGUNS"),
        "gunslinger_clicks": ("Gunslinger", "POSSIBLE_AIM_CLICK_ADDED_HANDGUNS"),
        "ranger_clicks": ("Ranger", "POSSIBLE_AIM_CLICK_ADDED_SHOTGUNS"),
    }
    for path in paths:
        cp = load_ini(path)
        for key, (section, option) in key_map.items():
            if cp.has_option(section, option):
                values[key] = cp.getint(section, option)
    return values


def load_items(path):
    return {as_int(ntext(node, "uiIndex"), -1): node for node in ET.parse(path).getroot() if ntext(node, "uiIndex")}


def load_data_rows(path):
    return {as_int(ntext(node, "uiIndex"), -1): node for node in ET.parse(path).getroot() if ntext(node, "uiIndex")}

def load_mercs(path):
    result = []
    for node in ET.parse(path).getroot():
        result.append({
            "id": as_int(ntext(node, "uiIndex"), -1),
            "type": as_int(ntext(node, "Type"), 0),
            "name": ntext(node, "zName"),
            "nick": ntext(node, "zNickname"),
            "exp": as_int(ntext(node, "bExpLevel"), 0),
            "mrk": as_int(ntext(node, "bMarksmanship"), 0),
            "dex": as_int(ntext(node, "bDexterity"), 0),
            "wis": as_int(ntext(node, "bWisdom"), 0),
            "agi": as_int(ntext(node, "bAgility"), 0),
            "traits": [as_int(ntext(node, f"bNewSkillTrait{i}"), 0) for i in (1, 2, 3)],
        })
    return result


def item_modifier(node, stance, tag):
    if node is None:
        return 0.0
    return as_float(ntext(node.find(STANCE_XML[stance]), tag), 0.0)


def item_value(node, tag):
    return as_float(ntext(node, tag), 0.0) if node is not None else 0.0

def trait_count(merc, trait):
    return sum(1 for value in merc["traits"] if value == trait)


def base_attribute(merc, c, min_cth=1.0):
    if merc["mrk"] <= 0 or merc["dex"] <= 0:
        return float(min_cth)
    num = c["BASE_EXP"] * merc["exp"] * 10 + c["BASE_MARKS"] * merc["mrk"]
    num += c["BASE_DEX"] * merc["dex"] + c["BASE_WIS"] * merc["wis"]
    den = c["BASE_EXP"] + c["BASE_MARKS"] + c["BASE_DEX"] + c["BASE_WIS"]
    return num / den / 3.0


def aim_attribute(merc, c):
    num = c["AIM_EXP"] * merc["exp"] * 10 + c["AIM_MARKS"] * merc["mrk"]
    num += c["AIM_DEX"] * merc["dex"] + c["AIM_WIS"] * merc["wis"]
    den = c["AIM_EXP"] + c["AIM_MARKS"] + c["AIM_DEX"] + c["AIM_WIS"]
    return num / den


def parse_path_ids(text):
    if not text or ":" not in text:
        return []
    tail = text.rsplit(":", 1)[1]
    return [int(x) for x in tail.split(">") if x.strip().isdigit()]


def parse_optic_paths(text):
    result = []
    for part in (text or "").split("; "):
        ids = parse_path_ids(part)
        if ids:
            result.append(ids)
    return result

def allowed_aim_levels(merc, wnode, inode, path_nodes, stance, skills):
    wtype = as_int(ntext(wnode, "ubWeaponType"), 0)
    levels = as_int(ntext(wnode, "ubAimLevels"), 0)
    wrange = as_int(ntext(wnode, "usRange"), 0)
    two_handed = as_int(ntext(inode, "TwoHanded"), 0) != 0
    if levels < 1 or levels > 8:
        if wtype in (GUN_PISTOL, GUN_M_PISTOL) or not two_handed:
            levels = 2
        elif wtype in (GUN_SHOTGUN, GUN_LMG, GUN_SMG):
            levels = 3
        elif wtype in (GUN_AS_RIFLE, GUN_RIFLE) and wrange <= 500:
            levels = 4
        elif (wtype in (GUN_AS_RIFLE, GUN_RIFLE) and wrange > 500) or (wtype == GUN_SN_RIFLE and wrange <= 500):
            levels = 6
        elif wtype == GUN_SN_RIFLE:
            levels = 8
        else:
            levels = 4
    levels += round(item_modifier(inode, stance, "AimLevels") + sum(item_modifier(n, stance, "AimLevels") for n in path_nodes))
    if wtype in (GUN_PISTOL, GUN_M_PISTOL):
        levels -= skills["gunslinger_clicks"] * trait_count(merc, GUNSLINGER_NT)
    elif wtype == GUN_RIFLE:
        ranger = skills["ranger_clicks"] * trait_count(merc, RANGER_NT) / 2.0
        sniper = skills["sniper_clicks"] * trait_count(merc, SNIPER_NT)
        levels -= round(max(ranger, sniper))
    elif wtype == GUN_SN_RIFLE:
        levels -= skills["sniper_clicks"] * trait_count(merc, SNIPER_NT)
    return max(1, min(8, int(levels)))


def scope_range_multiplier(merc, mag, c):
    if mag <= 1.0:
        return 1.0
    value = c["SCOPE_RANGE_MULTIPLIER"]
    if mag > 5.0:
        value -= trait_count(merc, SNIPER_NT) * 0.05
    else:
        value -= trait_count(merc, RANGER_NT) * 0.05
    return value

def aggregate_modifier(inode, path_nodes, stance, tag):
    return item_modifier(inode, stance, tag) + sum(item_modifier(node, stance, tag) for node in path_nodes)


def aggregate_value(inode, path_nodes, tag):
    return item_value(inode, tag) + sum(item_value(node, tag) for node in path_nodes)


def trait_cap_bonus(merc, wtype, aim_cap, mag, range_units, best_scope_range, skills, max_cth=99.0):
    if mag <= 1.0 or range_units < best_scope_range or aim_cap >= max_cth:
        return 0.0
    if wtype in (GUN_PISTOL, GUN_M_PISTOL):
        levels = trait_count(merc, GUNSLINGER_NT)
        per_click = skills["gunslinger_bonus"]
    else:
        levels = trait_count(merc, SNIPER_NT)
        per_click = skills["sniper_bonus"]
    difference = 99.0 - aim_cap
    total = 0.0
    for _ in range(levels):
        total += difference * per_click * 2.0 / max_cth
        difference -= total
    return total


def neutral_gun_range(base_range, inode, installed_nodes):
    pct = 100.0 + item_value(inode, "PercentRangeBonus")
    for node in installed_nodes:
        pct *= (100.0 + item_value(node, "PercentRangeBonus")) / 100.0
    flat = item_value(inode, "RangeBonus") + sum(item_value(node, "RangeBonus") for node in installed_nodes)
    return max(0.0, base_range * pct / 100.0 + flat)


def neutral_gun_accuracy(base_accuracy, inode, installed_nodes):
    accuracy = max(0.0, min(100.0, float(base_accuracy)))
    modifier = item_value(inode, "PercentAccuracyModifier") + sum(item_value(node, "PercentAccuracyModifier") for node in installed_nodes)
    accuracy += (100.0 - accuracy) * modifier / 100.0
    return max(0.0, min(100.0, accuracy))


def real_scope_mag(merc, active_mag, range_units, c):
    if active_mag <= 1.0:
        return 1.0
    target_mag = range_units / c["NORMAL_SHOOTING_DISTANCE"]
    range_mod = scope_range_multiplier(merc, active_mag, c)
    return min(active_mag, max(1.0, target_mag / max(0.01, range_mod)))


def effective_scope_mag(merc, active_mag, range_units, c):
    real_mag = real_scope_mag(merc, active_mag, range_units, c)
    if real_mag <= 1.0:
        return real_mag
    max_eff = real_mag * c["SCOPE_EFFECTIVENESS_MULTIPLIER"]
    trait_floor = 0.1
    sniper = trait_count(merc, SNIPER_NT)
    ranger = trait_count(merc, RANGER_NT)
    if sniper >= 2:
        trait_floor = max_eff * c["SCOPE_EFFECTIVENESS_MINIMUM_SNIPER"] / 100.0
    elif sniper == 1:
        trait_floor = max_eff * c["SCOPE_EFFECTIVENESS_MINIMUM_MARKSMAN"] / 100.0
    elif ranger >= 2:
        trait_floor = max_eff * c["SCOPE_EFFECTIVENESS_MINIMUM_RANGER"] / 100.0
    fixed = max_eff * c["SCOPE_EFFECTIVENESS_MINIMUM"] / 100.0
    skill = ((merc["exp"] * 10.0 * c["AIM_EXP"] + merc["mrk"] * c["AIM_MARKS"]) / (c["AIM_EXP"] + c["AIM_MARKS"])) / 100.0
    variable = max_eff * (100.0 - c["SCOPE_EFFECTIVENESS_MINIMUM"]) / 100.0 * skill
    return min(max_eff, max(fixed + variable, trait_floor))


def neutral_aperture_hit_chance(merc, cth, active_mag, range_tiles, gun_range, c):
    range_units = max(1.0, float(range_tiles) * 10.0)
    degrees = c["DEGREES_MAXIMUM_APERTURE"]
    basic = math.sin(math.radians(degrees)) * c["NORMAL_SHOOTING_DISTANCE"]
    real_mag = real_scope_mag(merc, active_mag, range_units, c)
    if real_mag <= 1.0:
        if c["IRON_SIGHTS_MAX_APERTURE_USE_GRADIENT"] > 0:
            mod = max(1.0, c["IRON_SIGHTS_MAX_APERTURE_MODIFIER"])
            basic *= (1.0 / math.sqrt(max(1.0, float(range_tiles))) / mod + (mod - 1.0) / mod)
        basic *= (100.0 - c["IRON_SIGHT_PERFORMANCE_BONUS"]) / 100.0
    eff_mag = effective_scope_mag(merc, active_mag, range_units, c)
    aperture = basic * (range_units / c["NORMAL_SHOOTING_DISTANCE"]) / max(0.01, eff_mag)
    aperture *= (100.0 - float(cth)) / 100.0
    if aperture <= 0:
        chance = 100
    else:
        chance = int(min(100.0, (300.0 / (math.pi * aperture * aperture)) * 100.0))
    max_range = gun_range * c["MAX_EFFECTIVE_RANGE_MULTIPLIER"]
    if gun_range > 0 and range_units > max_range:
        chance = 1
    elif gun_range > 0 and range_units > gun_range:
        reduction = chance * c["MAX_EFFECTIVE_RANGE_REDUCTION"]
        if c["MAX_EFFECTIVE_USE_GRADIENT"] > 0 and max_range > gun_range:
            chance = min(chance, int(chance - reduction * ((range_units - gun_range) / (max_range - gun_range))))
        else:
            chance = int(chance - reduction)
    return max(0, min(100, int(chance))), aperture, real_mag, eff_mag


def neutral_bullet_deviation(accuracy, range_tiles, gun_range, c):
    dev = c["MAX_BULLET_DEV"] * (100.0 - accuracy) / 100.0
    if c["RANGE_EFFECTS_DEV"] > 0 and gun_range > 0:
        dev *= max(1.0, (float(range_tiles) * 10.0) / gun_range)
    return dev / 2.0


def neutral_ncth(merc, wnode, inode, installed_nodes, modifier_nodes, stance, range_tiles, aim_clicks, c, skills, ap_max=100.0, min_cth=1.0, max_cth=99.0, active_mag=None, highest_mounted_mag=None):
    wtype = as_int(ntext(wnode, "ubWeaponType"), 0)
    handling = as_float(ntext(wnode, "Handling"), 0.0)
    ready_reduction = aggregate_value(inode, installed_nodes, "PercentReadyTimeAPReduction")
    gun_difficulty = handling * (100.0 - ready_reduction) / 100.0 * (100.0 / ap_max)
    gun_difficulty *= c[f"BASE_{STANCE_KEY[stance]}_STANCE"]
    gun_difficulty *= 1.0 + aggregate_modifier(inode, modifier_nodes, stance, "PercentHandling") / 100.0

    base = base_attribute(merc, c, min_cth) + aggregate_modifier(inode, modifier_nodes, stance, "FlatBase")
    base_mod = -gun_difficulty * c["BASE_DRAW_COST"]
    base_mod += gun_difficulty * aggregate_modifier(inode, modifier_nodes, stance, "PercentBase") / 100.0
    base = max(0.0, min(100.0, base * (100.0 + base_mod) / 100.0))

    if active_mag is None:
        active_mag = max([item_value(node, "ScopeMagFactor") for node in modifier_nodes] + [item_value(inode, "ScopeMagFactor"), 1.0])
    if highest_mounted_mag is None:
        highest_mounted_mag = max([item_value(node, "ScopeMagFactor") for node in installed_nodes] + [item_value(inode, "ScopeMagFactor"), 1.0])

    if aim_clicks <= 0:
        engine_value = int(max(min_cth, min(max_cth, base)))
        return engine_value, {"base": base, "cap": base, "levels": 0, "scope_penalty": 0.0, "mag": active_mag, "highest_mounted_mag": highest_mounted_mag, "float_final": base}

    range_units = float(range_tiles) * 10.0
    range_mod = scope_range_multiplier(merc, active_mag, c)
    best_scope_range = active_mag * c["NORMAL_SHOOTING_DISTANCE"] * range_mod

    cap = aim_attribute(merc, c)
    cap += trait_cap_bonus(merc, wtype, cap, active_mag, range_units, best_scope_range, skills)
    cap += cap * aggregate_modifier(inode, modifier_nodes, stance, "PercentCap") / 100.0
    cap = max(base, min(max_cth, cap))

    aim_difficulty = handling * (100.0 - ready_reduction) / 100.0 * (100.0 / ap_max)
    aim_difficulty *= c[f"AIM_{STANCE_KEY[stance]}_STANCE"]
    aim_difficulty *= 1.0 + aggregate_modifier(inode, modifier_nodes, stance, "PercentHandling") / 100.0
    aim_modifier = -c["AIM_DRAW_COST"] * aim_difficulty
    aim_modifier += aggregate_modifier(inode, modifier_nodes, stance, "PercentAim")

    scope_penalty = 0.0
    if active_mag > 1.0 and range_units > 0 and range_units < best_scope_range * c["AIM_TOO_CLOSE_THRESHOLD"]:
        ratio = best_scope_range * c["AIM_TOO_CLOSE_THRESHOLD"] / range_units
        scope_penalty = ratio * c["AIM_TOO_CLOSE_SCOPE"] * (active_mag / 2.0)
        aim_modifier += scope_penalty
    elif active_mag <= 1.0 and highest_mounted_mag > 1.0:
        scope_penalty = (highest_mounted_mag / 2.0) * c["AIM_TOO_CLOSE_SCOPE"] / 2.0
        aim_modifier += scope_penalty

    max_bonus = max(0.0, (cap - base) * (100.0 + aim_modifier) / 100.0)
    levels = allowed_aim_levels(merc, wnode, inode, modifier_nodes, stance, skills)
    clicks = max(0, min(int(aim_clicks), levels))
    divisor = levels * (levels + 1) / 2.0
    point_fraction = max_bonus / divisor if divisor else 0.0
    flat_aim = aggregate_modifier(inode, modifier_nodes, stance, "FlatAim")
    aim_points = sum(point_fraction * (levels - x) + flat_aim for x in range(clicks))
    float_final = max(base, min(cap, base + aim_points))

    # CalcNewChanceToHitGun truncates aim points and cap before its final INT32 return.
    engine_final = max(base + int(aim_points), base)
    engine_final = min(engine_final, int(cap))
    engine_final = max(min_cth, min(max_cth, engine_final))
    return int(engine_final), {
        "base": base, "cap": cap, "levels": levels, "scope_penalty": scope_penalty,
        "mag": active_mag, "highest_mounted_mag": highest_mounted_mag, "handling": handling,
        "ready_reduction": ready_reduction, "aim_modifier": aim_modifier, "aim_points": aim_points,
        "float_final": float_final,
    }

def select_mercs(mercs, selectors):
    if not selectors:
        raise SystemExit("at least one --merc selector is required")
    out = []
    for selector in selectors:
        key = selector.casefold()
        matches = [m for m in mercs if key in m["nick"].casefold() or key in m["name"].casefold() or selector == str(m["id"])]
        if not matches:
            raise SystemExit(f"merc not found: {selector}")
        for merc in matches:
            if merc not in out:
                out.append(merc)
    return out


def select_rows(rows, selectors):
    if not selectors:
        return rows
    out = []
    seen = set()
    for selector in selectors:
        if selector.isdigit():
            matches = [row for row in rows if selector == row["id"]]
        else:
            key = selector.casefold()
            matches = [row for row in rows if key == row["weapon"].casefold()]
            if not matches:
                matches = [row for row in rows if key in row["weapon"].casefold()]
        for row in matches:
            identity = row["id"]
            if identity not in seen:
                seen.add(identity)
                out.append(row)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-root", type=Path, help="base game root for Data/Data-1.13 merged INIs")
    ap.add_argument("--mod-root", type=Path, help="optional canonical Vengeance gamedir root for mod INIs")
    ap.add_argument("--items-root", required=True, type=Path)
    ap.add_argument("--merc-profiles", required=True, type=Path)
    ap.add_argument("--compatibility-csv", required=True, type=Path)
    ap.add_argument("--skills-ini", action="append", default=[], type=Path)
    ap.add_argument("--cth-ini", action="append", default=[], type=Path)
    ap.add_argument("--runtime-ini", action="append", default=[], type=Path)
    ap.add_argument("--merc", action="append", default=[])
    ap.add_argument("--weapon", action="append", default=[])
    ap.add_argument("--ranges", default="10,20,35,50")
    ap.add_argument("--stances", default="stand,crouch,prone")
    ap.add_argument("--csv", required=True, type=Path)
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--ap-maximum", type=float, default=100.0)
    args = ap.parse_args()

    if args.game_root:
        auto = live_vr_config_inputs(args.game_root, args.mod_root)
        if not args.cth_ini:
            args.cth_ini = auto["cth"]
        if not args.runtime_ini:
            args.runtime_ini = auto["runtime"]
        if not args.skills_ini:
            args.skills_ini = auto["skills"]
    if not args.skills_ini:
        raise SystemExit("provide --skills-ini or --game-root")

    items = load_items(args.items_root / "Items.xml")
    data_rows = load_data_rows(args.items_root / "Weapons.xml")
    mercs = select_mercs(load_mercs(args.merc_profiles), args.merc)
    constants = load_constants(args.cth_ini)
    limits = load_runtime_limits(args.runtime_ini)
    skills = load_skills(args.skills_ini)

    with args.compatibility_csv.open(encoding="utf-8-sig", newline="") as f:
        compat_rows = select_rows(list(csv.DictReader(f)), args.weapon)
    if not compat_rows:
        raise SystemExit("no matching weapon rows")

    ranges = [as_int(x) for x in args.ranges.split(",") if x.strip()]
    stances = [x.strip().lower() for x in args.stances.split(",") if x.strip()]
    if any(s not in STANCE_XML for s in stances):
        raise SystemExit("stances must be stand,crouch,prone")

    output = []
    for merc in mercs:
        for crow in compat_rows:
            wid = as_int(crow["id"], -1)
            if wid not in data_rows or wid not in items:
                continue
            wnode, inode = data_rows[wid], items[wid]
            paths = [[]] + parse_optic_paths(crow.get("optic_paths", ""))
            seen_paths = set()
            for path_ids in paths:
                key = tuple(path_ids)
                if key in seen_paths:
                    continue
                seen_paths.add(key)
                installed_nodes = [items[x] for x in path_ids if x in items]
                integrated_mag = max(1.0, item_value(inode, "ScopeMagFactor"))

                configs = []
                if path_ids:
                    optic_id = path_ids[-1]
                    optic_node = items.get(optic_id)
                    optic_name = ntext(optic_node, "szItemName", str(optic_id))
                    optic_mag = max(1.0, item_value(optic_node, "ScopeMagFactor"))
                    adapters = installed_nodes[:-1]
                    highest_mag = max(integrated_mag, optic_mag)
                    configs.append(("optic_active", optic_id, optic_name, installed_nodes, optic_mag, highest_mag))
                    configs.append(("1x_optic_mounted", 0, "Iron/1x", adapters, 1.0, highest_mag))
                else:
                    mode = "integrated_optic" if integrated_mag > 1.0 else "1x_unmounted"
                    configs.append((mode, 0, "Integrated" if integrated_mag > 1.0 else "Iron/1x", [], integrated_mag, integrated_mag))

                for optic_mode, optic_id, optic_name, modifier_nodes, active_mag, highest_mag in configs:
                    for stance in stances:
                        levels = allowed_aim_levels(merc, wnode, inode, modifier_nodes, stance, skills)
                        for rtiles in ranges:
                            for clicks in (0, levels):
                                value, detail = neutral_ncth(
                                    merc, wnode, inode, installed_nodes, modifier_nodes, stance, rtiles, clicks,
                                    constants, skills, args.ap_maximum, limits["min_cth"], limits["max_cth"],
                                    active_mag, highest_mag,
                                )
                                gun_range = neutral_gun_range(as_float(crow["range"]), inode, installed_nodes)
                                accuracy = neutral_gun_accuracy(as_float(crow["nAccuracy"]), inode, installed_nodes)
                                hit_chance, aperture, real_mag, effective_mag = neutral_aperture_hit_chance(
                                    merc, value, active_mag, rtiles, gun_range, constants
                                )
                                bullet_dev = neutral_bullet_deviation(accuracy, rtiles, gun_range, constants)
                                output.append({
                                    "merc_id": merc["id"], "merc": merc["nick"], "exp": merc["exp"],
                                    "mrk": merc["mrk"], "dex": merc["dex"], "wis": merc["wis"],
                                    "traits": "/".join(str(x) for x in merc["traits"] if x),
                                    "weapon_id": wid, "weapon": crow["weapon"], "weapon_type": crow["weapon_type"],
                                    "nAccuracy": crow["nAccuracy"], "effective_accuracy": round(accuracy, 3),
                                    "weapon_range_units": round(gun_range, 3), "handling": crow["handling"],
                                    "recoil_magnitude": crow["recoil_magnitude"], "optic_mode": optic_mode,
                                    "optic_id": optic_id, "optic": optic_name, "path": ">".join(str(x) for x in path_ids),
                                    "scope_mag": round(detail["mag"], 3),
                                    "highest_mounted_scope_mag": round(detail["highest_mounted_mag"], 3),
                                    "stance": stance, "range_tiles": rtiles, "aim_clicks": clicks,
                                    "allowed_aim_levels": levels, "neutral_ncth": value,
                                    "neutral_ncth_float": round(detail["float_final"], 3),
                                    "neutral_aperture_hit_pct": hit_chance, "neutral_aperture": round(aperture, 4),
                                    "real_scope_mag": round(real_mag, 3), "effective_scope_mag": round(effective_mag, 3),
                                    "bullet_deviation_radius": round(bullet_dev, 4),
                                    "base_ncth": round(detail["base"], 3), "aim_cap": round(detail["cap"], 3),
                                    "scope_penalty_pct": round(detail["scope_penalty"], 3),
                                    "aim_modifier_pct": round(detail.get("aim_modifier", 0.0), 3),
                                })

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(output[0].keys()))
        writer.writeheader()
        writer.writerows(output)

    manifest = {
        "model": "neutral deterministic NCTH core",
        "game_root": str(args.game_root) if args.game_root else None,
        "mod_root": str(args.mod_root) if args.mod_root else None,
        "cth_ini": [str(x) for x in args.cth_ini],
        "runtime_ini": [str(x) for x in args.runtime_ini],
        "skills_ini": [str(x) for x in args.skills_ini],
        "input_files": {
            "items": source_record(args.items_root / "Items.xml"),
            "weapons": source_record(args.items_root / "Weapons.xml"),
            "merc_profiles": source_record(args.merc_profiles),
            "compatibility_csv": source_record(args.compatibility_csv),
            "cth_ini": [source_record(x) for x in args.cth_ini],
            "runtime_ini": [source_record(x) for x in args.runtime_ini],
            "skills_ini": [source_record(x) for x in args.skills_ini],
        },
        "constants": constants,
        "limits": limits,
        "skills": skills,
        "mercs": [m["nick"] for m in mercs],
        "weapons": len(compat_rows),
        "weapon_selection": [{"id": int(row["id"]), "weapon": row["weapon"]} for row in compat_rows],
        "rows": len(output),
        "ranges_tiles": ranges,
        "stances": stances,
        "ap_maximum": args.ap_maximum,
    }
    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(f"MERCS={len(mercs)} WEAPONS={len(compat_rows)} ROWS={len(output)}")
    print(f"MODEL=neutral deterministic NCTH core; scope-mode active/inactive states explicit; limits={limits['min_cth']}/{limits['max_cth']}; dynamic battlefield modifiers intentionally excluded")
    print("CTH_INI:", " | ".join(str(x) for x in args.cth_ini))
    print("RUNTIME_INI:", " | ".join(str(x) for x in args.runtime_ini))
    print("SKILLS_INI:", " | ".join(str(x) for x in args.skills_ini))
    print("CSV:", args.csv)
    if args.manifest:
        print("MANIFEST:", args.manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
