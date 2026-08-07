# ADR-0006 — Training tooling is developer-only

- **Status:** Accepted
- **Date:** 2026-08-07
- **Roadmap item:** 13.7
- **Decided by:** Kassa

## Question

Is in-app model training a consumer product feature, or repository tooling that must not
appear in a normal install?

## Context

Settings leads with a "Train the model" card that asks for a Snapback repository path and
reports readiness only when that path contains `ml/pipeline_cli.py`. Help text tells the
user to install `ml/requirements-train.txt`. **This checkout has no `ml/` directory**, and a
packaged application would not ship a source tree. The first Settings card therefore
advertises a workflow neither this tree nor an installed build can satisfy.

Two real products were confused into one UI:

- **Focus feedback labels** — a session user marking Deep / Focused / Drift / Distracted.
  That is product data capture and stays.
- **Train / deploy / repo-path / CLI** — a developer loop that requires a checkout, Python
  training deps, and optional ONNX packaging. That is not a consumer feature today.

## Options considered

| Option | Upside | Downside |
|--------|--------|----------|
| A. Packaged on-device training | Honest end-user personalization | Needs restoring/packaging `ml/`, licences, progress IPC (**14.6**), and installed-artifact smoke — an `L` project with no pipeline in tree |
| B. Developer-only model tooling | Stops lying in Settings immediately; keeps export/deploy for people with a repo | Personalization stays out of the consumer path until A is rebuilt for real |

## Decision

**Training, repo-path configuration, in-app train-from-export, and the CLI copy surface are
developer tooling.** They appear only in Debug builds, or in a Release build when
`SNAPBACK_DEV_TRAINING` is set. A normal Release install shows focus-feedback labels and
does not mention the missing `ml/` tree.

Consumer Settings must not instruct users to install training deps or point at a repo path.

## Why

Option A is the better long-term product, but accepting it *now* without a packageable
pipeline recreates the third state 13.7 forbids: a prominent path that cannot complete.
Option B is the only honest answer given the tree that exists. It does not reject A — it
requires A to ship as a real packaged feature before Settings may advertise it again.

Focus labels stay visible because they are not training UI: they are the user's verdict on a
moment, and they remain useful even when no trainer is present (Review, future data export,
eventual A).

## Consequences

- Normal Release UI: Focus Feedback labels only; no train readiness, repo path, Train button,
  or `ml/` help text.
- Debug / `SNAPBACK_DEV_TRAINING`: the existing Training Deploy card remains for repo work.
- Native commands that start training or write a repo path refuse when developer tools are
  off (defense in depth — the capability token is not enough if a hostile page somehow
  reached them).
- **2.3** and further Tier 13 trainer work become repository tooling until a packaged
  pipeline lands.
- **13.8** startup recovery stays: optional ONNX debris must still not brick the core app,
  whether or not the consumer UI offers train/deploy.

## Revisit if

A packaged install ships a working trainer with no repo-path requirement and an installed
smoke that proves train → quality gate → activate without a developer checkout. Then write
a new ADR choosing option A and restore consumer training UI deliberately.
