# Professional CQB research -> Vengeance competence model

> STATUS: RESEARCH / DORMANT IMPLEMENTATION CONTRACT
>
> This document does not activate CQB behaviour. It defines how training quality
> should change building-clearing and building-holding behaviour once the dormant
> CQB layer is implemented.

## 1. Important modelling note

Vengeance's SOLDIER_CLASS_ADMINISTRATOR / SECURITY doctrine is a fictional game
category. It is **not** being claimed to correspond directly to a real-world military
administrative clerk, police officer, or security guard.

The useful real-world distinction is instead:

- troops with only basic/familiarization-level urban skills;
- ordinary infantry trained in core room/building drills;
- ordinary troops operating under a capable local leader;
- experienced/rehearsed assault troops;
- specialized/highly practiced CQB teams.

That training continuum maps cleanly onto Vengeance's existing doctrine ladder.

## 2. Professional-source findings

### USMC Basic School - Urban Operations II

Source:
https://www.trngcmd.marines.mil/Portals/207/Docs/TBS/B4R5379%20Urban%20Ops%20II%20Offense%20and%20Defense%20Operations.pdf

Relevant principles:

- Urban attacks use Recon, Isolation, Secure a Foothold, Seize the Objective.
- Assault, Support and Security are distinct tactical functions.
- A proficient unit suppresses/obscures/secures a breach before flowing forces through
  a foothold.
- Clearing uses rehearsed movement, sectors and common language.
- After an action, the force consolidates rapidly and assigns security against
  counterattack/infiltration.
- Defensive positions should have primary, alternate and supplementary positions.
- Alternate firing positions may be in an adjacent room.
- Defensive positions should be mutually supporting with overlapping sectors.
- A building defense should retain a covered route for resupply, reinforcement,
  casualty evacuation or withdrawal.

Game implication:
professional CQB is not "run through door faster". It is the ability to divide tactical
functions, maintain sector responsibility, create/hold a foothold, consolidate, and
change positions without the whole group losing coherence.

### I MEF Raid Leaders Course

Source:
https://www.imef.marines.mil/Media-Room/Stories/Article/Article/643708/more-tools-for-the-toolbox-raid-leaders-course-teaches-marines-urban-combat-ski/

The Marine Corps article explicitly distinguishes basic exposure from advanced skill:
all Marines receive some CQB training, while advanced MOUT/CQB techniques require
additional instruction and repetition. The course uses a crawl-walk-run progression,
starts with two-man clearing, progresses to four-man clearing, rotates Marines through
roles, and emphasizes repeated practice until the team operates smoothly.

Game implication:
low-quality troops should retain a few recognizable fundamentals. The difference
between basic and expert troops should be consistency, coordination, role discipline,
adaptation and the complexity of problems they can solve.

### U.S. Army / NYPD ESU training

Source:
https://www.army.mil/article-amp/258917/nypd_instructs_soldiers_on_close_quarters_battle

The Army article records experienced infantry acknowledging that full-time CQB
specialists catch small errors they miss. It emphasizes danger areas, fast second-man
support, individual roles and the effect of small coordination mistakes.

Game implication:
even experienced line infantry should not be perfect. Specialist/expert quality should
mainly reduce small errors: doorway pauses, uncovered sectors, delayed follow-up,
poor role handoff and stale plans.

### U.S. Army infantry CQB training

Source:
https://innovation.army.mil/News/Article-View/Article/4397605/1st-infantry-division-conducts-close-quarters-combat-training-at-novo-selo-trai/

Current Army training describes room-to-room movement by team elements, target
identification, rapid decisions and each soldier understanding a specific role.

Game implication:
ordinary LINE troops can plausibly perform basic buddy/team clearing, but the planner
should not assume every soldier can independently solve complicated multi-room
geometry.

### MARSOC / Marine CQB training

Source:
https://www.marsoc.marines.mil/COMMSTRAT/News/Article/Article/1371185/marsoc-trains-hodge-podge-of-marines/

The training account emphasizes knowing the fireteam's habits, cohesion and efficiency,
especially around the point man.

Game implication:
expert behaviour should emerge from stable local fireteam cooperation and role handoff,
not individual superhuman stats.

## 3. Design rule: fundamentals are broad; mastery is not

