# ADR-0001: Record architecture decisions in `docs/adr/`

- **Status:** Accepted
- **Date:** 2026-07-23

## Question

Where should the reasoning behind design decisions live, so it can be found later instead of
re-derived?

## Context

Several backlog items were really decisions, not tasks. Twice, behaviour that looked like a
bug was "fixed" and then reverted, because the reason for the original behaviour wasn't
written down anywhere.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. `docs/adr/`, one file per decision | Append-only, dated, easy to search and diff | One more directory |
| B. A section in the roadmap | No new structure | The roadmap gets rewritten in place, which is exactly what a decision record shouldn't allow |
| C. One `DECISIONS.md` log | One file to read | No per-decision status, so superseding is invisible |
| D. Don't write them down | Nothing to maintain | That's what caused the reverts |

## Decision

Decisions go in `docs/adr/NNNN-short-title.md`, one per file, based on `0000-template.md`.
They're append-only: changing your mind means a new ADR, and the old one is marked
`Superseded by ADR-NNNN`. `docs/adr/README.md` is the index.

## Why

The failure being prevented isn't "couldn't find the decision", it's "the decision got
quietly overwritten by someone fixing a bug". Numbered, append-only files make a reversal an
addition rather than a silent edit.

## Consequences

- Work that depends on an open decision waits for its ADR.
- Writing one takes a few minutes per decision.

## Revisit if

ADRs stop getting written because the format is too heavy. Then shrink the template.
