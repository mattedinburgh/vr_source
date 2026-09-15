#!/usr/bin/env python3
"""Vengeance Reloaded Campaign Companion.

Consumes VR_BlackBox.jsonl and produces a refinement report.

The Black Box is forensic: it records what the engine considered, selected and
what happened afterwards.  The Companion is analytical: it aggregates those
traces, compares builds/sessions and highlights likely refinement targets.

Usage:
    python tools/vr_companion_analyze.py VR_BlackBox.jsonl
    python tools/vr_companion_analyze.py VR_BlackBox.jsonl --baseline previous.jsonl
    python tools/vr_companion_analyze.py VR_BlackBox.jsonl -o VR_Companion_Report.md
"""

from __future__ import annotations

import argparse
import json
import math
import os
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


def load_events_with_diagnostics(
    path: Path, strict: bool = False
) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    """Load Black Box JSONL without throwing away an otherwise usable crash trace.

    A process crash can interrupt the final fwrite and leave only the last record
    truncated.  In normal mode we preserve every earlier valid record and report
    that damaged tail.  --strict-jsonl restores fail-fast behaviour for CI and
    format validation.
    """
    events: List[Dict[str, Any]] = []
    diagnostics: Dict[str, Any] = {
        "source": str(path),
        "invalid_lines": 0,
        "skipped_truncated_tail": 0,
        "foreign_schema_lines": 0,
        "first_invalid_line": None,
    }

    raw_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    last_nonempty = 0
    for index, raw in enumerate(raw_lines, 1):
        if raw.strip():
            last_nonempty = index

    for line_no, raw in enumerate(raw_lines, 1):
        raw = raw.strip()
        if not raw:
            continue
        try:
            event = json.loads(raw)
        except json.JSONDecodeError as exc:
            diagnostics["invalid_lines"] += 1
            if diagnostics["first_invalid_line"] is None:
                diagnostics["first_invalid_line"] = line_no

            is_tail = line_no == last_nonempty
            if is_tail and not strict:
                diagnostics["skipped_truncated_tail"] += 1
                print(
                    f"{path}:{line_no}: warning: ignoring truncated final JSONL record: {exc}",
                    file=sys.stderr,
                )
                continue
            raise SystemExit(f"{path}:{line_no}: invalid JSONL: {exc}")

        if not isinstance(event, dict):
            if strict:
                raise SystemExit(f"{path}:{line_no}: Black Box event is not a JSON object")
            diagnostics["foreign_schema_lines"] += 1
            continue
        if event.get("schema") != "vr-blackbox-1":
            diagnostics["foreign_schema_lines"] += 1
            continue
        events.append(event)

    diagnostics["loaded_events"] = len(events)
    return events, diagnostics


def load_events(path: Path) -> List[Dict[str, Any]]:
    events, _ = load_events_with_diagnostics(path)
    return events


