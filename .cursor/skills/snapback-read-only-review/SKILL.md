---
name: snapback-read-only-review
description: >-
  Read-only Snapback audit/review mode: evidence against current code, no edits,
  no invented backlog. Use for reviews, audits, architecture analysis, or when
  the user asks for findings without implementation.
---

# Snapback read-only review

## Mode

**Read-only.** Do not edit docs, add roadmap items, create commits, or "helpfully" start implementing fixes unless the owner explicitly asks.

## Rules of evidence

- Recheck **current** source. Archived reviews (`docs/archive/*`) are historical; their line numbers and "confirmed" claims are not today's truth.
- Say what kind of evidence each finding is: source inspection, reproduction, or measurement.
- Prefer durable citations (paths, symbol names). Avoid fragile copied line numbers as the only anchor.
- When passes conflict, reconcile before recommending work.

## Backlog discipline

- `docs/ROADMAP.md` is the sole live backlog. Map findings to an existing owner item when one exists.
- Do not invent a second ticket list. Do not treat archive proposal numbers as tickets.
- Flag conflicts with ADRs and **Decided not to build**; recommendations do not override those.

## Output shape

1. Scope and method (what was inspected; what was not run).
2. Findings ordered by severity/impact, each with evidence and smallest coherent repair sketch.
3. Explicit deferrals and unknowns.
4. Optional: which live roadmap items already cover each finding.

End by asking whether the owner wants any finding turned into roadmap progress—not by editing the roadmap unprompted.
