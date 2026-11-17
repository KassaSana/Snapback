# ADR-0007: A point in time is UTC milliseconds since the epoch, stored as INTEGER

- **Status:** Accepted, applied in schema v7 (2026-08-24)
- **Date:** 2026-08-19

## Question

How does the app represent a point in time, in the database, in C++ and over IPC?

## Context

When this was decided:

- **The schema disagreed with itself.** Eight columns stored time as RFC3339 `TEXT` and one,
  `feature_snapshots.timestamp`, as `REAL` Unix seconds. The code carried matching pairs of
  helpers to express one instant in both formats.
- **Ordering was undefined within a second.** Timestamps were written at whole-second
  resolution, so two sessions started in the same second came back in arbitrary order. Tests
  needed a backdating hook to work around it, and users saw it as a jumbled history list.
- **About thirty SQL expressions did date arithmetic** (`datetime`, `julianday`, `strftime`),
  several around indexed columns. Retention ran
  `DELETE ... WHERE datetime(timestamp) < datetime(?1)`, which can't use the index, so it
  scanned the largest tables in full.
- **Malformed timestamps were never deleted.** `datetime()` returns NULL for anything it can't
  parse, `NULL < x` is NULL, and those rows silently survived retention forever. That was
  minor while the app wrote every row itself, but data import meant rows could now come from
  elsewhere.

On the other side: text timestamps are readable in a DB browser, and changing representation
touches every important table.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. `INTEGER` epoch milliseconds, UTC, everywhere | One type; sub-second ordering; plain integer comparisons that use indexes; nothing to parse, so nothing to silently fail | Biggest migration (9 columns, ~30 expressions); not human-readable; local-time grouping needs an explicit conversion |
| B. Strict fixed-width RFC3339 text with milliseconds | Smaller migration; still readable; sorts correctly as text | `feature_snapshots` stays `REAL`, so the split becomes permanent; still one careless `datetime(col)` away from the original bug |
| C. Minimal fix: unwrap the indexed column, add milliseconds | One sitting | Leaves the root cause in place for the next time-related feature |

## Decision

A point in time is **milliseconds since the Unix epoch, UTC, stored as `INTEGER`**: in every
table, over IPC, and in C++.

Local time is only for display. It's applied when a value is shown or grouped for a report,
never when it's stored, filtered or compared.

## Why

A removes the whole class of bug instead of the current instances. Integers can't fail to
parse, so the retention bug can't be written at all, and filters are plain integer predicates
that use the indexes without anyone having to remember not to wrap a column.

The deciding factor was timing. B is cheaper on a product with real user databases to migrate.
At the time nothing had shipped, so the only database to migrate was a developer's. This was
the cheapest this change would ever be.

B also keeps `feature_snapshots.timestamp` as `REAL`, and converting that column to text would
be wrong because the feature pipeline wants numeric deltas. So B would make the split
permanent. Under A, that column becomes the normal case.

## Consequences

- Nine columns change type, and a migration rewrites existing rows behind a `user_version`
  bump, with a backup first.
- One `now_unix_ms()` replaces the RFC3339 helpers. RFC3339 is now only an output format.
- Retention deletes become `WHERE timestamp < ?1` and use their indexes.
- Reports group by local time explicitly:
  `strftime('%H', timestamp/1000.0, 'unixepoch', 'localtime')`.
- Timestamps cross IPC as numbers, and the frontend formats them.
- You can't read a raw timestamp in the database without converting it. That's the real cost.

## Revisit if

Nothing should reopen this. A second time representation is exactly what this exists to
prevent.