Every armed enemy should retain ordinary self-preservation and generic cover logic.
CQB competence changes *how reliably and how deeply* a unit applies building-specific
principles.

Do not implement a binary:
- ADMIN = stupid
- ELITE = knows CQB

Use a capability ladder.

## 4. Proposed Vengeance training ladder

| Vengeance profile | CQB abstraction | Assault | Hold | Intended feel |
| --- | --- | ---: | ---: | --- |
| SECURITY / administrator | familiarized guard / poor assault troop | 28 | 52 | Can guard obvious approach; unreliable clearing |
| LINE uncommanded | basic infantry | 52 | 60 | Knows core buddy drill but makes coordination errors |
| LINE commanded | basic infantry under competent local direction | 66 | 70 | Can execute simple assault-support-security pattern |
| VETERAN | experienced/rehearsed infantry | 82 | 82 | Good sectors, replanning, alternate approaches |
| ELITE_MOBILE | specialized assault-quality troops | 94 | 84 | Best at entry, flow and local counterattack |
| ELITE_GUARD | specialized defensive/strongpoint troops | 78 | 96 | Best at sectors, depth, alternates and holding |

These are planner bands, not direct probabilities and never CTH/AP bonuses.

## 5. What SECURITY / administrators should actually do

They are **not mindless**.

Expected strengths:
- understand obvious cover;
- recognize an exposed doorway as dangerous some of the time;
- cover the most obvious entrance;
- remain near their assigned post;
- react to visible/heard contact;
- fall back using the existing self-preservation system.

Expected weaknesses:
- weak sector distribution;
- may bunch near the same attractive firing point;
- slower handoff between cover and mover;
- poorer threshold discipline;
- little/no independent alternate-entry reasoning;
- no sophisticated room-to-room flow;
- no independent local counterattack;
- no proactive suppression/smoke plan merely to enable an entry;
- may hesitate before following the point man;
- may lose coordination when the first plan fails.

Important:
a SECURITY guard can be more competent at **HOLD** than at **ASSAULT**. Guarding a
door/window from a known post is much simpler than dynamically clearing an unknown
multi-room structure.

## 6. LINE infantry

### Uncommanded LINE

Should know a recognizable basic drill:
- avoid obvious threshold loitering more often than SECURITY;
- pair a mover with a nearby covering buddy;
- two-man simple entry when geometry is straightforward;
- basic sector separation;
- avoid immediately stacking the exact same interior tile.

But:
- only one coordinated mover at a time;
- limited alternate-route reasoning;
- poor multi-room memory/flow;
- little proactive utility use;
- more likely to stop after the first foothold and revert to generic AI;
- more likely to choose an adequate rather than optimal position.

### Commanded LINE

A nearby capable leader should unlock a *simple plan*, not veteran ability:
- assault / support / security role split;
- rear security on a second entrance;
- limited defense-in-depth;
- proactive suppression/smoke if equipment permits;
- two coordinated movers;
- cleaner transition from entry to secure.

The command effect should disappear when the leader is incapacitated, cowering,
disengaging or too far away, matching existing doctrine rules.

## 7. VETERAN

Veterans should show the largest qualitative jump.

Capabilities:
- reliable threshold discipline;
- better danger-sector assignment;
- alternate-entry comparison;
- local dynamic replanning if the first entry becomes untenable;
- proactive support when justified;
- rear/side security;
- defense in depth;
- limited local counterattack;
- better transition from ASSAULT -> SECURE -> next action.

Imperfection remains:
- stress/suppression should still degrade coordination;
- stale knowledge can still produce a wrong plan;
- veterans can still choose the wrong entry;
- planners should retain competence noise and reliability checks.

## 8. ELITE_MOBILE

This is the closest Deidranna gets to a specialized assault team.

Capabilities:
- all veteran abilities;
- better multi-room flow;
- strongest alternate-entry selection;
- fastest replanning after unexpected contact;
- can coordinate more than one mover where AP/geometry permits;
- strongest local counterattack / retake behaviour.

Constraints:
- no hidden information;
- no reaction/accuracy bonus from CQB status;
- resource limits still apply;
- cannot execute complex plans while isolated, broken, heavily suppressed or exhausted;
- should not be pulled away from mission context by a distant contact.

## 9. ELITE_GUARD

Elite guard doctrine is deliberately asymmetric.

