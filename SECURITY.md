# Security policy

Snapback is a local-first desktop application. It records window titles, application names,
input timing, and session goals on the user's own machine, and release builds open no network
connections for product features. The threat model that defines what the project defends
against, and what it explicitly does not, is
[ADR-0009](docs/adr/0009-local-first-threat-model.md). Read it before reporting: several
things that look like findings are recorded there as accepted v1 limits.

## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting for this repository
(**Security → Report a vulnerability**). Do not open a public issue for anything that could
expose a user's recorded activity.

Include the release or commit you tested, the platform, the steps to reproduce, and what an
attacker gains. A proof of concept is welcome; a captured copy of someone else's `focoflow.db`
is not.

Expect an acknowledgement within seven days. Fixes ship in the next release; there is no
embargo period beyond the time it takes to build one, because there is one maintainer and no
downstream distributors to coordinate.

## What is in scope

- Any way for a release build to send recorded data off the machine. Release builds are
  meant to be network-silent; a socket, beacon, or remote subresource is a bug
  (Roadmap 8.10, enforced by `scripts/check_no_remote_subresources.py`).
- Any way for a page other than the bundled frontend to invoke a native command through the
  webview bridge, including after a redirect or navigation.
- Any path by which "Delete all activity" or "Export my data" leaves out, or silently
  retains, a copy of recorded activity that the product claims to have removed or included.
- Data written outside the per-user data directory, or written with permissions broader
  than the owning user, on any supported platform.
- Privilege escalation through the installer, the start-on-login registration, or the
  trainer tooling.

## What is out of scope for v1

These are decided limits, not oversights. Reports about them will be acknowledged and
closed with a pointer to the decision.

- **Other software running as the same OS user** can read `focoflow.db` and exports. The
  database is not encrypted at rest (ADR-0009; Roadmap 4.5 is the optional follow-up).
- **Physical access to an unlocked session** is the user's responsibility, mitigated only
  by OS login, screen lock, and the lock-screen notification redaction setting.
- **Windows profile isolation** between OS accounts is relied on, not hardened beyond it.
- The **demo site** serves generated sample data and has no native bridge; findings about it
  that do not affect the desktop application are low priority.

## Supported versions

Fixes land on `master` and ship in the next tagged release. Older tags are not patched.

## Attribution

Reporters are credited in the release notes if they want to be. Ask, and say how you would
like to be named.
