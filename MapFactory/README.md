# Vengeance Map Factory v1

## Goal

Produce the 88 surface maps in rows A-F as a throughput problem, not an A3 debugging project.

The engine remains the only writer of JA2 map files. The factory adds a layer above it:

1. Profile the real sector through Vengeance MapEditor.
2. Classify the sector into a visual archetype.
3. Generate a deterministic recipe of coherent modules.
4. Compile the recipe in-memory through Vengeance and SaveWorld.
5. Render overview and tactical QA captures.
6. Measure visual delta and reload persistence.
7. Escalate strategy when a method fails instead of repeating it.

## Failure policy

A sector gets at most two automated design strategies: authored_modules, then strong_modules. If both fail, it moves to manual_required with its diagnostics preserved.

No sector may loop indefinitely. A failed attempt must either change strategy or stop.

## Pilot

The first cross-archetype pilot is A3, A8, A12, B13 and F15. It exists to prove that the pipeline generalizes before batch production expands.

## Primary metric

Progress is accepted maps / 88 surface maps. Workflow runs and experiments are diagnostics, not progress by themselves.
