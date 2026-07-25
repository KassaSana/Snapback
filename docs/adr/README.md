# Architecture Decision Records

**A decision that only exists in a chat log has not been made.** This directory is where
answers to the `decision`-tagged items in [ROADMAP.md](../ROADMAP.md) land, so the next
audit reads the reasoning instead of re-deriving the question.

That failure has already happened here twice: 5.4 and 5.6 were both "fixed" and then
reverted, because the reasoning behind the original shape was nowhere on disk.

## How to use this

1. Copy [`0000-template.md`](0000-template.md) to `NNNN-short-title.md`, next number, no gaps.
2. Fill it in **during** the decision session, not after. If you cannot write the *Why*
   section, the decision is not actually made yet.
3. Add a row to the index below.
4. In `ROADMAP.md`, replace the item body with a pointer to the ADR and move it to the
   Done archive.

## Rules

- **One decision per file.** Decision session A covers four roadmap items (5.3, 5.4, 1.2,
  7.7) because they share one *question* — that is still one ADR if one answer settles all
  four, and four ADRs if it doesn't.
- **ADRs are append-only.** Never edit a decision to say something new. Write a new ADR and
  set the old one's status to `Superseded by ADR-NNNN`. The wrong turn is the useful part.
- **Status vocabulary:** `Proposed` (written, not agreed), `Accepted` (in force),
  `Superseded by ADR-NNNN`, `Rejected` (considered and declined — keep it, it stops the
  idea coming back).
- **No code before `Accepted`.** `decision`-tagged roadmap items must not be implemented
  until the ADR exists. This is the rule the reverted "fixes" broke.

## Index

| ADR | Title | Status | Roadmap |
|-----|-------|--------|---------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted | 12.3 |

## Awaiting an ADR

The open `decision` items in `ROADMAP.md`, as of 2026-07-23. Grouped by the session that
should settle them — the roadmap's "Start here" table sets the order.

| Session | Roadmap items | The question |
|---------|---------------|--------------|
| — | 9.1 | What does v1 mean? Scopes everything else |
| A | 5.3, 5.4, 1.2, 7.7 | What do our scores mean, and on what scale? |
| B | 4.11 | Do we port the `title_parser` bug faithfully, or diverge from Rust? |
| — | 7.16 | How does this app represent time? Scopes 7.3 and 7.11 |
| — | 8.5 | Threat model — gates whether 4.5 encryption is required |
| — | 5.6 | What should `longest_active_stretch_5min` report for a new session? |
| — | 7.8 | Should `set_focus_mode` rewrite the user's default? |
| — | 9.10 | Retention window: user setting, and what default? |
| — | 10.2 | Dashboard information architecture |
| — | 13.5, 13.6 | Is there labelled data to train on, and who wins when model and heuristic disagree? |

Keep this table in sync when an item graduates: delete the row, add an Index row.
