# ADR-0009 — Local-first threat model for v1

- **Status:** Accepted
- **Date:** 2026-08-27
- **Roadmap item:** 8.5
- **Decided by:** Kassa

## Question

Who can hurt the user through Snapback's data, what does "local-only" actually promise, and
is encryption at rest required for v1?

## Context

`snapback.db` (default under the per-user data directory) is an **unencrypted SQLite** file
holding window titles, app names, session goals, behavioural features, and derived scores.
Exports (`VACUUM INTO`, CSV, support bundle) copy the same material to paths the user
chooses. Release builds are **network-silent** ([ADR-0002](0002-v1-supports-windows-and-macos.md),
Roadmap 8.10): the shipped frontend has no remote origins and the native app does not phone
home.

The Privacy card tells users **"Nothing leaves this device."** Users reasonably hear that as
"private," but it is narrower: it is a **no-transmission** claim, not confidentiality against
every other program on the machine. On Windows, any process running as the same user can open
files the user can open; on Unix, `src/util/private_dir.cpp` restricts group/other mode bits
on the data directory but cannot stop same-user malware.

Roadmap **4.5** (SQLCipher) and **8.12** (secure deletion) were blocked on this document.
Roadmap **3.7** (cloud sync) would supersede part of the local-only promise and needs its own
ADR if pursued.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Treat "local-only" as full disk confidentiality | Matches a naive reading of Privacy copy | Requires SQLCipher or OS keychain integration, key recovery story, and perf cost — large scope for v1 |
| B. Treat "local-only" as no network egress; same-user exposure is explicit (chosen) | Honest for a personal desktop tool; matches 8.10 and current architecture | Users on shared or compromised machines need to understand the limit |
| C. Defer all privacy copy until encryption ships | Avoids overstating protection | Leaves the product mis-described today; encryption may never be v1 |

## Decision

**v1 threat model — in scope**

| Adversary | Capability assumed | What Snapback defends |
|-----------|-------------------|------------------------|
| **Network attacker** | Can reach the internet; cannot break TLS to Snapback | **Out of scope for v1 app** — release builds do not open outbound connections for product features (8.10). |
| **Other software as the same OS user** | Can read/write user-owned files, inject input, scrape the screen | **Partially mitigated:** Unix private directory modes (`private_dir.cpp`); Windows relies on user profile ACLs. **Not mitigated:** a malicious or compromised app running as the user can read `snapback.db` and exports. |
| **Physical access, unlocked session** | Can use the app, read the DB, read notifications/overlay | **User responsibility:** OS login, screen lock, Snapback's lock-screen notification redaction setting. |
| **Another OS account on the same machine** | Separate login | **Mitigated on Unix** when data dir is `0700`; **Windows** depends on profile isolation — treated as best-effort, not a v1 guarantee. |

**Data sensitivity (highest first)**

1. **Window titles and parsed context** — often name projects, people, health, finance.
2. **Session goals and reflections** — user-authored intent.
3. **Behavioural features and scores** — sensitive in aggregate but rarely as identifying as titles.
4. **Settings and rules** — lower sensitivity; still user data.

**What "Nothing leaves this device" means in v1**

- Snapback **does not** upload, sync, or beacon telemetry to a server controlled by the project.
- Snapback **does** write data to disk under the user's account and to export paths the user selects.
- Snapback **does not** encrypt the database at rest in v1.

**SQLCipher / encryption at rest:** **not required for v1.** Optional follow-up under Roadmap 4.5
if a concrete user story appears (shared workstation, compliance). If added later, it requires
key management, migration, and updated Privacy copy — not a silent upgrade.

**Uninstall and deletion:** Scoped by Roadmap 8.12. v1 promises explicit user-initiated delete
and export paths; wiping free pages and WAL remnants is a separate, documented hardening item.

## Why

Option B matches what the code actually does and what ADR-0002 committed to: a **local-first
desktop agent**, not a confidential enclave. Option A would be the right product for
enterprise shared machines but is not the author's stated v1 user (single-user Windows +
macOS desktop). Option C leaves the Privacy card overstating protection.

The distinction matters for audits: **8.10 proved network silence**; it did not prove
confidentiality against local malware. Recording that gap stops it becoming an accidental lie.

## Consequences

- Privacy and onboarding copy should say **local-only / no cloud**, not "encrypted" or
  "hidden from other apps," unless 4.5 ships.
- **4.5** (SQLCipher) remains optional; this ADR does not schedule it.
- **8.12** secure deletion remains the right place for "data gone after uninstall" semantics.
- **3.7** cloud product requires a new ADR that supersedes the no-transmission parts of this
  model and ADR-0002's privacy framing.
- Support bundle and export flows must keep listing what leaves the machine when the user
  explicitly exports (already true in Privacy card scope text).

## Revisit if

- A real user needs **shared-machine** or **compliance** confidentiality → reopen 4.5.
- **Cloud sync** is scheduled → new ADR, supersede transmission sections here.
- OS sandboxing (App Sandbox, container) materially changes same-user read risk on a shipped
  platform.
