# Packaging and release decisions

## Current state

- **Unsigned Windows ZIP** via CPack (`scripts/package_windows.ps1`)
- **Optional IExpress self-extractor** when `iexpress` is available
- **Bundled frontend** copied beside `snapback.exe` for demo/release
- **CI:** headless tests on 3 OSes, ASan/UBSan, TSan, feature-parity fixtures, ONNX smoke, production-smoke workflow
- **Release:** a tag-driven workflow (`.github/workflows/release.yml`) builds + tests the
  Windows package and publishes it to GitHub Releases on a `v*` tag

Signing the installer is the next step (Roadmap Tier 0.4); see below.

## Authenticode signing (next step)

Release builds should be signed so Windows SmartScreen does not warn on first run.

### Requirements

1. A code-signing certificate (EV recommended for immediate SmartScreen trust)
2. `signtool.exe` from the Windows SDK on the packaging machine
3. `SignTool` timestamp server (e.g. `http://timestamp.digicert.com`)

### Usage

`scripts/package_windows.ps1` accepts an optional certificate:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 `
  -SignCertificate "Cert:\CurrentUser\My\THUMBPRINT_HERE"
```

When `-SignCertificate` is set, the script signs:

- `snapback.exe`
- the CPack ZIP installer (if Authenticode supports the container — exe is always signed)
- the IExpress installer exe when produced

### What we do not sign yet

- ONNX Runtime DLL (third-party; ship as-is)
- Frontend static assets (not executables)

## Auto-update (v1 decision)

**Decision: defer Tauri-style auto-update for v1.**

Rationale:

- The Rust app used Tauri's updater; replicating secure delta updates in C++ is a full subsystem (signing, channels, rollback).
- v1 ships as a signed installer + manual upgrade path.
- Revisit when installer signing and CI release artifacts are stable.

Alternatives if needed later:

1. **Manual download** — link from README / GitHub Releases (simplest)
2. **In-app "check for updates"** — HTTP fetch of a version manifest + download link (no silent install)
3. **Full updater** — port Tauri policy with signed manifests (high effort)

## macOS / Linux packaging

Deferred. Windows is the primary demo machine. macOS would need `.app` bundle + notarization; Linux would need AppImage or distro packages.
