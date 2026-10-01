---
name: snapback-roadmap-item
description: >-
  Picks and updates Snapback work from docs/ROADMAP.md only—status tags, ADR
  gates, no invented backlogs. Use when starting a feature, choosing what to
  build next, updating progress, or when tempted to add a TODO list or scratch doc.
---

# Snapback roadmap item

## Source of truth

- `docs/ROADMAP.md` is the **only** live backlog.
- Completed work lives in `docs/roadmap_archive.md`.
- Binding refusals: roadmap **Decided not to build**. Do not revive without owner agreement (often a new ADR).

## Status tags

| Tag | Meaning |
| --- | --- |
| `proposed` | Idea/finding; not agreed to build |
| `accepted` | Agreed to build; agreement must be citable |
| `in progress` | Part landed; item says what remains |
| `decision` | **No code** until an ADR answers the question |

Promoting status is a deliberate edit, never a side effect of a persuasive write-up.

## Before coding

1. Confirm the item still matches today's code (roadmap claims about the world vs the tree).
2. If tagged `decision`, stop and write/land an ADR first.
3. Prefer the roadmap's "Start here" sequence when choosing among open items.
4. Make the smallest coherent change; one focused concern per commit.
5. Update the roadmap item **in the same commit** as the work (progress note + status if needed).

## Do not

- Invent parallel TODO lists, session notes, or `docs/scratch/`.
- Implement "Decided not to build" items.
- Push unless the owner asks.
- Treat archived audits as an accepted queue; recheck code and map to a live roadmap owner.