Assault:
- veteran-like local clearing if their strongpoint is penetrated;
- lower willingness to roam or conduct deep pursuit;
- no ELITE_MOBILE-style aggressive room-to-room chase.

Defense:
- strongest sector allocation;
- strongest anti-crowding;
- primary + alternate positions;
- deeper fallback routes;
- reserve/counterattack element when manpower permits;
- better persistence of rear security;
- better understanding that losing one room does not mean abandoning the building.

This should make high-value facilities difficult because of **structure and coordination**,
not because defenders receive artificial combat bonuses.

## 10. Explicit capability matrix

| Capability | SECURITY | LINE | LINE+CMD | VETERAN | ELITE MOBILE | ELITE GUARD |
| --- | --- | --- | --- | --- | --- | --- |
| Fatal-funnel awareness | unreliable | yes | yes | strong | very strong | very strong |
| Two-man entry | no/reaction only | basic | yes | yes | yes | yes locally |
| Sector deconfliction | weak | basic | good | strong | expert | expert |
| Rear security | weak | situational | yes | yes | yes | strong |
| Alternate entry analysis | no | no | limited/manual leader effect | yes | strong | usually no deep assault |
| Dynamic replan | weak | weak | moderate | strong | strongest | strong |
| Proactive support | reactive | reactive | yes | yes | yes | yes |
| Defense in depth | crude | limited | yes | yes | yes | strongest |
| Local counterattack | no | no | limited only through later policy | yes | strongest | strong/anchored |
| Complex room flow | no | no | no | limited via repeated plans | yes | no deep roaming |

## 11. Failure modes must be visible

Do not make competence merely a hidden score.

SECURITY should visibly:
- pause at bad moments;
- sometimes follow late;
- over-focus on the obvious doorway;
- fail to cover an alternate approach;
- choose a merely acceptable defensive tile;
- fail to reorganize quickly after a casualty.

LINE should visibly:
- perform the basic drill but sometimes lose flow after contact;
- have one soldier move while others support;
- require leadership for sophisticated coordination.

VETERAN/ELITE should visibly:
- recover faster from disrupted plans;
- occupy complementary sectors;
- shift to alternate positions;
- preserve rear security;
- avoid repeated use of a compromised entry.

## 12. Stress and casualties

Training quality is not immunity to stress.

Future CQB decision quality should combine:
- VRCQB training profile;
- existing AIPlannerReliability();
- AILocalStress();
- morale;
- suppression/shock;
- injury/fatigue;
- loss of local leader;
- local casualty state.

A heavily suppressed elite element can therefore temporarily behave worse than a calm
LINE element. The difference is that elite troops should recover/replan more reliably.

## 13. Defensive doctrine from professional sources

The professional material strongly supports the existing planned HOLD system:

- primary positions;
- alternate positions;
- supplementary sectors;
- mutually supporting positions;
- overlapping fields of fire;
- a covered withdrawal/reinforcement route;
- consolidation after contact;
- security against counterattack/infiltration.

For Vengeance, this should be implemented primarily as **position scoring and role
allocation**, not as scripted map coordinates.

## 14. Implementation contract

When activation work begins:

1. Use existing AIGetDoctrineProfile(), AICompetenceTier(), AIPlannerReliability() and
   AIHasLocalCommandSupport().
2. Do not add a second rank/experience system.
3. Do not add CTH/AP/perception/reaction bonuses.
4. Keep CQB plans fireteam-local.
5. Keep plans knowledge-fair.
6. Use short-lived plans that invalidate on new contact, leader loss, casualty,
   suppression spike or tactical turn rollback.
7. Apply building-specific behaviour only when geometry/context justifies it.
8. Preserve mapper orders and strongpoint anchoring.
9. Record training profile and failure/rejection reason in Black Box telemetry.
10. Keep the entire module hard-gated until explicit activation approval.

## 15. Current inactive code preparation

CQBBuildingDoctrine.h / .cpp now define:

- a six-level CQB training profile;
- explicit CQB capability flags;
- separate assault vs hold skill;
- threshold/hesitation/coordination error bands;
- preferred clearing-team size;
- coordinated-mover limits;
- mapping from existing Vengeance doctrine into the CQB profile.

The code is still not referenced by DecideAction, not in the VS project and
VRCQB_IsRuntimeEnabled() remains FALSE.
