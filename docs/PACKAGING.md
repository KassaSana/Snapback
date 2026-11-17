# Packaging and releases

## Current state

- **Unsigned Windows ZIP** via CPack (`scripts/package_windows.ps1`)
- **Windows NSIS installer** via CPack, required unless `-SkipInstaller` is passed
- **Bundled frontend** copied beside `snapback.exe` for demo/release
- **CI:** headless tests on 3 OSes, ASan/UBSan, TSan, feature-parity fixtures, ONNX smoke, production-smoke workflow
- **Release:** a tag-driven workflow (`.github/workflows/release.yml`) builds + tests the
  Windows package and publishes it to GitHub Releases on a `v*` tag, behind the tag gate below
- **Signing hook:** release builds sign EXE artifacts when
  `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` is configured

## Why NSIS and not IExpress

The first installer was an IExpress self-extractor. `iexpress.exe` exits 1 on GitHub's
Windows runners with no error output, whatever the SED file says, so it never built in CI.
CPack's NSIS generator replaced it: it produces a real uninstaller and reports why it failed.
`release.yml` installs NSIS on the runner, and `package_windows.ps1` fails if `makensis` is
missing.

## Cutting a release

A `v*` tag triggers `release.yml`, and `ci.yml` doesn't run on tag pushes. Branch protection
doesn't cover tags either, so the release workflow checks that the tagged commit passed CI.
The order:

1. **Bump `project(... VERSION x.y.z)` in `CMakeLists.txt`** for the candidate and update
   release notes before merging.
2. **Merge the final candidate to `master`** and wait for `ci.yml` to finish **green on
   that exact commit**. Any subsequent version/documentation fix requires its own green run.
3. **Tag that commit** `vX.Y.Z` and push the tag; that publishes the release. A manual
   workflow run builds artifacts without publishing.

The `verify-tag` job refuses to build unless all three hold:

| Check | Fails when |
|---|---|
| `scripts/check_release_tag.py` | the tag is not `vX.Y.Z`, or does not equal `PROJECT_VERSION` |
| `git merge-base --is-ancestor` | the tagged commit is not reachable from `origin/master` |
| `gh api .../ci.yml/runs?head_sha=…` | that commit has no completed, successful `ci.yml` run |

The third check reads CI's result instead of re-running the matrix. An API error counts as a
failure.

## Outside the repo

| Item | What to do |
| --- | --- |
| Apple Developer account | Needed for macOS notarization and notifications |
| Code-signing certificate | Provision it and set `SNAPBACK_SIGN_CERTIFICATE_THUMBPRINT` on the release runner |
| GitHub Pages | Settings → Pages → Source: **GitHub Actions** (one-time, for the live demo) |

## Authenticode signing

Release builds should carry a verifiable publisher signature. The packaged signing success
path remains unverified until certificate provisioning and a signed artifact check complete.

### Requirements

1. A code-signing certificate; certificate type and provisioning are external release decisions
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

- `snapback.exe` — **immediately after the build, before CPack runs**
- the installer exe when produced — after CPack's NSIS run, the first moment it exists

The ZIP package itself is not Authenticode-signed; Windows trust is established by signing
the executable content and installer executable.

### Order matters

`snapback.exe` has to be signed before CPack runs, because CPack copies it into both the ZIP
and the installer. After packaging, the script extracts the ZIP it's about to upload and
requires the `snapback.exe` inside it to be:

1. `Get-AuthenticodeSignature` status `Valid`, and
2. signed by the **same thumbprint** that was passed in.

The second check is not redundant. A status check alone passes for anything validly signed by
anyone — a stray Microsoft-signed binary picked up by the glob would satisfy it. Both failure
modes were exercised against the real script: an unsigned binary in the ZIP is rejected as
`NotSigned`, and a validly-signed binary from a different certificate is rejected on the
thumbprint.

**Still unproven:** the success path has never run, because it needs a real certificate. Until
a signed build has been produced and verified, README describes Windows signing as wired but
incomplete.

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

## Auto-update

Not in v1. Upgrades are a manual download from GitHub Releases. Even an update check is a
network call, which the app doesn't make ([ADR-0009](adr/0009-local-first-threat-model.md)),
and a real updater needs signed manifests, channels and rollback.

## macOS / Linux packaging

macOS `.app`/DMG packaging and notarization are the main thing left for v1. They need an Apple Developer account, bundle identifier, and
signing/notarization credentials. Linux packaging comes after v1.
