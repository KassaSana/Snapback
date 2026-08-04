# dependencies.md — what we fetch, and how to change it

ROADMAP 8.6. This is the update process for Snapback's C++ dependencies. The frontend's
dependencies are a separate story handled by npm and Dependabot; see
[running.md](running.md) for building either half.

## The rule

**Every fetched C++ dependency is pinned to something that cannot change underneath us.**
In practice that means one of two things:

| Mechanism | Pin | Used by |
|---|---|---|
| `FetchContent_Declare` + `GIT_REPOSITORY` | `GIT_TAG` is a 40-character **commit SHA** | `nlohmann_json`, `doctest`, `webview` |
| `FetchContent_Declare` + `URL` | `URL_HASH SHA256=…` | `sqlite_amalgamation` |

`scripts/check_dependency_pins.py` enforces this, and CI runs it. Adding a dependency with a
tag name fails the build.

## Why a tag is not a pin

A git tag is a mutable pointer. Upstream can move `v3.11.3` onto a different commit at any
time — accidentally, or because a release was re-cut, or because the account was compromised
— and our next clean build would compile the new tree with **no diff on our side to review**.
Nothing else would catch it: Dependabot watches our GitHub Actions and our npm tree, not
`FetchContent`. A commit SHA cannot move, because git object names are derived from content.

The same argument applies to a downloaded archive, which is why the SQLite amalgamation
carries a `URL_HASH` rather than just a URL.

This is not hypothetical caution about a small risk. These dependencies are compiled
directly into the binary we ship to users; they are not sandboxed, reviewed, or isolated in
any way.

## Bumping a dependency

1. **Pick the version** and find the commit it points at:

   ```sh
   git ls-remote https://github.com/nlohmann/json refs/tags/v3.11.3 'refs/tags/v3.11.3^{}'
   ```

2. **Take the right hash.** If the command prints *two* lines, the tag is **annotated**: the
   `refs/tags/<v>` line is the tag *object* and the `refs/tags/<v>^{}` line is the commit.
   **Use the `^{}` one** — CMake cannot check out a tag object. If it prints one line the tag
   is lightweight and that line is the commit.

   This is a real trap rather than a theoretical one: `webview` uses annotated tags and
   `nlohmann/json` and `doctest` do not, so two of the three pins in `CMakeLists.txt` would
   be resolved correctly by the naive command and the third would not.

3. **Edit `CMakeLists.txt`**, putting the human-readable version in the trailing comment.
   A bare hash tells the next reader nothing about what they are looking at:

   ```cmake
   GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03  # v3.11.3
   ```

4. **Configure into a fresh build tree**, so the fetch actually happens rather than reusing
   the already-downloaded copy in `build/_deps`:

   ```sh
   cmake -S . -B build-deps-check -DCMAKE_BUILD_TYPE=Debug
   cmake --build build-deps-check -j8 --target snapback_tests
   ./build-deps-check/snapback_tests
   rm -rf build-deps-check      # remove throwaway trees you create
   ```

   A configure against the existing `build/` proves nothing about the new pin, because
   `FetchContent` will not re-fetch a dependency it already has.

5. **Run the guard**: `python3 scripts/check_dependency_pins.py --verbose`.

## ONNX Runtime is pinned separately

ROADMAP 8.9. The ONNX Runtime does not arrive through `FetchContent`, so the guard above
never saw it. The two ONNX CI jobs download a **prebuilt** runtime archive from a GitHub
release and extract it into `third_party/`, where CMake links it into the test binary.

A release URL is not a pin, for the same reason a git tag is not: GitHub release assets can
be deleted and re-uploaded under the same name. The digest is the pin; the version beside it
is for humans.

[`third_party/onnxruntime-pins.json`](../third_party/onnxruntime-pins.json) is the single
source of truth. Both jobs read the version, URL, filename, and expected SHA-256 out of it
and restate none of them, and `scripts/check_onnx_pins.py` enforces that arrangement.

### Bumping ONNX Runtime

1. **Download both archives** for the new version and hash them:

   ```sh
   v=1.21.0
   for a in onnxruntime-win-x64-$v.zip onnxruntime-linux-x64-$v.tgz; do
     curl --fail --location -O "https://github.com/microsoft/onnxruntime/releases/download/v$v/$a"
     sha256sum "$a"
   done
   ```

2. **Update `version`, `release_url`, and both `file`/`sha256` pairs together.** The guard
   requires each filename to contain the manifest's version, so a bump that changes the
   version and forgets to re-hash fails rather than verifying a stale digest.

3. **Run the guard**: `python3 scripts/check_onnx_pins.py --verbose`.

The guard also fails if a job extracts before verifying, hardcodes a version or digest that
the manifest already holds, or renames the vendor step out from under the parser. That last
one matters: a guard that silently stops finding anything reports success forever.

## What is deliberately not covered

- **No advisory monitoring.** Nothing watches these four projects for CVEs today; pinning
  makes builds reproducible, which is a different property from being current. Pinning
  arguably makes staleness *more* likely, since there is no longer a tag quietly pulling in
  patch releases — that is the trade being accepted, not an oversight. Revisit under 8.5 once
  there is a threat model to weigh it against.
- **Vendored SQLite.** `third_party/sqlite/` is preferred over the fetch when present, so
  offline builds work. That copy is checked in and reviewable, which is a stronger guarantee
  than either mechanism above.
