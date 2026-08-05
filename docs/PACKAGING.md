# Packaging and release decisions

## Current state

- **Unsigned Windows ZIP** via CPack (`scripts/package_windows.ps1`)
- **Optional IExpress self-extractor** when `iexpress` is available
- **Bundled frontend** copied beside `snapback.exe` for demo/release
- **CI:** headless tests on 3 OSes, ASan/UBSan, TSan, feature-parity fixtures, ONNX smoke, production-smoke workflow
- **Release:** a tag-driven workflow (`.github/workflows/release.yml`) builds + tests the
  Windows package and publishes it to GitHub Releases on a `v*` tag, behind the tag gate below
- **Signing hook:** release builds sign EXE artifacts when
  `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` is configured

## Cutting a release

ROADMAP 9.11. A `v*` tag is its own way into `release.yml`, and `ci.yml` does not run for
tag pushes — it runs on pushes and PRs to `main`/`master`. Without a gate, a tag could
publish a commit that never saw the macOS/Linux/sanitizer/ONNX matrix, or was never on
master at all. **Branch protection does not cover this: a tag is not a branch.**

So the order matters:

1. **Merge to `master`** and let `ci.yml` finish **green** on the merge commit.
2. **Bump `project(... VERSION x.y.z)` in `CMakeLists.txt`** if you have not already, and
   commit it — the tag must name that exact version.
3. **Tag that commit** and push: `git tag v0.2.0 && git push origin v0.2.0`.

The `verify-tag` job refuses to build unless all three hold:

| Check | Fails when |
|---|---|
| `scripts/check_release_tag.py` | the tag is not `vX.Y.Z`, or does not equal `PROJECT_VERSION` |
| `git merge-base --is-ancestor` | the tagged commit is not reachable from `origin/master` |
| `gh api .../ci.yml/runs?head_sha=…` | that commit has no completed, successful `ci.yml` run |

The third check reads CI's conclusion rather than re-running the matrix, because a copy of
the matrix here would drift from the real one. It fails closed: an API error or an
unexpected response is treated as unproven, not as a pass.

## Authenticode signing

Release builds should be signed so Windows SmartScreen does not warn on first run.

### Requirements

1. A code-signing certificate (EV recommended for immediate SmartScreen trust)
2. `signtool.exe` from the Windows SDK on the packaging machine
3. The certificate installed in the Windows certificate store for the runner user
4. `SignTool` timestamp server (the script uses `http://timestamp.digicert.com`)

For EV certificates, use a self-hosted Windows release runner with the vendor token/HSM
available to that runner. GitHub-hosted runners usually cannot access a physical EV token.

### Usage

`scripts/package_windows.ps1` accepts an optional certificate thumbprint:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 `
  -SignCertificate "THUMBPRINT_HERE"
```

When `-SignCertificate` is set, the script signs:

- `snapback.exe`
- the IExpress installer exe when produced

The ZIP package itself is not Authenticode-signed; Windows trust is established by signing
the executable content and installer executable.

### GitHub Release workflow

Set this repository secret:

- `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT`: the SHA-1 thumbprint of the installed signing
  certificate

When the secret is present, `.github/workflows/release.yml` passes it to
`scripts/package_windows.ps1 -SignCertificate`. When the secret is absent, the workflow
still builds and uploads unsigned artifacts.

### What we do not sign yet

- ONNX Runtime DLL (third-party; ship as-is)
- Frontend static assets (not executables)

## Auto-update (v1 decision)

**Decision: defer auto-update for v1.**

Rationale:

- Replicating secure delta updates is a full subsystem (signing, channels, rollback).
- v1 ships as a signed installer + manual upgrade path.
- Revisit when installer signing and CI release artifacts are stable.

Alternatives if needed later:

1. **Manual download** — link from README / GitHub Releases (simplest)
2. **In-app "check for updates"** — HTTP fetch of a version manifest + download link (no silent install)
3. **Full updater** — add signed manifests, channels, and rollback (high effort)

## macOS / Linux packaging

macOS `.app`/DMG packaging and notarization are the remaining external v1 release blocker
([Roadmap 3.3](ROADMAP.md)); they require an Apple Developer account, bundle identifier, and
signing/notarization credentials. Linux AppImage or distro packaging remains a post-v1 item.
