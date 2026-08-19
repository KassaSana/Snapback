# Working in this repository

Read this before your first change. It is the conventions that are **enforced but not
obvious** — the ones where following your instincts produces a red build or, worse, a quiet
inconsistency nobody notices for a month.

This file is committed on purpose. Guidance that lives only in a personal, gitignored agent
file (`CLAUDE.md` and friends are gitignored here, deliberately, and docs may not cite them
as paths) is guidance no clone has and no tool but its owner's can read. Before this file
existed, the commit-attribution rule below was written down in exactly one place —
`.cursor/rules/commit-attribution.mdc` — which meant Cursor knew about it and nothing else
did, including a human reading the repo for the first time.

## The one rule with no exceptions

**Every commit is Kassa's own work.** No `Co-Authored-By:` trailer, no "Generated with …"
footer, no AI or vendor attribution, in commit messages or PR bodies. Do not pass `--author`
and do not change `git config`.

This is enforced, not trusted: `scripts/check_commit_attribution.py` walks every ref on every
CI run. It is a guard rather than a note because several tools append attribution
automatically, at commit time, when nobody is looking — and such a commit is permanent in a
way an ordinary mistake is not. Rewriting it changes every SHA after it, which invalidates
release tags and the CI-conclusion check the release gate reads.

Enable the local hook once per clone so a bad trailer never reaches a commit:

```sh
git config core.hooksPath scripts/hooks
```

Naming a tool in prose is fine. Claiming it wrote the code is not.

## Where work is tracked

- [`docs/ROADMAP.md`](docs/ROADMAP.md) is the **only** backlog. No parallel TODO lists, no
  session notes checked into the tree. A `docs/scratch/` directory existed once; it went
  stale, contradicted the accepted record, and was deleted.
- Items tagged **`decision`** must not be implemented until an ADR exists. This rule was
  written after two "fixes" were made and then reverted because the reasoning behind the
  original shape was nowhere on disk.
- Decisions land in [`docs/adr/`](docs/adr/README.md). ADRs are append-only: never edit one to
  say something new; write a new one and mark the old superseded.
- **`Accepted` on an ADR means agreed, not built.** ADRs are written in the present tense, so
  one whose code has not landed reads exactly like a description of the tree. If you write an
  ADR ahead of its implementation, say so in the header and add an `## Implementation status`
  section. [ADR-0007](docs/adr/0007-time-is-integer-milliseconds-utc.md) is the worked
  example — it says the schema is integer milliseconds, and the schema is not, yet.

**Trust the roadmap's claims about the world, not its claims about the code.** The file says
this itself: when an item says something is missing, check that it is actually missing; when
it says something is done, check the code has a caller. Items marked `DONE` keep their
original finding below the resolution as quoted history — that text is deliberately stale and
is not a description of the current tree.

## Citing code in docs and comments

Cite a **symbol**, never a line number:

```
good:  storage.cpp:kMigrations       state.cpp:health
bad:   storage.cpp:<any line number>
```

(The bad form is written with a placeholder on purpose: a real line number here would be
caught by the very guard this section is describing.)

`scripts/check_doc_symbols.py` fails the build on a bare line number and on a symbol that no
longer exists in the file it names. A line number is the one reference that rots *silently*:
the file keeps existing, so the path guard stays green while the number drifts onto unrelated
code. An audit on 2026-08-19 found 84 line-number citations across the docs, a large fraction
of them already pointing at blank lines, a closing brace, or past the end of a 20-line file.

Quoted historical code — the `The original finding was:` blocks — must **not** carry a
citation. Write "as it stood on `<date>`" instead, so nobody follows a live-looking pointer
into code that has deliberately changed since.

Paths are repo-relative everywhere, including inside `frontend/`: `src/` is the C++ tree and
`frontend/src/` is the dashboard.

## Guards you have to satisfy

`scripts/` holds the checks CI runs; [`scripts/README.md`](scripts/README.md) has the full
table, and `check_scripts_documented.py` keeps that table complete. The ones that most often
surprise a first change:

| If you… | …then |
| --- | --- |
| add a `frontend/tests/*.test.ts` | add it to the `test:unit` chain in `frontend/package.json`. It is hand-chained, not globbed, so an unlisted test runs nowhere and the suite still reports green |
| exclude a module from frontend coverage | it needs a test that `test:unit` actually runs |
| add a script to `scripts/` | give it a row in `scripts/README.md` |
| cite code | use `file:symbol` (above) |
| save a file from a Windows editor | make sure it did not add a UTF-8 BOM |
| add a CI job | add a row to `docs/testing_strategy.md`'s table |
| add a C++ dependency | pin it to a commit SHA or `URL_HASH`, never a tag — see [`docs/dependencies.md`](docs/dependencies.md) |

Run them all locally with the wrappers: `./scripts/test_local.sh` (macOS/Linux) or
`scripts/test_local.ps1` (Windows).

## Style

`.clang-format`, `.clang-tidy`, `frontend/eslint.config.js`, and `frontend/prettier.config.js`
describe the house style. Every value in them was measured against the code that already
exists rather than chosen, so they describe this codebase rather than imposing a different one.

**Existing files were deliberately not reformatted**, and CI splits on that:

| File | Formatting |
| --- | --- |
| **Added** by your change | **Enforced.** New code is free to get right, and this is the only moment it is |
| Already existed | **Advisory.** Reported in the `format-check` job, never fails the build |

The split is by file age rather than by what you touched, because "format what you touch"
collapses on a large legacy file: `frontend/src/App.tsx` is 830 lines and 606 of them move
under Prettier, so a one-line edit would demand a 606-line reformat or a red build. Nobody
follows a rule like that; they route around it.

Converting a pre-existing file **is welcome** — do it in a commit that contains nothing else,
so the diff is reviewable as pure formatting instead of hidden inside a behaviour change.

`npm run lint` must be clean (warnings are fine, errors are not). CI does not run
`clang-tidy` yet — `.clang-tidy` is there so editors and `clangd` agree on a check set; see
the note at the top of that file.

The prose style in comments is part of the style. Comments here explain *why*, and
particularly why an obvious-looking alternative is wrong. Match the density of the file you
are in.

## Two JSON boundaries

The IPC surface uses **camelCase** keys with **snake_case** command names. The training and
fixture data (`CaptureEvent` only) uses snake_case keys, because its consumer is the training
tooling, not the dashboard. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) has the table and
the reasoning, along with a glossary of the five different types called a "summary".

## Searching the tree

Build directories are gitignored but present, and large — a local `build-msvc/` runs to
hundreds of megabytes, most of it vendored third-party sources under `_deps/`. Any search that
ignores `.gitignore` (`find`, `Get-ChildItem -Recurse`, a naive file walk) will surface
doctest, nlohmann/json, and the SQLite amalgamation as if they were project code. Prefer
`git ls-files` or a `.gitignore`-aware search tool.

## Build and test

[`docs/running.md`](docs/running.md) has the per-OS commands, the environment variables, and a
troubleshooting table. [`docs/testing_strategy.md`](docs/testing_strategy.md) explains what
each CI job actually proves and — just as usefully — what none of them do.
