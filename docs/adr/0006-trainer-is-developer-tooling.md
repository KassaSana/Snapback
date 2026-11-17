# ADR-0006: Training tooling is developer-only

- **Status:** Accepted
- **Date:** 2026-08-07

## Question

Is in-app model training a user feature, or repository tooling that shouldn't appear in a
normal install?

## Context

Settings opened with a "Train the model" card. It asked for a path to a Snapback checkout and
only reported ready if that path contained `ml/pipeline_cli.py`, and its help text told the user
to install Python training dependencies. An installed app has no source tree, so the first card
in Settings advertised something no user could complete.

Two different things had been mixed together:

- **Focus feedback:** the user marking a moment as Deep work, Productive, Drift or Distracted.
  That's product data and stays.
- **Train, deploy, repo path, CLI:** a developer loop that needs a checkout and Python. Not a
  user feature.

## Options

| Option | Upside | Downside |
|--------|--------|----------|
| A. Ship on-device training | Real personalisation | Needs a packaged pipeline, licences, progress reporting and install tests. A large project |
| B. Make it developer-only | Settings stops promising something it can't do; export and deploy still work for developers | Personalisation stays out of the user-facing app for now |

## Decision

**Training, the repo-path setting, train-from-export and the CLI instructions are developer
tools.** They appear only in Debug builds, or in Release when `SNAPBACK_DEV_TRAINING` is set. A
normal install shows focus feedback and nothing about training.

## Why

Option A is the better product eventually, but shipping it without a packaged pipeline would
leave a prominent feature that can't complete. Option B doesn't rule A out; it just means A has
to really work before Settings advertises it again.

## Consequences

- In a normal Release build, Settings shows focus feedback only.
- The native commands that start training or set a repo path refuse to run when developer tools
  are off, not just hide in the UI.
- Recovering from a bad model is still available to everyone. Rollback isn't gated, because
  someone whose model was broken by a deployment shouldn't need a developer build to fix it.

## Revisit if

A packaged install can train, check quality and activate a model with no checkout. Then write a
new ADR choosing option A.
