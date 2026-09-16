#!/usr/bin/env python3
"""
Analyze Vengeance Reloaded AI self-play black-box output.

Stdlib-only so it can run on a stock Python 3 installation.
"""

import argparse
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

ACTION_NAMES = {
    0:"NONE",1:"RANDOM_PATROL",2:"SEEK_FRIEND",3:"SEEK_OPPONENT",4:"TAKE_COVER",
    5:"GET_CLOSER",6:"POINT_PATROL",7:"LEAVE_WATER_GAS",8:"SEEK_NOISE",
    9:"ESCORTED_MOVE",10:"RUN_AWAY",11:"KNIFE_MOVE",12:"APPROACH_MERC",
    13:"TRACK",14:"EAT",15:"PICKUP_ITEM",16:"SCHEDULE_MOVE",17:"WALK",18:"RUN",
    19:"WITHDRAW",20:"FLANK_LEFT",21:"FLANK_RIGHT",22:"MOVE_TO_CLIMB",
    23:"CHANGE_FACING",24:"CHANGE_STANCE",25:"YELLOW_ALERT",26:"RED_ALERT",
    27:"CREATURE_CALL",28:"PULL_TRIGGER",29:"USE_DETONATOR",30:"FIRE_GUN",
    31:"TOSS_PROJECTILE",32:"KNIFE_STAB",33:"THROW_KNIFE",34:"GIVE_AID",
    35:"WAIT",36:"PENDING_ACTION",37:"DROP_ITEM",38:"COWER",39:"STOP_COWERING",
    40:"OPEN_OR_CLOSE_DOOR",41:"UNLOCK_DOOR",42:"LOCK_DOOR",43:"LOWER_GUN",
    44:"ABSOLUTELY_NONE",45:"CLIMB_ROOF",46:"END_TURN",47:"END_COWER_AND_MOVE",
    48:"TRAVERSE_DOWN",49:"OFFER_SURRENDER",50:"RAISE_GUN",51:"STEAL_MOVE",
    52:"RELOAD_GUN",53:"JUMP_WINDOW",54:"FREE_PRISONER",55:"USE_SKILL",
    56:"HANDLE_ITEM",
}

def read_tsv(path):
    p=Path(path)
    if not p.exists():
        return []
    with p.open("r", encoding="utf-8", errors="replace", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))

def num(row, key, default=0.0):
    try:
        return float(row.get(key, default) or default)
    except (TypeError, ValueError):
        return float(default)

def mean(vals):
    return statistics.fmean(vals) if vals else 0.0

def median(vals):
    return statistics.median(vals) if vals else 0.0

def pct(a,b):
    return 100.0*a/b if b else 0.0

def group_runs(rows):
    out=defaultdict(list)
    for r in rows:
        out[(r.get("build_label","current"), r.get("selected_map","slot"), r.get("fixture_slot","?"))].append(r)
    return out

def action_stats(decisions):
    out=defaultdict(Counter)
    repeats=defaultdict(lambda: [None,0,0])
    for r in decisions:
        key=(r.get("build_label","current"), r.get("selected_map","slot"), r.get("fixture_slot","?"), r.get("team","?"))
        try:
            action=int(r.get("action","-1"))
        except ValueError:
            action=-1
        out[key][action]+=1

        skey=(r.get("build_label","current"), r.get("fixture_slot","?"),
              r.get("run","?"), r.get("team","?"), r.get("soldier_id","?"))
        sig=(r.get("action"), r.get("action_data"))
        prev,count,total=repeats[skey]
        if sig==prev:
            count += 1
            if count >= 2:
                total += 1
        else:
            count=0
        repeats[skey]=[sig,count,total]

    repeat_total=sum(v[2] for v in repeats.values())
    return out, repeat_total

def deterministic_warnings(rows):
    grouped=defaultdict(set)
    counts=Counter()
    for r in rows:
        key=(r.get("build_label","current"),r.get("selected_map","slot"),r.get("fixture_slot","?"),r.get("seed","?"))
        grouped[key].add(r.get("state_hash",""))
        counts[key]+=1
    return [(k,counts[k],hashes) for k,hashes in grouped.items()
            if counts[k] > 1 and len(hashes) > 1]

def paired_delta(rows, baseline, candidate):
    by=defaultdict(dict)
    for r in rows:
        label=r.get("build_label","current")
        if label not in (baseline,candidate):
            continue
        key=(r.get("selected_map","slot"),r.get("fixture_slot","?"),r.get("seed","?"))
        by[key][label]=r

    pairs=[(k,v[baseline],v[candidate]) for k,v in by.items()
           if baseline in v and candidate in v]
    if not pairs:
        return None

    metrics=["team_turns","wall_ms","side_a_alive","side_b_alive",
             "a_damage","b_damage","a_moves","b_moves","a_suppression_ap","b_suppression_ap"]
    deltas={}
    for m in metrics:
        vals=[num(c,m)-num(b,m) for _,b,c in pairs]
        deltas[m]=(mean(vals),median(vals))

    outcome_changes=sum(1 for _,b,c in pairs if b.get("result") != c.get("result"))
    return pairs,deltas,outcome_changes

