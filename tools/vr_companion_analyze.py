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
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


def load_events(path: Path) -> List[Dict[str, Any]]:
    events: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, 1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                event = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{line_no}: invalid JSONL: {exc}")
            if event.get("schema") != "vr-blackbox-1":
                continue
            events.append(event)
    return events


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


def build_decisions(events: Iterable[Dict[str, Any]]) -> Dict[int, Dict[str, Any]]:
    decisions: Dict[int, Dict[str, Any]] = {}
    for event in events:
        decision_id = event.get("decision_id")
        if not isinstance(decision_id, int):
            continue

        record = decisions.setdefault(
            decision_id,
            {
                "id": decision_id,
                "layer": event.get("layer"),
                "begin": None,
                "states": {},
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


def tactical_summary(
    events: List[Dict[str, Any]], decisions: Dict[int, Dict[str, Any]]
) -> Dict[str, Any]:
    tactical = [d for d in decisions.values() if d.get("layer") == "tactical"]
    commits = [c for d in tactical for c in d["commits"] if "action" in c]
    outcomes = [o for d in tactical for o in d["outcomes"]]

    status = Counter(o.get("status", "unknown") for o in outcomes)
    actions = Counter(str(c.get("action")) for c in commits)

    ap_spent: List[float] = []
    grid_delta: List[float] = []
    attack_hits = 0
    attack_samples = 0

    for outcome in outcomes:
        if outcome.get("metric_a") == "grid_delta":
            grid_delta.append(abs(float(outcome.get("value_a", 0))))
        if outcome.get("metric_b") == "ap_spent":
            ap_spent.append(float(outcome.get("value_b", 0)))
        detail = parse_detail(outcome.get("detail"))
        if "last_attack_hit" in detail:
            attack_samples += 1
            attack_hits += 1 if detail["last_attack_hit"] == "1" else 0

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

    diagnostics = Counter(
        event.get("code", "unknown")
        for event in events
        if event.get("layer") == "tactical" and event.get("kind") == "diagnostic"
    )

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
        "avg_abs_grid_delta": safe_mean(grid_delta),
        "attack_hit_rate": pct(attack_hits, attack_samples),
        "attack_hit_samples": attack_samples,
        "action_counts": dict(actions.most_common()),
        "attack_cover_pairs": attack_cover_pairs,
        "defense_wins": defense_wins,
        "offense_wins": offense_wins,
        "ties": ties,
        "avg_cover_minus_attack_score": safe_mean(score_gaps),
        "diagnostics": dict(diagnostics.most_common()),
    }


def strategic_summary(
    events: List[Dict[str, Any]], decisions: Dict[int, Dict[str, Any]]
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
            }
            for e in starts
        ],
    }


def summarize(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    decisions = build_decisions(events)
    return {
        "session": session_summary(events),
        "tactical": tactical_summary(events, decisions),
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
        ("Tactical attack hit rate %", "tactical", "attack_hit_rate"),
        ("Cover - attack adjusted score", "tactical", "avg_cover_minus_attack_score"),
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
    tac = summary["tactical"]
    strat = summary["strategic"]

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
        base_tac = baseline["tactical"]
        base_strat = baseline["strategic"]
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
    tac = summary["tactical"]
    strat = summary["strategic"]

    lines: List[str] = [
        "# Vengeance Reloaded Campaign Companion",
        "",
        f"Source: `{source}`",
        "",
        "## Tactical refinement subsystem",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Decisions | {tac['decisions']} |",
        f"| Completed outcomes | {tac['completed']} ({tac['completed_rate']:.1f}%) |",
        f"| Rejected outcomes | {tac['rejected']} ({tac['rejected_rate']:.1f}%) |",
        f"| Superseded outcomes | {tac['superseded']} ({tac['superseded_rate']:.1f}%) |",
        f"| Average AP spent | {fmt(tac['avg_ap_spent'])} |",
        f"| Average absolute movement | {fmt(tac['avg_abs_grid_delta'])} grids |",
        f"| Recorded last-attack hit rate | {tac['attack_hit_rate']:.1f}% ({tac['attack_hit_samples']} samples) |",
        f"| Attack-vs-cover comparisons | {tac['attack_cover_pairs']} |",
        f"| Cover wins / attack wins / ties | {tac['defense_wins']} / {tac['offense_wins']} / {tac['ties']} |",
        f"| Mean cover-minus-attack adjusted score | {fmt(tac['avg_cover_minus_attack_score'])} |",
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
        "## Strategic refinement subsystem",
        "",
        "| Metric | Result |",
        "|---|---:|",
        f"| Decisions | {strat['decisions']} |",
        f"| Commits | {strat['commits']} |",
        f"| No-action selections | {strat['no_action']} ({strat['no_action_rate']:.1f}%) |",
        f"| Candidates / decision | {strat['avg_candidates_per_decision']:.2f} |",
        f"| Mean eligible candidate score | {fmt(strat['avg_candidate_score'])} |",
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
    args = parser.parse_args()

    current_events = load_events(args.log)
    current = summarize(current_events)

    baseline = None
    if args.baseline:
        baseline = summarize(load_events(args.baseline))

    report = render_markdown(args.log, current, args.baseline, baseline)
    args.output.write_text(report, encoding="utf-8")
    args.json_output.write_text(
        json.dumps(current, indent=2, sort_keys=True), encoding="utf-8"
    )

    print(f"Wrote {args.output}")
    print(f"Wrote {args.json_output}")


if __name__ == "__main__":
    main()
