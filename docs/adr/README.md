# Architecture decisions

Short records of design decisions and why they were made, so they don't get re-argued or
accidentally undone.

To add one, copy [`0000-template.md`](0000-template.md) to the next number. ADRs are
append-only: to change a decision, write a new ADR and mark the old one
`Superseded by ADR-NNNN`.

| ADR | Decision | Status |
|-----|----------|--------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-v1-supports-windows-and-macos.md) | v1 supports Windows and macOS | Accepted |
| [0003](0003-three-surface-dashboard.md) | Split the dashboard into Now, Review and Settings | Accepted |
| [0004](0004-verdict-and-opinion.md) | The state is the policy verdict, the scores are the model's opinion | Accepted |
| [0005](0005-a-session-is-declared-and-attended.md) | A session is declared and attended by the user | Accepted |
| [0006](0006-trainer-is-developer-tooling.md) | Training tooling is developer-only | Accepted |
| [0007](0007-time-is-integer-milliseconds-utc.md) | Time is UTC milliseconds since the epoch, stored as INTEGER | Accepted |
| [0008](0008-protect-master-from-red-ci.md) | Protect `master` from red CI | Accepted |
| [0009](0009-local-first-threat-model.md) | Local-first threat model for v1 | Accepted |

Open questions are listed in the [roadmap](../ROADMAP.md#open-questions).