def build_report(runs, decisions, baseline=None, candidate=None):
    lines=[]
    lines.append("VENGEANCE AI SELF-PLAY ANALYSIS")
    lines.append("="*34)
    lines.append(f"Runs: {len(runs)} | Decisions: {len(decisions)}")
    lines.append("")

    groups=group_runs(runs)
    for (label,map_name,fixture), rs in sorted(groups.items()):
        outcomes=Counter(r.get("result","?") for r in rs)
        turns=[num(r,"team_turns") for r in rs]
        wall=[num(r,"wall_ms") for r in rs]
        a_shots=sum(num(r,"a_shots") for r in rs)
        a_hits=sum(num(r,"a_hits") for r in rs)
        b_shots=sum(num(r,"b_shots") for r in rs)
        b_hits=sum(num(r,"b_hits") for r in rs)
        stalls=outcomes.get("max_team_turns",0)
        lines.append(f"[{label}] map {map_name} fixture {fixture}")
        lines.append(f"  runs={len(rs)} outcomes={dict(outcomes)}")
        lines.append(f"  team_turns mean={mean(turns):.1f} median={median(turns):.1f} "
                     f"stall_rate={pct(stalls,len(rs)):.1f}%")
        lines.append(f"  wall_ms mean={mean(wall):.0f} median={median(wall):.0f}")
        lines.append(f"  side_A hit_rate={pct(a_hits,a_shots):.1f}% "
                     f"damage/run={mean([num(r,'a_damage') for r in rs]):.1f} "
                     f"moves/run={mean([num(r,'a_moves') for r in rs]):.1f} "
                     f"suppAP/run={mean([num(r,'a_suppression_ap') for r in rs]):.1f} "
                     f"smoke/run={mean([num(r,'a_smoke') for r in rs]):.2f}")
        lines.append(f"  side_B hit_rate={pct(b_hits,b_shots):.1f}% "
                     f"damage/run={mean([num(r,'b_damage') for r in rs]):.1f} "
                     f"moves/run={mean([num(r,'b_moves') for r in rs]):.1f} "
                     f"suppAP/run={mean([num(r,'b_suppression_ap') for r in rs]):.1f} "
                     f"smoke/run={mean([num(r,'b_smoke') for r in rs]):.2f}")
        lines.append("")

    acts, repeat_total=action_stats(decisions)
    if acts:
        lines.append("ACTION MIX")
        lines.append("-"*10)
        for key,counter in sorted(acts.items()):
            label,map_name,fixture,team=key
            total=sum(counter.values())
            top=counter.most_common(12)
            pretty=", ".join(f"{ACTION_NAMES.get(a,str(a))}={n} ({pct(n,total):.1f}%)" for a,n in top)
            lines.append(f"[{label}] map {map_name} fixture {fixture} team {team}: {pretty}")
        lines.append(f"Repeated identical action+destination loop signals: {repeat_total}")
        lines.append("")

    nondet=deterministic_warnings(runs)
    lines.append("REPRODUCIBILITY")
    lines.append("-"*15)
    if nondet:
        lines.append("WARNING: identical build/fixture/seed produced different final hashes:")
        for (label,map_name,fixture,seed),count,hashes in nondet:
            lines.append(f"  {label} map={map_name} fixture={fixture} seed={seed} n={count} hashes={sorted(hashes)}")
    else:
        lines.append("No conflicting duplicate-seed final hashes found in this corpus.")
    lines.append("")

    # Diagnostic signals: evidence to inspect, not automatic AI verdicts.
    lines.append("DIAGNOSTIC SIGNALS")
    lines.append("-"*18)
    any_signal=False
    for (label,map_name,fixture),rs in sorted(groups.items()):
        outcomes=Counter(r.get("result","?") for r in rs)
        stalls=outcomes.get("max_team_turns",0)
        if len(rs) >= 5 and pct(stalls,len(rs)) >= 5:
            lines.append(f"  {label}/map {map_name}/fixture {fixture}: max-turn stalls {pct(stalls,len(rs)):.1f}%")
            any_signal=True
    for (label,map_name,fixture,team),counter in sorted(acts.items()):
        total=sum(counter.values())
        if total < 30: continue
        wait=counter.get(35,0)
        setup=counter.get(23,0)+counter.get(24,0)+counter.get(50,0)
        flank=counter.get(20,0)+counter.get(21,0)
        if pct(wait,total) > 20:
            lines.append(f"  {label}/map {map_name}/fixture {fixture}/team {team}: WAIT share {pct(wait,total):.1f}%")
            any_signal=True
        if pct(setup,total) > 35:
            lines.append(f"  {label}/map {map_name}/fixture {fixture}/team {team}: facing/stance/raise setup share {pct(setup,total):.1f}%")
            any_signal=True
        if total >= 100 and flank == 0:
            lines.append(f"  {label}/map {map_name}/fixture {fixture}/team {team}: no flank decisions across {total} actions")
            any_signal=True
    if repeat_total:
        lines.append(f"  repeated identical action+destination loop signals={repeat_total}")
        any_signal=True
    if not any_signal:
        lines.append("  No threshold diagnostics triggered.")
    lines.append("")

    if baseline and candidate:
        pair=paired_delta(runs,baseline,candidate)
        lines.append(f"PAIRED A/B: {baseline} -> {candidate}")
        lines.append("-"*40)
        if not pair:
            lines.append("No matching fixture+seed pairs found.")
        else:
            pairs,deltas,outcome_changes=pair
            lines.append(f"Matched pairs: {len(pairs)} | outcome changes: {outcome_changes}")
            for metric,(m,med) in deltas.items():
                lines.append(f"  {metric}: mean delta={m:+.2f}, median delta={med:+.2f}")
        lines.append("")

    return "\n".join(lines)+"\n"

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--runs",default="AI SelfPlay Runs.tsv")
    ap.add_argument("--decisions",default="AI SelfPlay Decisions.tsv")
    ap.add_argument("--baseline")
    ap.add_argument("--candidate")
    ap.add_argument("--output",default="AI SelfPlay Analysis.txt")
    args=ap.parse_args()

    runs=read_tsv(args.runs)
    decisions=read_tsv(args.decisions)
    report=build_report(runs,decisions,args.baseline,args.candidate)
    Path(args.output).write_text(report,encoding="utf-8")
    print(report,end="")

if __name__=="__main__":
    main()
