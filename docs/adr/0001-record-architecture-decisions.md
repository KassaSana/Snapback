# ADR-0001 — Record architecture decisions in `docs/adr/`

- **Status:** Accepted
- **Date:** 2026-07-23
- **Roadmap item:** 12.3
- **Decided by:** Kassa

## Question

Where do the answers to this project's `decision`-tagged backlog items live, so that a
later session can find the reasoning instead of re-deriving the question?

## Context

`ROADMAP.md` carries fourteen items tagged `decision` (1.2, 4.11, 5.3, 5.4, 5.6, 7.7, 7.8,
7.16, 8.5, 9.1, 9.10, 10.2, 13.5, 13.6). Their entire output is *a decision and its
reasoning* — there is no artifact to commit otherwise.

Two forces made this urgent:

- **Reasoning already evaporated twice.** 5.4 and 5.6 were implemented, then reverted,
  because the rationale for the existing behavior lived only in a chat log. The revert cost
  more than the original decision would have.
- **This repo's docs assert false things in both directions.** `CLAUDE.md` alone carried six
  wrong claims as of 2026-07-20 — work described as missing that existed, work marked done
  whose code never ran. A prose status table is not a durable record of *why*; a dated,
  append-only file is.

The decision sessions are next in the roadmap sequence, so the home has to exist before
them, not after.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. `docs/adr/`, one file per decision | Append-only, greppable, diffable, dated; standard practice; survives audits | One more directory to keep in sync |
| B. A `DECISIONS.md` section in `ROADMAP.md` | Zero new structure | The roadmap is a *live backlog* — items get rewritten in place, which is exactly the mutation an ADR must not allow |
| C. A single `docs/DECISIONS.md` log | One file to read | Merge-conflict prone with a second agent working the repo; no per-decision status, so superseding is invisible |
| D. Keep deciding in chat | Nothing to maintain | This is the current state, and it is what produced the reverts |

## Decision

Decisions land in `docs/adr/NNNN-short-title.md`, one file per decision, from
`0000-template.md`. Files are append-only: a changed mind produces a new ADR and sets the
old one's status to `Superseded by ADR-NNNN`. `docs/adr/README.md` holds the index and the
list of items still awaiting an ADR.

## Why

Option A wins on the one property the others lack: **immutability**. The failure mode here
isn't "we couldn't find the decision," it's "the decision was quietly overwritten by someone
who thought they were fixing a bug." Numbered files with a `Superseded` status make a
reversal visible as an addition rather than a silent edit, and a `Revisit if` section makes
the trigger for reopening explicit.

Option B loses specifically because `ROADMAP.md` declares itself the live backlog and
rewrites items in place — the same file cannot be both mutable and the system of record.
Option C loses on concurrency: a second agent works this repo in parallel, and a single
append-target log conflicts on every write.

This would flip if the repo ever grew a real docs site or ticket tracker with immutable
history — then the ADR files would become a redundant mirror.

## Consequences

- `decision`-tagged roadmap items are **not implementable** until their ADR is `Accepted`.
  That rule is now enforceable by pointing at a missing file.
- When an item graduates, its `ROADMAP.md` body is replaced by a pointer to the ADR and
  moved to the Done archive — the roadmap stops carrying reasoning.
- Unblocks decision sessions A (5.3, 5.4, 1.2, 7.7) and B (4.11), and 9.1 before them.
- Costs a few minutes per decision session to write the file properly.

## Revisit if

Two consecutive decision sessions end without an ADR being written — that means the format
is too heavy, and the template should shrink rather than the practice being abandoned.
