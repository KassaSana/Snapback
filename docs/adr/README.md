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

- **One decision per file.** Decision session A covered five roadmap items (5.3, 5.4, 1.2,
  7.7, 7.18) because they share one *question* — one answer settled all five, so it is one
  ADR. It would have been five if it hadn't.
- **ADRs are append-only.** Never edit a decision to say something new. Write a new ADR and
  set the old one's status to `Superseded by ADR-NNNN`. The wrong turn is the useful part.
- **Status vocabulary:** `Proposed` (written, not agreed), `Accepted` (in force),
  `Superseded by ADR-NNNN`, `Rejected` (considered and declined — keep it, it stops the
  idea coming back).
- **`Accepted` means agreed, not built.** ADRs are written in the present tense — "Scores are
  `[0,1]` everywhere" — which reads as a description of the code, and usually is one, because
  the code lands with the decision. When it does not, say so **in the header**, with an
  `Applied:` line and an `## Implementation status` section naming what is actually true
  today and which roadmap item carries the remaining work. ADR-0007 is the worked example.
  Without that, the most authoritative document in the repository confidently describes a
  system nobody has built, and the next reader writes code against it.
- **No code before `Accepted`.** `decision`-tagged roadmap items must not be implemented
  until the ADR exists. This is the rule the reverted "fixes" broke.

## Index

| ADR | Title | Status | Roadmap |
|-----|-------|--------|---------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted | 12.3 |
| [0002](0002-v1-supports-windows-and-macos.md) | v1 supports Windows and macOS | Accepted | 9.1 |
| [0003](0003-three-surface-dashboard.md) | Split the dashboard into Now, Review, and Settings | Accepted | 10.2 |
| [0004](0004-verdict-and-opinion.md) | The state is the policy verdict, the scores are the model's opinion | Accepted | 7.7, 7.18, 5.3, 5.4, 1.2 |
| [0005](0005-a-session-is-declared-and-attended.md) | A session is declared by the user and attended by the user | Accepted | 2.7, 7.23, 2.8 |
| [0006](0006-trainer-is-developer-tooling.md) | Training tooling is developer-only | Accepted | 13.7 |
| [0007](0007-time-is-integer-milliseconds-utc.md) | A point in time is UTC milliseconds since the epoch, stored as INTEGER | Accepted — **not yet applied** | 7.16 |
| [0008](0008-protect-master-from-red-ci.md) | Protect `master` from red CI | Accepted | 6.2 |

## Awaiting an ADR

The open `decision` items in `ROADMAP.md`, as of 2026-08-03. Grouped by the session that
should settle them — the roadmap's "Start here" table sets the order.

| Session | Roadmap items | The question |
|---------|---------------|--------------|
| — | 8.5 | Threat model — gates whether 4.5 encryption is required |
| — | 5.6 | What should `longest_active_stretch_5min` report for a new session? |
| — | 7.8 | Should `set_focus_mode` rewrite the user's default? |
| — | 9.10 | Retention window: user setting, and what default? |
| — | 13.5, 13.6 | Is there labelled data to train on, and who wins when model and heuristic disagree? |

Keep this table in sync when an item graduates: delete the row, add an Index row.
