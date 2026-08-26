# ADR-0007 — A point in time is UTC milliseconds since the epoch, stored as INTEGER

- **Status:** Accepted
- **Applied:** Yes — schema v7 and the IPC contract, 2026-08-24.
- **Date:** 2026-08-19
- **Roadmap item:** 7.16 (scopes 5.5, 7.1, 7.2)
- **Decided by:** Kassa

## Question

How does this app represent a point in time — in the database, in C++, and on the way to the
user? Four open findings are four symptoms of that one question never having been answered.

## Context

Nothing here was theorized; each fact below was checked against the tree on 2026-08-19.

**The schema already disagrees with itself.** Eight columns store time as `TEXT`
(`storage.cpp:migrate_baseline_schema`, plus `created_at`/`updated_at` on `app_rules`)
and one stores it as `REAL` Unix seconds (`feature_snapshots.timestamp`, `storage.cpp:migrate_baseline_schema`).
This is not a style difference; the code pays for it. `retention_cutoff_rfc3339` and
`retention_cutoff_unix_secs` (`storage.cpp:retention_cutoff_unix_secs`, `storage.cpp:retention_cutoff_rfc3339`) exist as a matched pair for no reason
other than expressing one instant in two storage formats.

**Ordering is undefined at the resolution the app writes.** `AppState::rfc3339_at` takes a
`std::time_t` and formats `%Y-%m-%dT%H:%M:%SZ` (`state.cpp:rfc3339_at`) — there is no sub-second
field to lose, because none is ever produced. Two sessions started in the same wall-clock
second tie under `ORDER BY started_at DESC` and come back in an arbitrary order. This is
already worked around in the test suite via `Storage::backdate_session_for_test`, and it is
visible to users as an arbitrarily ordered history list.

**Roughly thirty SQL expressions do date arithmetic** (`datetime`, `julianday`, `strftime`),
and several wrap a column that is indexed. `DELETE FROM predictions WHERE datetime(timestamp)
< datetime(?1)` (`storage.cpp:prune_runtime_data`, and `context_snapshots` in the same function) cannot use
`idx_predictions_ts`, so retention full-scans the largest tables in the database.

**The retention finding is real but narrower than 5.5 records it.** 5.5 says retention
"silently never deletes". Executed against SQLite 3.35.5, well-formed rows *do* delete;
`datetime()` parses `2026-08-19T07:00:00Z` fine. What actually happens is that `datetime()`
returns `NULL` for an unparseable value, `NULL < x` is `NULL`, and those rows survive every
retention pass forever with no error. That was a small hazard while every row was written by
`rfc3339_at`. **9.14 changed that**: there is now an import path, so rows the app did not
write can enter the database, and the one class of row that silently outlives retention is
exactly the class most likely to arrive through it. Correcting the claim is part of this
decision — the next audit should not re-derive a bug that is not there and miss the one that
is.

The forces pulling the other way are real. Text timestamps are readable in a DB browser
without arithmetic, the reporting SQL reads naturally today, and a representation change
touches every table that matters.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. `INTEGER` epoch milliseconds, UTC, everywhere | One type; sub-second ordering; comparisons are plain integer predicates so indexes apply; `feature_snapshots` stops being an exception; no parsing, so no `NULL`-swallowing class of bug | Largest migration — 9 columns and ~30 expressions; values not human-readable in a DB browser; local-time bucketing needs an explicit conversion |
| B. Strict fixed-width RFC3339 `TEXT` with milliseconds | Smaller migration (pad with `.000`); stays human-readable; fixed-width UTC sorts lexicographically, so `<` works and uses the index | Leaves `feature_snapshots` as `REAL`, so the split survives as a permanent documented exception; still parses text for every date computation; still one careless `datetime(col)` away from the original bug |
| C. Minimal fix: unwrap the indexed column, add sub-second resolution | One sitting; fixes both live symptoms | Four findings keep their shared root cause; the schema still disagrees with itself; the next time-shaped feature re-opens all of it |

## Decision

A point in time is **milliseconds since the Unix epoch, UTC, stored as `INTEGER`** — in every
table, on the IPC boundary, and in C++.

Local time is a **presentation concern only**. It is applied at the point a value is shown or
bucketed for a report, never at the point it is stored, filtered, or compared.

## Why

Option A is the only one that removes the *class* of defect rather than its current instances.
Integers do not fail to parse, so there is no `NULL`-swallowing comparison to write by
accident; the retention bug is not fixed so much as made unexpressible. Filters become plain
integer predicates, which is what makes `idx_predictions_ts` usable without anyone having to
remember not to wrap a column.

**The decisive factor is timing, not purity.** Option B is genuinely cheaper today, and on a
shipped product with real user databases to migrate it would probably win. Nothing has
shipped — the CHANGELOG records that `v0.2.0` points at a commit no longer on any branch and
that there is no published baseline. The only database that needs migrating is a developer's.
This is the cheapest this change will ever be, and it gets monotonically more expensive from
here. Choosing B to save effort now means paying more for the same change later, or keeping
the schema split permanently.

Option B's specific weakness is that it *keeps* `feature_snapshots.timestamp` as `REAL`.
Converting that column to text would be actively wrong — the feature pipeline wants numeric
time deltas — so B does not end the inconsistency it is meant to settle; it ratifies it. Under
A that column stops being an exception and becomes the general case.

Option C was rejected because 7.16 exists precisely to stop these four items being patched
separately. Patching them separately is what leaves the root cause in place.

**This flips if** the app ships and accumulates user databases before the migration lands. At
that point the migration risk starts to outweigh the schema cleanliness, and B becomes the
defensible answer.

## Consequences

- Nine columns change type; a schema migration rewrites existing rows. The migration must be
  ordered behind a `user_version` bump like every other, and 7.22's pre-migration `VACUUM
  INTO` backup covers it.
- `AppState::rfc3339_at` / `now_rfc3339` are replaced by a single `now_unix_ms()`. The
  RFC3339 string becomes an output format, produced at the edge, not a storage format.
- `retention_cutoff_rfc3339` and `retention_cutoff_unix_secs` collapse into one function.
- The two retention `DELETE`s become `WHERE timestamp < ?1` and start using their indexes.
- Reporting SQL keeps its local-time bucketing but converts explicitly:
  `strftime('%H', timestamp/1000.0, 'unixepoch', 'localtime')`.
- The IPC contract changes shape: timestamps cross as numbers. `test_ipc_contract` and the
  frontend's formatting layer both have to move together.
- `Storage::backdate_session_for_test` can be reconsidered once ordering is well-defined —
  7.16 already flagged that seam as a workaround for this item.
- **Unblocks** 9.10 (retention window as a user setting), 7.1, 7.2, and the Review range work.
  Removes the ordering ambiguity behind the history list.
- **Forecloses** reading a raw timestamp out of the database without converting it. That is
  the real cost of this decision and it should be stated plainly rather than discovered.

## Revisit if

The app ships with real user databases before this migration lands — see *Why*. Absent that,
nothing should reopen this; a second representation is the thing this ADR exists to prevent.
