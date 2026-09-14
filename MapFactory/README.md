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

## Methodology gate

After five representative pilot sectors, the controller evaluates the method itself. If fewer than 3 of 5 sectors pass bake persistence plus visible tactical QA, the generic recipe approach is abandoned for production. The next method is archetype-specific composition kits: complete isometric farms, checkpoints, wilderness clusters and industrial compounds designed as coherent blocks, sliced into reusable JA2 tile frames, then placed by the same engine compiler.

After the pilot passes, production proceeds in waves. A wave that repeatedly falls below 50% acceptance is a methodology failure, not a reason to keep retrying individual maps.

## Optimization discipline

The methodology is not optimized indefinitely.

Pilot rule:
- Below 60% acceptance after five representative sectors: switch method.
- 60% to 79%: allow one targeted optimization cycle only.
- 80% or better: freeze the method and start production.
- After that one optimization cycle, 70% or better is considered good enough and the method is frozen; below that, switch method.

Production-wave rule:
- 75% or better acceptance: keep the frozen method.
- 50% to 74%: allow one targeted optimization cycle.
- Below 50%, or still below the acceptable threshold after that optimization: switch method.

Once a method is frozen, individual difficult sectors are handled as outliers. They do not reopen the whole methodology unless an entire production wave degrades.

The objective is acceptable visual quality at useful throughput, not theoretical perfection.
