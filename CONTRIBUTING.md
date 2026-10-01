# Working in this repository

Read this before your first change. It is the conventions that are **enforced but not
obvious** — the ones where following your instincts produces a red build or, worse, a quiet
inconsistency nobody notices for a month.

[`AGENTS.md`](AGENTS.md) is the short entry point for coding agents; this file holds the
conventions for every contributor.

## The one rule with no exceptions

**Every commit is Kassa's own work.** No `Co-Authored-By:` trailer, no "Generated with …"
footer, no AI or vendor attribution, in commit messages or PR bodies. Do not pass `--author`
and do not change `git config`.

`scripts/check_commit_attribution.py` enforces this in CI. Enable the local hook once per
clone so an invalid message is rejected before the commit is created:

```sh
git config core.hooksPath scripts/hooks
```

## Where work is tracked

- [`docs/ROADMAP.md`](docs/ROADMAP.md) is the **only** backlog. No parallel TODO lists, no
  session notes checked into the tree. A `docs/scratch/` directory existed once; it went
  stale, contradicted the accepted record, and was deleted.
- **Its tracked items hold open work only, and every item names a status.** `proposed` is the
  default: a finding, however well evidenced, that nobody has agreed to build. `accepted`
  needs a citable agreement — a row in the roadmap's own "Start here" sequence, or a release
  blocker in an ADR.
  `in progress` means part of it landed. Promoting an item is a deliberate edit, never a side
  effect of writing it persuasively. Completed items move to
  [`docs/roadmap_archive.md`](docs/roadmap_archive.md); `scripts/check_roadmap_status.py`
  enforces both rules.
- Items tagged **`decision`** must not be implemented until an ADR exists. This rule was
  written after two "fixes" were made and then reverted because the reasoning behind the
  original shape was nowhere on disk.
- Decisions land in [`docs/adr/`](docs/adr/README.md). ADRs are append-only: never edit one to
  say something new; write a new one and mark the old superseded.
- **`Accepted` on an ADR means agreed, not built.** ADRs are written in the present tense, so
  one whose code has not landed reads exactly like a description of the tree. If you write an
  ADR ahead of its implementation, say so in the header and add an `## Implementation status`
  section. [ADR-0007](docs/adr/0007-time-is-integer-milliseconds-utc.md) now marks its
  integer-millisecond schema decision as applied.

**Trust the roadmap's claims about the world, not its claims about the code.** The file says
this itself: when an item says something is missing, check that it is actually missing; when
it says something is done, check the code has a caller. Historical item bodies and progress are retained in
the roadmap archive, explicitly distinguished from current open work. They describe the
inspected or completed state, not today's implementation.

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

Run the local headless checks and repository guards with `python scripts/verify.py` on any
platform. The older `./scripts/test_local.sh` (macOS/Linux) and `scripts/test_local.ps1`
(Windows) wrappers remain for native and frontend build/test iteration.

## Style

`.clang-format`, `.clang-tidy`, `frontend/eslint.config.js`, and `frontend/prettier.config.js`
describe the house style. Every value in them was measured against the code that already
exists rather than chosen, so they describe this codebase rather than imposing a different one.

**Existing files were deliberately not reformatted.** The dedicated formatting CI job was
removed; formatting is now a contributor check, while frontend ESLint remains a CI gate:

| File | Formatting |
| --- | --- |
| **Added** by your change | Format it before review. Check C++ with `clang-format --dry-run --Werror <file>` or frontend code with `npx prettier --check <file>` from `frontend/` |
| Already existed | Formatting is advisory; do not reformat it as part of a behavior change |

The split is by file age rather than by what you touched, because "format what you touch"
collapses on a large legacy file: `frontend/src/App.tsx` is 830 lines and 606 of them move
under Prettier, so a one-line edit would demand a 606-line reformat or a red build. Nobody
follows a rule like that; they route around it.

Converting a pre-existing file **is welcome** — do it in a commit that contains nothing else,
so the diff is reviewable as pure formatting instead of hidden inside a behaviour change.

`npm run lint` must be clean (warnings are fine, errors are not). CI does not run
`clang-tidy` yet — `.clang-tidy` is there so editors and `clangd` agree on a check set; see
the note at the top of that file.

Comments explain *why*, particularly why an obvious-looking alternative is wrong, and stay
short. History belongs in git and the roadmap archive, not in code: no roadmap item numbers,
no "this used to…", no retelling of the bug that motivated a change. A test may say what
regression it guards against, in a sentence.

## Two JSON boundaries

The IPC surface uses **camelCase** keys with **snake_case** command names. The training and
fixture data (`CaptureEvent` only) uses snake_case keys, because its consumer is the training
tooling, not the dashboard. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) has the table and
the reasoning, along with a glossary of the six different types called a "summary".

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