def event_integrity_summary(
    events: List[Dict[str, Any]], ingestion: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    seen = set()
    duplicates = 0
    regressions = 0
    missing_sequence_ids = 0
    last_by_session: Dict[Any, int] = {}

    for event in events:
        session = event.get("session")
        seq = event.get("seq")
        if not isinstance(seq, int):
            continue

        key = (session, seq)
        if key in seen:
            duplicates += 1
        seen.add(key)

        previous = last_by_session.get(session)
        if previous is not None:
            if seq <= previous:
                regressions += 1
            elif seq > previous + 1:
                missing_sequence_ids += seq - previous - 1
        last_by_session[session] = seq

    result = {
        "duplicate_sequence_ids": duplicates,
        "sequence_regressions": regressions,
        "missing_sequence_ids": missing_sequence_ids,
        "ingestion": dict(ingestion or {}),
    }
    return result


def write_text_atomic(path: Path, text: str) -> None:
    """Replace a report only after the complete new file has reached disk."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with tmp.open("w", encoding="utf-8", newline="") as handle:
        handle.write(text)
        handle.flush()
        os.fsync(handle.fileno())
    tmp.replace(path)


def parse_detail(detail: Any) -> Dict[str, str]:
    if not isinstance(detail, str):
        return {}
    result: Dict[str, str] = {}
    for item in detail.split(";"):
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def build_decisions(events: Iterable[Dict[str, Any]]) -> Dict[Tuple[Any, int], Dict[str, Any]]:
    decisions: Dict[Tuple[Any, int], Dict[str, Any]] = {}
    for event in events:
        decision_id = event.get("decision_id")
        if not isinstance(decision_id, int):
            continue

        key = (event.get("session"), decision_id)
        record = decisions.setdefault(
            key,
            {
                "id": decision_id,
                "session": event.get("session"),
                "layer": event.get("layer"),
                "begin": None,
                "states": {},
                "assessments": [],
                "candidates": [],
                "commits": [],
                "outcomes": [],
            },
        )
        record["layer"] = record.get("layer") or event.get("layer")
        kind = event.get("kind")
        if kind == "decision_begin":
            record["begin"] = event
            record["layer"] = event.get("layer")
        elif kind == "state":
            record["states"][event.get("key")] = event.get("value")
        elif kind == "assessment":
            record["assessments"].append(event)
            metadata = {
                "schema", "seq", "session", "layer", "kind", "decision_id",
                "battle_id", "assessment_type", "actor_id",
            }
            for field, value in event.items():
                if field in metadata:
                    continue
                if isinstance(value, bool):
                    record["states"][field] = 1 if value else 0
                elif isinstance(value, (int, float)):
                    record["states"][field] = value
        elif kind == "candidate":
            record["candidates"].append(event)
        elif kind == "decision_commit":
            record["commits"].append(event)
        elif kind == "outcome":
            record["outcomes"].append(event)
    return decisions


def safe_mean(values: Iterable[float]) -> Optional[float]:
    seq = [float(v) for v in values]
    return statistics.mean(seq) if seq else None


def pct(numerator: float, denominator: float) -> float:
    return 100.0 * numerator / denominator if denominator else 0.0


def grenade_throw_summary(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    launches = [
        event for event in events
        if event.get("layer") == "tactical"
        and event.get("kind") == "grenade_throw_launch"
    ]
    landings = [
        event for event in events
        if event.get("layer") == "tactical"
        and event.get("kind") == "grenade_throw_landing"
    ]

    pending: Dict[Tuple[Any, Any], Dict[str, Any]] = {}
    pairs: List[Tuple[Dict[str, Any], Dict[str, Any]]] = []
    all_flight_events = sorted(
        launches + landings,
        key=lambda event: (
            int(event.get("session", 0) or 0),
            int(event.get("seq", 0) or 0),
        ),
    )
    for event in all_flight_events:
        key = (event.get("session"), event.get("projectile_id"))
        if event.get("kind") == "grenade_throw_launch":
            pending[key] = event
        elif key in pending:
            pairs.append((pending.pop(key), event))

    max_ranges = [
        float(event["max_range"]) for event in launches
        if isinstance(event.get("max_range"), (int, float))
    ]
    target_distances = [
        float(event["target_distance"]) for event in launches
        if isinstance(event.get("target_distance"), (int, float))
    ]
    strengths = [
        float(event["effective_strength"]) for event in launches
        if isinstance(event.get("effective_strength"), (int, float))
    ]
    breath_pct = [
        100.0 * float(event["breath"]) / float(event["breath_max"])
        for event in launches
        if isinstance(event.get("breath"), (int, float))
        and isinstance(event.get("breath_max"), (int, float))
        and float(event["breath_max"]) > 0
    ]
    target_offsets = [
        float(event["target_offset_to_nearest_player"]) for event in launches
        if isinstance(event.get("target_offset_to_nearest_player"), (int, float))
        and float(event["target_offset_to_nearest_player"]) >= 0
    ]
    actual_distances: List[float] = []
    landing_offsets: List[float] = []
    overrange_records: List[Dict[str, Any]] = []
    targeted_beyond_records: List[Dict[str, Any]] = []

    for launch in launches:
        target_distance = launch.get("target_distance")
        max_range = launch.get("max_range")
        if (
            isinstance(target_distance, (int, float))
            and isinstance(max_range, (int, float))
            and float(target_distance) > float(max_range)
        ):
            targeted_beyond_records.append({
                "session": launch.get("session"),
                "actor_id": launch.get("actor_id"),
                "projectile_id": launch.get("projectile_id"),
                "item": launch.get("item"),
                "target_distance": target_distance,
                "max_range": max_range,
                "distance_to_nearest_player": launch.get("distance_to_nearest_player"),
                "target_offset_to_nearest_player": launch.get("target_offset_to_nearest_player"),
            })

    for launch, landing in pairs:
        actual = landing.get("actual_distance")
        max_range = launch.get("max_range")
        if isinstance(actual, (int, float)) and float(actual) >= 0:
            actual_distances.append(float(actual))
        offset = landing.get("landing_offset_to_nearest_player")
        if isinstance(offset, (int, float)) and float(offset) >= 0:
            landing_offsets.append(float(offset))
        # Allow one tile for discrete-grid/first-move bookkeeping. Anything
        # beyond that is a high-confidence range-rule anomaly.
        if (
            isinstance(actual, (int, float))
            and isinstance(max_range, (int, float))
            and float(actual) >= 0
            and float(actual) > float(max_range) + 1.0
        ):
            overrange_records.append({
                "session": launch.get("session"),
                "actor_id": launch.get("actor_id"),
                "projectile_id": launch.get("projectile_id"),
                "item": launch.get("item"),
                "max_range": max_range,
                "target_distance": launch.get("target_distance"),
                "actual_distance": actual,
                "strength": launch.get("effective_strength"),
                "breath": launch.get("breath"),
                "breath_max": launch.get("breath_max"),
                "stance": launch.get("stance"),
                "throwing_traits": launch.get("throwing_traits"),
            })

    stance_counts = Counter(str(event.get("stance", "unknown")) for event in launches)
    trait_counts = Counter(str(event.get("throwing_traits", "unknown")) for event in launches)

    return {
        "launches": len(launches),
        "landings": len(landings),
        "paired_flights": len(pairs),
        "unmatched_launches": len(pending),
        "avg_max_range": safe_mean(max_ranges),
        "avg_target_distance": safe_mean(target_distances),
        "avg_actual_distance": safe_mean(actual_distances),
        "avg_effective_strength": safe_mean(strengths),
        "avg_breath_pct": safe_mean(breath_pct),
        "avg_target_offset_to_player": safe_mean(target_offsets),
        "avg_landing_offset_to_player": safe_mean(landing_offsets),
        "targeted_beyond_range": len(targeted_beyond_records),
        "actual_overrange": len(overrange_records),
        "stance_counts": dict(stance_counts.most_common()),
        "throwing_trait_counts": dict(trait_counts.most_common()),
        "targeted_beyond_records": targeted_beyond_records[:20],
        "overrange_records": overrange_records[:20],
    }


def tactical_summary(
    events: List[Dict[str, Any]], decisions: Dict[Tuple[Any, int], Dict[str, Any]]
) -> Dict[str, Any]:
    tactical = [d for d in decisions.values() if d.get("layer") == "tactical"]
    commits = [c for d in tactical for c in d["commits"] if "action" in c]
    outcomes = [o for d in tactical for o in d["outcomes"]]

    status = Counter(o.get("status", "unknown") for o in outcomes)
    actions = Counter(str(c.get("action")) for c in commits)

    ap_spent: List[float] = []
    grid_delta: List[float] = []
    last_attack_hit_flags = 0
    completed_action_samples = 0

    for outcome in outcomes:
        if outcome.get("metric_a") == "grid_delta":
            grid_delta.append(abs(float(outcome.get("value_a", 0))))
        if outcome.get("metric_b") == "ap_spent":
            value = float(outcome.get("value_b", 0))
            # -1 means the action crossed an AP refresh/turn boundary and the
            # delta is not a valid action cost. Never average it as real AP.
            if value >= 0:
                ap_spent.append(value)
        detail = parse_detail(outcome.get("detail"))
        if "last_attack_hit" in detail:
            completed_action_samples += 1
            last_attack_hit_flags += 1 if detail["last_attack_hit"] == "1" else 0

    attack_cover_pairs = 0
    defense_wins = 0
    offense_wins = 0
    ties = 0
    score_gaps: List[float] = []
    for decision in tactical:
        attack = None
        cover = None
        for candidate in decision["candidates"]:
            if candidate.get("candidate") == "attack_option":
                attack = candidate
            elif candidate.get("candidate") == "cover_option":
                cover = candidate
        if attack and cover:
            attack_cover_pairs += 1
            a = float(attack.get("adjusted_score", 0))
            c = float(cover.get("adjusted_score", 0))
            score_gaps.append(c - a)
            if c > a:
                defense_wins += 1
            elif a > c:
                offense_wins += 1
            else:
                ties += 1

    # Black Box v2 decision-forensics: summarize both actor perception and
    # omniscient formation snapshots without mixing the two information scopes.
    state_samples: Dict[str, List[float]] = defaultdict(list)
    perceived_force_ratios: List[float] = []
    retreat_candidate_types = Counter()
    retreat_eligible = Counter()

    for decision in tactical:
        # Prefer every compact v2 assessment sample. Fall back to the merged
        # state dictionary for older logs and planner-only decisions.
        assessments = decision.get("assessments", [])
        if assessments:
            for assessment in assessments:
                for key, value in assessment.items():
                    if key in {
                        "schema", "seq", "session", "layer", "kind",
                        "decision_id", "battle_id", "assessment_type", "actor_id",
                    }:
                        continue
                    if isinstance(value, bool):
                        state_samples[str(key)].append(1.0 if value else 0.0)
                    elif isinstance(value, (int, float)):
                        state_samples[str(key)].append(float(value))

                friends = assessment.get("perceived_friendly_strength")
                enemies = assessment.get("perceived_enemy_strength")
                if (
                    isinstance(friends, (int, float))
                    and isinstance(enemies, (int, float))
                    and float(enemies) > 0
                ):
                    perceived_force_ratios.append(float(friends) / float(enemies))
        else:
            for key, value in decision["states"].items():
                if isinstance(value, (int, float)):
                    state_samples[str(key)].append(float(value))

            friends = decision["states"].get("perceived_friendly_strength")
            enemies = decision["states"].get("perceived_enemy_strength")
            if (
                isinstance(friends, (int, float))
                and isinstance(enemies, (int, float))
                and float(enemies) > 0
            ):
                perceived_force_ratios.append(float(friends) / float(enemies))

        for candidate in decision["candidates"]:
            name = str(candidate.get("candidate", "unknown"))
            if name in {"hold_ground", "organized_disengagement", "sector_escape"}:
                retreat_candidate_types[name] += 1
                if candidate.get("eligible"):
                    retreat_eligible[name] += 1

    formation_snapshots = [
        event
        for event in events
        if event.get("layer") == "tactical"
        and event.get("kind") == "formation_snapshot"
    ]
    ready_ratios: List[float] = []
    formation_stress: List[float] = []
    formation_casualties: List[float] = []
    peak_escaping = 0
    peak_disengaging = 0
    peak_cowering = 0

    for snap in formation_snapshots:
        living = snap.get("living")
        ready = snap.get("combat_ready")
        if (
            isinstance(living, (int, float))
            and isinstance(ready, (int, float))
            and float(living) > 0
        ):
            ready_ratios.append(float(ready) / float(living))
        if isinstance(snap.get("average_stress"), (int, float)):
            formation_stress.append(float(snap["average_stress"]))
        if isinstance(snap.get("casualty_percent"), (int, float)):
            formation_casualties.append(float(snap["casualty_percent"]))
        if isinstance(snap.get("escaping"), (int, float)):
            peak_escaping = max(peak_escaping, int(snap["escaping"]))
        if isinstance(snap.get("disengaging"), (int, float)):
            peak_disengaging = max(peak_disengaging, int(snap["disengaging"]))
        if isinstance(snap.get("cowering"), (int, float)):
            peak_cowering = max(peak_cowering, int(snap["cowering"]))

    diagnostics = Counter(
        event.get("code", "unknown")
        for event in events
        if event.get("layer") == "tactical" and event.get("kind") == "diagnostic"
    )

    combat_hits = [
        event for event in events
        if event.get("layer") == "tactical" and event.get("kind") == "combat_hit"
    ]
    damage_events = [
        event for event in events
        if event.get("layer") == "tactical" and event.get("kind") == "damage_applied"
    ]
    damage_by_reason = Counter(
        str(event.get("damage_reason", "unknown")) for event in damage_events
    )
    total_life_loss = sum(
        max(0.0, float(event.get("actual_life_loss", 0)))
        for event in damage_events
        if isinstance(event.get("actual_life_loss"), (int, float))
    )
    total_breath_loss = sum(
        max(0.0, float(event.get("actual_breath_loss", 0)))
        for event in damage_events
        if isinstance(event.get("actual_breath_loss"), (int, float))
    )
    entered_downed = sum(1 for event in damage_events if event.get("entered_downed"))
    lethal_damage_events = sum(1 for event in damage_events if event.get("killed"))

    total_outcomes = sum(status.values())
    return {
        "decisions": len(tactical),
        "commits": len(commits),
        "outcomes": total_outcomes,
        "completed": status.get("completed", 0),
        "rejected": status.get("rejected", 0),
        "superseded": status.get("superseded", 0),
        "completed_rate": pct(status.get("completed", 0), total_outcomes),
        "rejected_rate": pct(status.get("rejected", 0), total_outcomes),
        "superseded_rate": pct(status.get("superseded", 0), total_outcomes),
        "avg_ap_spent": safe_mean(ap_spent),
        "valid_ap_samples": len(ap_spent),
        "avg_abs_grid_delta": safe_mean(grid_delta),
        "combat_hits": len(combat_hits),
        "damage_events": len(damage_events),
        "total_life_loss": total_life_loss,
        "total_breath_loss": total_breath_loss,
        "entered_downed": entered_downed,
        "lethal_damage_events": lethal_damage_events,
        "damage_by_reason": dict(damage_by_reason.most_common()),
        "last_attack_hit_flag_rate": pct(last_attack_hit_flags, completed_action_samples),
        "completed_action_samples": completed_action_samples,
        "action_counts": dict(actions.most_common()),
        "attack_cover_pairs": attack_cover_pairs,
        "defense_wins": defense_wins,
        "offense_wins": offense_wins,
        "ties": ties,
        "avg_cover_minus_attack_score": safe_mean(score_gaps),
        "v2_state_means": {
            key: safe_mean(values) for key, values in sorted(state_samples.items())
        },
        "avg_perceived_force_ratio": safe_mean(perceived_force_ratios),
        "retreat_candidate_types": dict(retreat_candidate_types.most_common()),
        "retreat_eligible": dict(retreat_eligible.most_common()),
        "formation_snapshots": len(formation_snapshots),
        "avg_formation_ready_rate": (
            100.0 * safe_mean(ready_ratios)
            if safe_mean(ready_ratios) is not None
            else None
        ),
        "avg_formation_stress": safe_mean(formation_stress),
        "avg_formation_casualty_pct": safe_mean(formation_casualties),
        "peak_escaping": peak_escaping,
        "peak_disengaging": peak_disengaging,
        "peak_cowering": peak_cowering,
        "diagnostics": dict(diagnostics.most_common()),
    }


def battle_summary(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    starts = {
        (event.get("session"), event.get("battle_id")): event
        for event in events
        if event.get("kind") == "battle_start" and isinstance(event.get("battle_id"), int)
    }
    ends = [
        event
        for event in events
        if event.get("kind") == "battle_end" and isinstance(event.get("battle_id"), int)
    ]

    results = Counter(event.get("result", "unknown") for event in ends)
    resolved_ids = {(event.get("session"), event.get("battle_id")) for event in ends}
    unresolved_keys = sorted(
        (session, battle_id)
        for session, battle_id in starts
        if (session, battle_id) not in resolved_ids
    )
    unresolved_ids = [f"{session}:{battle_id}" for session, battle_id in unresolved_keys]

    decision_counts = Counter(
        (event.get("session"), event.get("battle_id"))
        for event in events
        if event.get("layer") == "tactical"
        and event.get("kind") == "decision_begin"
        and isinstance(event.get("battle_id"), int)
    )

    durations: List[float] = []
    for event in ends:
        start = starts.get((event.get("session"), event.get("battle_id")))
        if not start:
            continue
        start_minute = start.get("world_minutes")
        end_minute = event.get("world_minutes")
        if isinstance(start_minute, (int, float)) and isinstance(end_minute, (int, float)):
            durations.append(max(0.0, float(end_minute) - float(start_minute)))

    player_deltas = [
        float(event.get("player_count_delta", 0))
        for event in ends
        if isinstance(event.get("player_count_delta"), (int, float))
    ]
    enemy_deltas = [
        float(event.get("enemy_count_delta", 0))
        for event in ends
        if isinstance(event.get("enemy_count_delta"), (int, float))
    ]
    militia_deltas = [
        float(event.get("militia_count_delta", 0))
        for event in ends
        if isinstance(event.get("militia_count_delta"), (int, float))
    ]

    player_successes = results.get("victory", 0) + results.get("enemy_retreat", 0)
    resolved = len(ends)

    return {
        "starts": len(starts),
        "resolved": resolved,
        "unresolved": len(unresolved_ids),
        "unresolved_ids": unresolved_ids,
        "results": dict(results.most_common()),
        "player_success_rate": pct(player_successes, resolved),
        "avg_player_count_delta": safe_mean(player_deltas),
        "avg_enemy_count_delta": safe_mean(enemy_deltas),
        "avg_militia_count_delta": safe_mean(militia_deltas),
        "avg_duration_minutes": safe_mean(durations),
        "avg_tactical_decisions": safe_mean(decision_counts.values()),
    }


def interaction_summary(
    events: List[Dict[str, Any]], lookback_minutes: int = 48 * 60
) -> Dict[str, Any]:
    starts = {
        (event.get("session"), event.get("battle_id")): event
        for event in events
        if event.get("kind") == "battle_start"
        and isinstance(event.get("battle_id"), int)
    }
    ends = {
        (event.get("session"), event.get("battle_id")): event
        for event in events
        if event.get("kind") == "battle_end"
        and isinstance(event.get("battle_id"), int)
    }
    arrivals = [
        event for event in events if event.get("kind") == "strategic_group_arrived"
    ]

    reinforced_results = Counter()
    unreinforced_results = Counter()
    reinforced_battles = 0
    unreinforced_battles = 0
    reinforcement_sizes: List[float] = []
    reinforced_decisions: List[float] = []
    unreinforced_decisions: List[float] = []
    decision_counts = Counter(
        (event.get("session"), event.get("battle_id"))
        for event in events
        if event.get("layer") == "tactical"
        and event.get("kind") == "decision_begin"
        and isinstance(event.get("battle_id"), int)
    )
    records: List[Dict[str, Any]] = []

    for key, start in starts.items():
        end = ends.get(key)
        world_minutes = start.get("world_minutes")
        x = start.get("sector_x")
        y = start.get("sector_y")
        if not isinstance(world_minutes, (int, float)):
            continue
        if not isinstance(x, int) or not isinstance(y, int):
            continue

        # Strategic AI sector IDs are 0-based over the 16x16 playable grid.
        sector_id = (y - 1) * 16 + (x - 1)
        recent = [
            event
            for event in arrivals
            if event.get("session") == start.get("session")
            and event.get("sector") == sector_id
            and isinstance(event.get("world_minutes"), (int, float))
            and 0
            <= float(world_minutes) - float(event.get("world_minutes"))
            <= lookback_minutes
        ]
        reinforced = bool(recent)
        arrived_troops = sum(
            float(event.get("group_size", 0))
            for event in recent
            if isinstance(event.get("group_size"), (int, float))
        )

        tactical_decisions = float(decision_counts.get(key, 0))
        if reinforced:
            reinforced_battles += 1
            reinforcement_sizes.append(arrived_troops)
            reinforced_decisions.append(tactical_decisions)
        else:
            unreinforced_battles += 1
            unreinforced_decisions.append(tactical_decisions)

        result = end.get("result") if end else None
        if result:
            if reinforced:
                reinforced_results[result] += 1
            else:
                unreinforced_results[result] += 1

        records.append(
            {
                "session": start.get("session"),
                "battle_id": start.get("battle_id"),
                "sector_id": sector_id,
                "world_minutes": world_minutes,
                "recent_reinforcement_groups": len(recent),
                "recent_reinforcement_troops": arrived_troops,
                "tactical_decisions": tactical_decisions,
                "result": result,
            }
        )

    reinforced_resolved = sum(reinforced_results.values())
    unreinforced_resolved = sum(unreinforced_results.values())
    reinforced_player_success = (
        reinforced_results.get("victory", 0)
        + reinforced_results.get("enemy_retreat", 0)
    )
    unreinforced_player_success = (
        unreinforced_results.get("victory", 0)
        + unreinforced_results.get("enemy_retreat", 0)
    )

    return {
        "lookback_minutes": lookback_minutes,
        "reinforced_battles": reinforced_battles,
        "unreinforced_battles": unreinforced_battles,
        "avg_recent_reinforcement_troops": safe_mean(reinforcement_sizes),
        "avg_tactical_decisions_reinforced": safe_mean(reinforced_decisions),
        "avg_tactical_decisions_unreinforced": safe_mean(unreinforced_decisions),
        "reinforced_player_success_rate": pct(
            reinforced_player_success, reinforced_resolved
        ),
        "unreinforced_player_success_rate": pct(
            unreinforced_player_success, unreinforced_resolved
        ),
        "reinforced_resolved": reinforced_resolved,
        "unreinforced_resolved": unreinforced_resolved,
        "battle_records": records,
    }


def strategic_mobility_summary(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    orders = [
        event for event in events if event.get("kind") == "strategic_move_order"
    ]
    arrivals = [
        event for event in events if event.get("kind") == "strategic_group_arrived"
    ]

    # Pair an arrival with the latest preceding order for the same group and
    # session whose target matches the arrival sector. This handles redirects.
    orders_by_group: Dict[Tuple[Any, Any], List[Dict[str, Any]]] = defaultdict(list)
    for order in orders:
        orders_by_group[(order.get("session"), order.get("group_id"))].append(order)
    for seq in orders_by_group.values():
        seq.sort(key=lambda event: int(event.get("seq", 0)))

    matched = 0
    travel_minutes: List[float] = []
    unmatched_arrivals = 0
    assignments = Counter()

    for arrival in arrivals:
        assignments[arrival.get("assignment", "unknown")] += 1
        candidates = orders_by_group.get(
            (arrival.get("session"), arrival.get("group_id")), []
        )
        prior = [
            order
            for order in candidates
            if int(order.get("seq", 0)) < int(arrival.get("seq", 0))
            and order.get("target_sector") == arrival.get("sector")
        ]
        if not prior:
            unmatched_arrivals += 1
            continue
        order = prior[-1]
        matched += 1
        start = order.get("world_minutes")
        end = arrival.get("world_minutes")
        if isinstance(start, (int, float)) and isinstance(end, (int, float)):
            travel_minutes.append(max(0.0, float(end) - float(start)))

    ordered_groups = {
        (event.get("session"), event.get("group_id"), event.get("target_sector"))
        for event in orders
    }
    arrived_groups = {
        (event.get("session"), event.get("group_id"), event.get("sector"))
        for event in arrivals
    }
    outstanding = len(ordered_groups - arrived_groups)

    return {
        "orders": len(orders),
        "arrivals": len(arrivals),
        "matched_arrivals": matched,
        "unmatched_arrivals": unmatched_arrivals,
        "outstanding_order_keys": outstanding,
        "arrival_match_rate": pct(matched, len(arrivals)),
        "avg_travel_minutes": safe_mean(travel_minutes),
        "assignments": dict(assignments.most_common()),
    }


def strategic_summary(
    events: List[Dict[str, Any]], decisions: Dict[Tuple[Any, int], Dict[str, Any]]
) -> Dict[str, Any]:
    strategic = [d for d in decisions.values() if d.get("layer") == "strategic"]
    commits = [c for d in strategic for c in d["commits"]]
    selections = Counter(c.get("selection", "unknown") for c in commits)
    reasons = Counter(c.get("reason", "unknown") for c in commits)
    candidates = [c for d in strategic for c in d["candidates"]]

    candidate_types = Counter(c.get("candidate", "unknown") for c in candidates)
    eligible_scores = [
        float(c.get("adjusted_score", 0)) for c in candidates if c.get("eligible")
    ]
    rejected_candidates = [c for c in candidates if not c.get("eligible")]
    rejection_reasons = Counter(
        c.get("reason", "unknown") for c in rejected_candidates
    )

    no_action = selections.get("no_action", 0)
    diagnostics = Counter(
        event.get("code", "unknown")
        for event in events
        if event.get("layer") == "strategic" and event.get("kind") == "diagnostic"
    )

    state_samples: Dict[str, List[float]] = defaultdict(list)
    for decision in strategic:
        for key, value in decision["states"].items():
            if isinstance(value, (int, float)):
                state_samples[str(key)].append(float(value))

    return {
        "decisions": len(strategic),
        "commits": len(commits),
        "no_action": no_action,
        "no_action_rate": pct(no_action, len(commits)),
        "selections": dict(selections.most_common()),
        "reasons": dict(reasons.most_common()),
        "candidate_types": dict(candidate_types.most_common()),
        "eligible_candidates": len(candidates) - len(rejected_candidates),
        "rejected_candidates": len(rejected_candidates),
        "candidate_rejection_reasons": dict(rejection_reasons.most_common()),
        "avg_candidate_score": safe_mean(eligible_scores),
        "avg_candidates_per_decision": (
            float(len(candidates)) / len(strategic) if strategic else 0.0
        ),
        "state_means": {
            key: safe_mean(values) for key, values in sorted(state_samples.items())
        },
        "diagnostics": dict(diagnostics.most_common()),
    }


def session_summary(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    starts = [e for e in events if e.get("kind") == "session_start"]
    sessions = sorted({e.get("session") for e in events if e.get("session") is not None})
    return {
        "events": len(events),
        "sessions": sessions,
        "builds": [
            {
                "session": e.get("session"),
                "build_date": e.get("build_date"),
                "build_time": e.get("build_time"),
                "experiment_tag": e.get("experiment_tag", "unlabeled"),
                "build_branch": e.get("build_branch"),
                "build_commit": e.get("build_commit"),
                "build_commit_short": e.get("build_commit_short"),
                "build_dirty": e.get("build_dirty"),
                "build_source_fingerprint": e.get("build_source_fingerprint"),
                "build_generated_at": e.get("build_generated_at"),
                "build_configuration": e.get("build_configuration"),
                "build_platform": e.get("build_platform"),
                "build_target": e.get("build_target"),
                "build_provenance_version": e.get("build_provenance_version"),
                "recent_changes": e.get("recent_changes", []),
            }
            for e in starts
        ],
    }


def summarize(
    events: List[Dict[str, Any]],
    ingestion: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    decisions = build_decisions(events)
    return {
        "session": session_summary(events),
        "integrity": event_integrity_summary(events, ingestion),
        "battle": battle_summary(events),
        "tactical": tactical_summary(events, decisions),
        "grenades": grenade_throw_summary(events),
        "interaction": interaction_summary(events),
        "strategic_mobility": strategic_mobility_summary(events),
        "strategic": strategic_summary(events, decisions),
    }


def fmt(value: Any, digits: int = 1) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        if math.isnan(value):
            return "n/a"
        return f"{value:.{digits}f}"
    return str(value)


def comparison_rows(current: Dict[str, Any], baseline: Dict[str, Any]) -> List[Tuple[str, float, float, float]]:
    paths = [
        ("Tactical completion rate %", "tactical", "completed_rate"),
        ("Tactical rejected rate %", "tactical", "rejected_rate"),
        ("Tactical superseded rate %", "tactical", "superseded_rate"),
        ("Tactical average AP spent", "tactical", "avg_ap_spent"),
        ("Completed actions with last-attack-hit flag %", "tactical", "last_attack_hit_flag_rate"),
        ("Battle player success rate %", "battle", "player_success_rate"),
        ("Battle average player-count delta", "battle", "avg_player_count_delta"),
        ("Battle average enemy-count delta", "battle", "avg_enemy_count_delta"),
        ("Battle average duration minutes", "battle", "avg_duration_minutes"),
        ("Battle average tactical decisions", "battle", "avg_tactical_decisions"),
        ("Player success after recent enemy reinforcement %", "interaction", "reinforced_player_success_rate"),
        ("Player success without recent enemy reinforcement %", "interaction", "unreinforced_player_success_rate"),
        ("Cover - attack adjusted score", "tactical", "avg_cover_minus_attack_score"),
        ("Strategic move arrival match rate %", "strategic_mobility", "arrival_match_rate"),
        ("Strategic average travel minutes", "strategic_mobility", "avg_travel_minutes"),
        ("Strategic no-action rate %", "strategic", "no_action_rate"),
        ("Strategic candidates / decision", "strategic", "avg_candidates_per_decision"),
        ("Strategic average candidate score", "strategic", "avg_candidate_score"),
    ]
    rows: List[Tuple[str, float, float, float]] = []
    for label, section, key in paths:
        cur = current[section].get(key)
        base = baseline[section].get(key)
        if cur is None or base is None:
            continue
        rows.append((label, float(base), float(cur), float(cur) - float(base)))
    return rows


def recommendations(summary: Dict[str, Any], baseline: Optional[Dict[str, Any]]) -> List[str]:
    findings: List[str] = []
    battle = summary["battle"]
    interaction = summary["interaction"]
    tac = summary["tactical"]
    grenades = summary["grenades"]
    mobility = summary["strategic_mobility"]
    strat = summary["strategic"]
    integrity = summary.get("integrity", {})
    ingestion = integrity.get("ingestion", {})

    if ingestion.get("skipped_truncated_tail", 0):
        findings.append(
            "Black Box recovery: the final JSONL record was truncated, so the Companion "
            "ignored only that tail record and analyzed all earlier complete evidence."
        )
    if integrity.get("duplicate_sequence_ids", 0) or integrity.get("sequence_regressions", 0):
        findings.append(
            "Black Box integrity warning: duplicate or out-of-order sequence IDs were detected. "
            "Treat causal ordering around those records as uncertain until the source log is inspected."
        )
    if integrity.get("missing_sequence_ids", 0):
        findings.append(
            f"Black Box integrity warning: {integrity['missing_sequence_ids']} sequence ID(s) are missing "
            "from the loaded telemetry. This may indicate an interrupted or partially copied log."
        )

    if (
        interaction["reinforced_resolved"] >= 3
        and interaction["unreinforced_resolved"] >= 3
        and interaction["reinforced_player_success_rate"]
        > interaction["unreinforced_player_success_rate"] + 20.0
    ):
        findings.append(
            "Cross-layer anomaly: player success is materially higher in battles that followed recent enemy reinforcement arrivals. "
            "Inspect reinforcement composition, arrival timing, and whether fresh groups are entering disadvantageous tactical states."
        )
    if battle["unresolved"]:
        findings.append(
            f"Battle lifecycle telemetry has {battle['unresolved']} unresolved battle(s). "
            "Use the Black Box battle IDs to determine whether the session ended mid-battle or an end condition bypassed instrumentation."
        )
    if battle["resolved"] >= 5 and battle["player_success_rate"] > 90.0:
        findings.append(
            f"Across {battle['resolved']} resolved battles, player success is {battle['player_success_rate']:.1f}%. "
            "Do not tune difficulty from this alone, but inspect whether strategic pressure and tactical survival are both underperforming."
        )
    if grenades["actual_overrange"]:
        findings.append(
            f"AI grenade range anomaly: {grenades['actual_overrange']} resolved hand-grenade flight(s) exceeded calculated max range by more than the one-tile bookkeeping tolerance. "
            "Inspect the listed projectile IDs before making any balance change."
        )
    if grenades["targeted_beyond_range"]:
        findings.append(
            f"AI grenade targeting anomaly: {grenades['targeted_beyond_range']} committed hand-grenade throw(s) targeted a grid beyond the actor's calculated max range. "
            "This can indicate planner/execution disagreement even when physics clamps the actual flight."
        )

    if tac["rejected_rate"] > 5.0:
        findings.append(
            f"Tactical planning/execution mismatch: {tac['rejected_rate']:.1f}% of recorded outcomes were rejected. "
            "Inspect affordability/AP checks and any candidate scoring that ignores execution cost."
        )
    if tac["superseded_rate"] > 3.0:
        findings.append(
            f"Tactical action lifecycle instability: {tac['superseded_rate']:.1f}% of outcomes were superseded before closure. "
            "Use the Black Box decision IDs to locate the actions being replaced."
        )
    if tac["diagnostics"]:
        top = next(iter(tac["diagnostics"].items()))
        findings.append(
            f"Tactical troubleshooting signal: most common diagnostic is '{top[0]}' ({top[1]} occurrences)."
        )
    if tac["attack_cover_pairs"] >= 10:
        gap = tac["avg_cover_minus_attack_score"]
        if gap is not None and gap > 10:
            findings.append(
                f"Battle AI is strongly cover-weighted in measured attack-vs-cover decisions "
                f"(average adjusted cover advantage {gap:.1f}). Check whether this is producing excessive passivity."
            )
        elif gap is not None and gap < -10:
            findings.append(
                f"Battle AI is strongly attack-weighted in measured attack-vs-cover decisions "
                f"(average adjusted attack advantage {-gap:.1f}). Check exposure and casualty outcomes."
            )

    escape_eligible = tac.get("retreat_eligible", {}).get("sector_escape", 0)
    disengage_eligible = tac.get("retreat_eligible", {}).get("organized_disengagement", 0)
    hold_mean = tac.get("v2_state_means", {}).get("hold_confidence")
    if escape_eligible and hold_mean is not None and hold_mean >= 50:
        findings.append(
            f"Black Box v2 courage anomaly: sector escape was eligible {escape_eligible} time(s) while mean recorded hold confidence was {hold_mean:.1f}. "
            "Inspect the decision IDs for force-ratio, collapse-streak and rout-pressure disagreement."
        )
    if tac.get("formation_snapshots", 0) and tac.get("peak_escaping", 0) >= 2:
        findings.append(
            f"Formation-level rout signal: up to {tac['peak_escaping']} soldier(s) were simultaneously in escape state. "
            "Compare formation combat-ready strength with each actor's perceived force ratio before changing courage weights."
        )
    if disengage_eligible and not escape_eligible and tac.get("peak_disengaging", 0):
        findings.append(
            "Organized disengagement occurred without sector-escape eligibility. "
            "This is normally healthy behavior; inspect outcomes only if the formation became excessively passive."
        )

    if mobility["unmatched_arrivals"]:
        findings.append(
            f"Strategic execution trace has {mobility['unmatched_arrivals']} arrival(s) without a matching move order. "
            "Treat this as a Black Box coverage or group-redirection issue before drawing balance conclusions."
        )
    if mobility["outstanding_order_keys"] >= 3:
        findings.append(
            f"There are {mobility['outstanding_order_keys']} strategic move target(s) without a recorded arrival. "
            "Some may still be in transit; use world_minutes and group IDs to separate delay from pathing/reassignment failures."
        )
    if strat["no_action_rate"] > 50.0 and strat["commits"] >= 5:
        findings.append(
            f"Strategic AI selected no action in {strat['no_action_rate']:.1f}% of recorded decisions. "
            "Separate legitimate inactivity from reinforcement starvation using the recorded reasons and state values."
        )
    if strat["diagnostics"].get("reinforcement_selection_fallthrough", 0):
        findings.append(
            "Strategic reinforcement selection reached an impossible fallthrough at least once. "
            "Treat this as a Black Box troubleshooting issue before tuning weights."
        )

    if baseline:
        base_battle = baseline["battle"]
        base_tac = baseline["tactical"]
        base_strat = baseline["strategic"]
        if battle["resolved"] >= 3 and base_battle["resolved"] >= 3:
            if battle["player_success_rate"] > base_battle["player_success_rate"] + 15.0:
                findings.append(
                    "Change-impact signal: player battle success increased by more than 15 percentage points versus baseline. "
                    "Inspect tactical and strategic causal chains before attributing this to any single change."
                )
        if tac["rejected_rate"] > base_tac["rejected_rate"] + 3.0:
            findings.append(
                "Regression versus baseline: tactical rejected-action rate increased by more than 3 percentage points."
            )
        if strat["no_action_rate"] > base_strat["no_action_rate"] + 10.0:
            findings.append(
                "Regression versus baseline: strategic no-action rate increased by more than 10 percentage points."
            )

    if not findings:
        findings.append(
            "No high-confidence refinement warning crossed the current thresholds. "
            "Continue collecting sessions; the report is intentionally conservative rather than inventing causality from small samples."
        )
    return findings


def render_markdown(
    source: Path,
    summary: Dict[str, Any],
    baseline_source: Optional[Path],
    baseline: Optional[Dict[str, Any]],
) -> str:
    battle = summary["battle"]
    interaction = summary["interaction"]
    tac = summary["tactical"]
    grenades = summary["grenades"]
    mobility = summary["strategic_mobility"]
    strat = summary["strategic"]
    integrity = summary.get("integrity", {})
    ingestion = integrity.get("ingestion", {})

    lines: List[str] = [
        "# Vengeance Reloaded Campaign Companion",
        "",
        f"Source: `{source}`",
        "",
        "Experiments: " + ", ".join(
            sorted(
                {
                    str(item.get("experiment_tag", "unlabeled"))
                    for item in summary["session"]["builds"]
                }
            )
        ),
        "",
        "## Build provenance",
        "",
        "| Session | Branch | Commit | Dirty | Configuration | Target |",
        "|---|---|---|---:|---|---|",
    ]

    for build in summary["session"]["builds"]:
        dirty = build.get("build_dirty")
        dirty_text = "yes" if dirty else "no" if dirty is not None else "n/a"
        lines.append(
            f"| {build.get('session', 'n/a')} | "
            f"{build.get('build_branch') or 'legacy/unknown'} | "
            f"{build.get('build_commit_short') or build.get('build_commit') or 'legacy/unknown'} | "
            f"{dirty_text} | "
            f"{build.get('build_configuration') or 'n/a'} / {build.get('build_platform') or 'n/a'} | "
            f"{build.get('build_target') or 'n/a'} |"
        )
        recent = build.get("recent_changes") or []
        if recent:
            lines.append("")
            lines.append(
                f"Recent changes for session {build.get('session', 'n/a')}: "
                + "; ".join(str(item) for item in recent)
            )

    lines += [
        "",
        "## Black Box integrity",
        "",
        "| Check | Result |",
        "|---|---:|",
        f"| Loaded events | {summary['session']['events']} |",
        f"| Truncated tail records recovered | {ingestion.get('skipped_truncated_tail', 0)} |",
        f"| Foreign/unsupported records skipped | {ingestion.get('foreign_schema_lines', 0)} |",
        f"| Duplicate sequence IDs | {integrity.get('duplicate_sequence_ids', 0)} |",
        f"| Out-of-order sequence IDs | {integrity.get('sequence_regressions', 0)} |",
        f"| Missing sequence IDs | {integrity.get('missing_sequence_ids', 0)} |",
        "",
        "A single malformed final record is recoverable because a crash can interrupt the last write. "
        "Malformed records in the middle of the file remain fatal unless explicitly validated in strict mode.",
        "",
        "## Tactical refinement subsystem",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Decisions | {tac['decisions']} |",
        f"| Completed outcomes | {tac['completed']} ({tac['completed_rate']:.1f}%) |",
        f"| Rejected outcomes | {tac['rejected']} ({tac['rejected_rate']:.1f}%) |",
        f"| Superseded outcomes | {tac['superseded']} ({tac['superseded_rate']:.1f}%) |",
        f"| Average AP spent | {fmt(tac['avg_ap_spent'])} ({tac['valid_ap_samples']} valid samples) |",
        f"| Average absolute movement | {fmt(tac['avg_abs_grid_delta'])} grids |",
        f"| Direct combat hits logged | {tac['combat_hits']} |",
        f"| Applied-damage events | {tac['damage_events']} |",
        f"| Total life loss recorded | {fmt(tac['total_life_loss'])} |",
        f"| Total breath loss recorded | {fmt(tac['total_breath_loss'])} |",
        f"| Entered downed state | {tac['entered_downed']} |",
        f"| Lethal damage events | {tac['lethal_damage_events']} |",
        f"| Completed actions with last-attack-hit flag | {tac['last_attack_hit_flag_rate']:.1f}% ({tac['completed_action_samples']} completed-action samples) |",
        f"| Attack-vs-cover comparisons | {tac['attack_cover_pairs']} |",
        f"| Cover wins / attack wins / ties | {tac['defense_wins']} / {tac['offense_wins']} / {tac['ties']} |",
        f"| Mean cover-minus-attack adjusted score | {fmt(tac['avg_cover_minus_attack_score'])} |",
        "",
        "### AI grenade range fairness",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| AI hand-grenade launches | {grenades['launches']} |",
        f"| Resolved/paired flights | {grenades['paired_flights']} |",
        f"| Mean calculated max range | {fmt(grenades['avg_max_range'])} tiles |",
        f"| Mean committed target distance | {fmt(grenades['avg_target_distance'])} tiles |",
        f"| Mean actual flight distance | {fmt(grenades['avg_actual_distance'])} tiles |",
        f"| Mean effective strength | {fmt(grenades['avg_effective_strength'])} |",
        f"| Mean breath at release | {fmt(grenades['avg_breath_pct'])}% |",
        f"| Mean target offset from nearest player merc | {fmt(grenades['avg_target_offset_to_player'])} tiles |",
        f"| Mean landing offset from nearest player merc | {fmt(grenades['avg_landing_offset_to_player'])} tiles |",
        f"| Targets beyond calculated max | {grenades['targeted_beyond_range']} |",
        f"| Actual flights beyond max + 1 tile | {grenades['actual_overrange']} |",
        f"| Unmatched launch records | {grenades['unmatched_launches']} |",
        f"| Beyond-range target projectile IDs | {', '.join(str(r.get('projectile_id')) for r in grenades['targeted_beyond_records']) if grenades['targeted_beyond_records'] else 'none'} |",
        f"| Over-range projectile IDs | {', '.join(str(r.get('projectile_id')) for r in grenades['overrange_records']) if grenades['overrange_records'] else 'none'} |",
        "",
        "A one-tile tolerance is used only for final flight auditing because the physics object records its first moved grid rather than the exact sub-tile release point. "
        "Committed targets are checked against max range with no tolerance.",
        "",
        "### Black Box v2 decision forensics",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Formation snapshots | {tac['formation_snapshots']} |",
        f"| Mean formation combat-ready rate | {fmt(tac['avg_formation_ready_rate'])}% |",
        f"| Mean formation stress | {fmt(tac['avg_formation_stress'])} |",
        f"| Mean formation casualty rate | {fmt(tac['avg_formation_casualty_pct'])}% |",
        f"| Peak simultaneous disengaging | {tac['peak_disengaging']} |",
        f"| Peak simultaneous escaping | {tac['peak_escaping']} |",
        f"| Peak simultaneous cowering | {tac['peak_cowering']} |",
        f"| Mean perceived friendly/enemy strength ratio | {fmt(tac['avg_perceived_force_ratio'], 2)} |",
        f"| Mean hold confidence | {fmt(tac['v2_state_means'].get('hold_confidence'))} |",
        f"| Mean local stress | {fmt(tac['v2_state_means'].get('local_stress'))} |",
        f"| Mean personal risk | {fmt(tac['v2_state_means'].get('personal_risk'))} |",
        f"| Mean rout pressure | {fmt(tac['v2_state_means'].get('rout_pressure'))} |",
        f"| Eligible organized disengagements | {tac['retreat_eligible'].get('organized_disengagement', 0)} |",
        f"| Eligible sector escapes | {tac['retreat_eligible'].get('sector_escape', 0)} |",
        "",
        "Omniscient formation snapshots are reported separately from actor perception. "
        "Use decision IDs to inspect a soldier's perceived force strength, stress, risk, cover, leadership, weapon state and collapse streak before attributing a retreat to bad AI.",
        "",
        "### Battle outcomes",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Battles started | {battle['starts']} |",
        f"| Battles resolved | {battle['resolved']} |",
        f"| Unresolved battle IDs | {', '.join(map(str, battle['unresolved_ids'])) if battle['unresolved_ids'] else 'none'} |",
        f"| Player success rate | {battle['player_success_rate']:.1f}% |",
        f"| Mean player-count delta | {fmt(battle['avg_player_count_delta'])} |",
        f"| Mean enemy-count delta | {fmt(battle['avg_enemy_count_delta'])} |",
        f"| Mean militia-count delta | {fmt(battle['avg_militia_count_delta'])} |",
        f"| Mean battle duration | {fmt(battle['avg_duration_minutes'])} campaign minutes |",
        f"| Mean tactical decisions / battle | {fmt(battle['avg_tactical_decisions'])} |",
        "",
        "#### Results",
        "",
    ]

    if battle["results"]:
        lines += ["| Result | Count |", "|---|---:|"]
        for result, count in battle["results"].items():
            lines.append(f"| {result} | {count} |")
    else:
        lines.append("No completed battles recorded.")

    lines += [
        "",
        "### Tactical action mix",
        "",
    ]

    if tac["action_counts"]:
        lines += ["| Action ID | Count |", "|---:|---:|"]
        for action, count in list(tac["action_counts"].items())[:20]:
            lines.append(f"| {action} | {count} |")
    else:
        lines.append("No tactical action commits recorded.")

    lines += [
        "",
        "## Cross-layer interaction analysis",
        "",
        f"Window: {interaction['lookback_minutes']} campaign minutes before battle start.",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Battles after recent enemy reinforcement | {interaction['reinforced_battles']} |",
        f"| Battles without recent enemy reinforcement | {interaction['unreinforced_battles']} |",
        f"| Mean recently arrived enemy troops | {fmt(interaction['avg_recent_reinforcement_troops'])} |",
        f"| Mean tactical decisions after reinforcement | {fmt(interaction['avg_tactical_decisions_reinforced'])} |",
        f"| Mean tactical decisions without reinforcement | {fmt(interaction['avg_tactical_decisions_unreinforced'])} |",
        f"| Player success after recent reinforcement | {interaction['reinforced_player_success_rate']:.1f}% ({interaction['reinforced_resolved']} resolved) |",
        f"| Player success without recent reinforcement | {interaction['unreinforced_player_success_rate']:.1f}% ({interaction['unreinforced_resolved']} resolved) |",
        "",
        "This comparison is a causal lead, not proof: use the Black Box group IDs, timestamps and battle records to inspect the actual chain.",
        "",
        "## Strategic refinement subsystem",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Decisions | {strat['decisions']} |",
        f"| Commits | {strat['commits']} |",
        f"| No-action selections | {strat['no_action']} ({strat['no_action_rate']:.1f}%) |",
        f"| Candidates / decision | {strat['avg_candidates_per_decision']:.2f} |",
        f"| Eligible candidates | {strat['eligible_candidates']} |",
        f"| Rejected candidates | {strat['rejected_candidates']} |",
        f"| Mean eligible candidate score | {fmt(strat['avg_candidate_score'])} |",
        "",
        "### Strategic candidate rejection reasons",
        "",
    ]

    if strat["candidate_rejection_reasons"]:
        lines += ["| Reason | Count |", "|---|---:|"]
        for reason, count in strat["candidate_rejection_reasons"].items():
            lines.append(f"| {reason} | {count} |")
    else:
        lines.append("No rejected strategic candidates recorded.")

    lines += [
        "",
        "### Strategic movement execution",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Move orders | {mobility['orders']} |",
        f"| Arrivals | {mobility['arrivals']} |",
        f"| Matched arrivals | {mobility['matched_arrivals']} ({mobility['arrival_match_rate']:.1f}%) |",
        f"| Unmatched arrivals | {mobility['unmatched_arrivals']} |",
        f"| Outstanding move targets | {mobility['outstanding_order_keys']} |",
        f"| Mean matched travel time | {fmt(mobility['avg_travel_minutes'])} campaign minutes |",
        "",
        "### Strategic selections",
        "",
    ]

    if strat["selections"]:
        lines += ["| Selection | Count |", "|---|---:|"]
        for name, count in strat["selections"].items():
            lines.append(f"| {name} | {count} |")
    else:
        lines.append("No strategic commits recorded.")

    lines += ["", "## Black Box troubleshooting signals", ""]
    diagnostics = []
    for layer in ("tactical", "strategic"):
        for code, count in summary[layer]["diagnostics"].items():
            diagnostics.append((layer, code, count))
    if diagnostics:
        lines += ["| Layer | Diagnostic | Count |", "|---|---|---:|"]
        for layer, code, count in sorted(diagnostics, key=lambda x: (-x[2], x[0], x[1])):
            lines.append(f"| {layer} | {code} | {count} |")
    else:
        lines.append("No diagnostics recorded.")

    if baseline is not None and baseline_source is not None:
        lines += [
            "",
            "## Change-impact comparison",
            "",
            f"Baseline: `{baseline_source}`",
            "",
            "| Metric | Baseline | Current | Delta |",
            "|---|---:|---:|---:|",
        ]
        for label, base, cur, delta in comparison_rows(summary, baseline):
            lines.append(f"| {label} | {base:.2f} | {cur:.2f} | {delta:+.2f} |")

    lines += ["", "## Refinement findings", ""]
    for item in recommendations(summary, baseline):
        lines.append(f"- {item}")

    lines += [
        "",
        "## Interpretation rule",
        "",
        "The Companion treats correlations as hypotheses, not proof. Use the Black Box decision IDs to inspect the underlying chain before changing AI weights or mechanics.",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze Vengeance Reloaded Black Box telemetry.")
    parser.add_argument("log", type=Path, help="Current VR_BlackBox.jsonl")
    parser.add_argument("--baseline", type=Path, help="Earlier Black Box log for before/after comparison")
    parser.add_argument("-o", "--output", type=Path, default=Path("VR_Companion_Report.md"))
    parser.add_argument("--json-output", type=Path, default=Path("VR_Companion_Summary.json"))
    parser.add_argument(
        "--strict-jsonl",
        action="store_true",
        help="Fail on any malformed JSONL record, including a truncated final crash record.",
    )
    args = parser.parse_args()

    current_events, current_ingestion = load_events_with_diagnostics(
        args.log, strict=args.strict_jsonl
    )
    current = summarize(current_events, current_ingestion)

    baseline = None
    if args.baseline:
        baseline_events, baseline_ingestion = load_events_with_diagnostics(
            args.baseline, strict=args.strict_jsonl
        )
        baseline = summarize(baseline_events, baseline_ingestion)

    report = render_markdown(args.log, current, args.baseline, baseline)
    write_text_atomic(args.output, report)
    write_text_atomic(args.json_output, json.dumps(current, indent=2, sort_keys=True))

    print(f"Wrote {args.output}")
    print(f"Wrote {args.json_output}")


if __name__ == "__main__":
    main()
