param(
    [string]$BuildDir = "build-windows-package",
    [string]$Config = "Release",
    [switch]$SkipNpmInstall,
    [switch]$SkipInstaller,
    [string]$SignCertificate = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$FrontendDir = Join-Path $RepoRoot "frontend"
$BuildPath = Join-Path $RepoRoot $BuildDir

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required but was not found on PATH."
    }
}

# $ErrorActionPreference = "Stop" only makes *cmdlets* terminate; native executables
# (npm, cmake, ctest, cpack, ...) just set $LASTEXITCODE and the script sails past a
# failure. Route every external command through this so a red build can't be packaged.
function Invoke-Native {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Command"
    }
}

function Sign-ReleaseBinary {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Certificate
    )
    $signtool = Get-Command signtool -ErrorAction SilentlyContinue
    if (-not $signtool) {
        throw "signtool.exe is required when -SignCertificate is set."
    }
    & $signtool.Source sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
        /sha1 $Certificate $Path
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed (exit $LASTEXITCODE): $Path"
    }
    Write-Host "Signed $Path"
}

# Roadmap 0.4b. Signing the build-tree exe proves nothing about what we upload: the ZIP and
# the installer carry their own copies, taken at the moment they were packaged. This opens
# the artifact we actually ship and checks the signature on the binary inside it.
function Assert-PackagedBinarySigned {
    param(
        [Parameter(Mandatory = $true)][string]$Zip,
        [Parameter(Mandatory = $true)][string]$VerifyDir,
        [Parameter(Mandatory = $true)][string]$Certificate
    )
    Remove-Item -LiteralPath $VerifyDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $VerifyDir | Out-Null
    Expand-Archive -LiteralPath $Zip -DestinationPath $VerifyDir -Force

    $packaged = Get-ChildItem -LiteralPath $VerifyDir -Recurse -Filter "snapback.exe" |
        Select-Object -First 1
    if (-not $packaged) {
        throw "Signature verification failed: no snapback.exe inside $Zip."
    }
    Assert-BinarySigned -Path $packaged.FullName -Certificate $Certificate `
        -Label "packaged snapback.exe (from $(Split-Path -Leaf $Zip))"
}

function Assert-BinarySigned {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Certificate,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne "Valid") {
        # "NotSigned" here is the exact 0.4b bug: signing ran, but after the artifact was
        # built from an unsigned binary. Fail the package rather than upload it.
        throw "Signature verification failed for $Label - status '$($signature.Status)'. " +
              "The signed binary did not make it into the artifact."
    }

    # A valid signature is not enough: any Microsoft-signed stray binary picked up by the
    # glob would satisfy that. What must be proven is that *our* certificate signed the
    # copy inside the artifact, so compare thumbprints.
    $actual = $signature.SignerCertificate.Thumbprint
    if ($actual -ne $Certificate.Replace(" ", "").Trim()) {
        throw "Signature verification failed for $Label - signed by thumbprint '$actual', " +
              "expected '$Certificate'."
    }
    Write-Host "Verified signature on $Label ($($signature.SignerCertificate.Subject))"
}

Require-Command cmake
Require-Command ctest
Require-Command cpack
Require-Command npm

Push-Location $FrontendDir
try {
    if (-not (Test-Path "node_modules") -and -not $SkipNpmInstall) {
        Invoke-Native { npm ci }
    }
    Invoke-Native { npm run typecheck }
    Invoke-Native { npm run test }
    Invoke-Native { npm run build }
} finally {
    Pop-Location
}

# No -G: naming "Visual Studio 17 2022" pinned the release build to one runner image, and
# CMake fails outright ("could not find any instance of Visual Studio") once that image moves
# on — so a release would stop building for a reason that has nothing to do with the code.
# -A x64 stays: this script names its artifacts win64, so the architecture is asserted here
# rather than inherited from whatever the default generator happens to pick.
Invoke-Native { cmake -S $RepoRoot -B $BuildPath -A x64 -DSNAPBACK_BUILD_APP=ON -DSNAPBACK_ONNX=OFF }
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback_tests }
Invoke-Native { ctest --test-dir $BuildPath -C $Config --output-on-failure }
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback }

# Roadmap 0.4b. Sign here — after the build, BEFORE anything packages the result.
#
# This used to happen at the end of the script, which looked equivalent and was not: CPack had
# already copied the *unsigned* snapback.exe into both the ZIP and the installer. Only the
# loose build-tree binary and the outer installer ended up signed, so every uploaded artifact
# still carried an unsigned executable.
if ($SignCertificate) {
    $exeCandidates = @(
        (Join-Path $BuildPath "$Config\snapback.exe"),
        (Join-Path $BuildPath "snapback.exe")
    )
    $exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exe) {
        throw "snapback.exe was not found for signing under $BuildPath."
    }
    Sign-ReleaseBinary -Path $exe -Certificate $SignCertificate
}

Push-Location $BuildPath
try {
    Invoke-Native { cpack -G ZIP -C $Config }
    $zip = Get-ChildItem -LiteralPath $BuildPath -Filter "Snapback-*-win64.zip" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $zip) {
        throw "CPack ZIP output was not found under $BuildPath."
    }

    # The installer used to be an IExpress self-extractor wrapping this ZIP plus
    # install_windows_package.ps1. It never once built on a GitHub-hosted runner: iexpress.exe
    # exits 1 there and reports nothing else, for every SED it is handed — with a license page
    # or without, with one source directory or two. It is an undocumented IE-era GUI tool with
    # no diagnostics, so this uses CPack's NSIS generator instead. NSIS is a real installer
    # with a real uninstaller, CMakeLists.txt already configures it, and its failures are
    # readable.
    $installerExe = $null
    if (-not $SkipInstaller) {
        # A missing makensis is a hard failure, not a warning. The IExpress path only warned,
        # which is how a packaging step nothing had ever produced survived to a release tag.
        Require-Command makensis
        Invoke-Native { cpack -G NSIS -C $Config }

        # Both generators name their output from CPACK_PACKAGE_FILE_NAME, so the NSIS exe is
        # the ZIP's base name with a different extension.
        $nsisExe = Join-Path $BuildPath "$($zip.BaseName).exe"
        if (-not (Test-Path -LiteralPath $nsisExe)) {
            throw "CPack NSIS output was not found at $nsisExe."
        }
        # Rename to the -installer.exe name the release workflow's upload glob, the signing
        # block below, and docs/windows_demo.md all already use. Derived from the ZIP CPack
        # actually produced, so the version cannot drift from a hardcoded one when it bumps.
        $installerExe = Join-Path $BuildPath "$($zip.BaseName)-installer.exe"
        Move-Item -LiteralPath $nsisExe -Destination $installerExe -Force
        Write-Host "Unsigned NSIS installer generated: $installerExe"
    }

    if ($SignCertificate) {
        # snapback.exe was signed before CPack ran, above. The installer is signed here
        # because it does not exist until CPack has produced it — and by now it carries the
        # already-signed binary.
        #
        # $installerExe is $null when the installer was skipped; Test-Path $null throws
        # under ErrorActionPreference=Stop, so short-circuit on the null first.
        if ($installerExe -and (Test-Path $installerExe)) {
            Sign-ReleaseBinary -Path $installerExe -Certificate $SignCertificate
        }

        # Prove it on the artifacts that actually get uploaded, not on the build tree. This
        # is what would have caught the original ordering bug.
        Assert-PackagedBinarySigned -Zip $zip.FullName `
            -VerifyDir (Join-Path $BuildPath "signature-verify") `
            -Certificate $SignCertificate
        if ($installerExe -and (Test-Path $installerExe)) {
            Assert-BinarySigned -Path $installerExe -Certificate $SignCertificate `
                -Label "NSIS installer"
        }
    }
} finally {
    Pop-Location
}

Write-Host "Package output is under $BuildPath"
if (-not $SignCertificate) {
    Write-Host "Unsigned build. Pass -SignCertificate THUMBPRINT to Authenticode-sign release binaries."
}
