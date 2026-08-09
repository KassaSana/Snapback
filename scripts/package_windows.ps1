param(
    [string]$BuildDir = "build-windows-package",
    [string]$Config = "Release",
    [switch]$SkipNpmInstall,
    [switch]$SkipIExpress,
    [switch]$TryNsis,
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
# This used to happen at the end of the script, which looked equivalent and was not: CPack
# and IExpress had already copied the *unsigned* snapback.exe into the ZIP and embedded that
# ZIP in the installer. Only the loose build-tree binary and the outer installer ended up
# signed, so every uploaded artifact still carried an unsigned executable.
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

    $installerExe = $null
    if (-not $SkipIExpress) {
        $iexpress = Get-Command iexpress -ErrorAction SilentlyContinue
        if ($iexpress) {
            $installerScript = Join-Path $RepoRoot "scripts\install_windows_package.ps1"
            # Derive the installer name from the ZIP CPack actually produced so the version
            # can never drift from the hardcoded one when it bumps (e.g. "Snapback-0.2.0-win64").
            $installerExe = Join-Path $BuildPath "$($zip.BaseName)-installer.exe"
            $sedPath = Join-Path $BuildPath "snapback-installer.sed"
            $zipName = Split-Path -Leaf $zip.FullName
            $installerScriptName = Split-Path -Leaf $installerScript
            $buildPathEscaped = $BuildPath.TrimEnd('\')
            $scriptDirEscaped = (Split-Path -Parent $installerScript).TrimEnd('\')
            @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=<None>
AdminQuietInstCmd=%AppLaunched%
UserQuietInstCmd=%AppLaunched%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=Snapback installation completed.
TargetName=$installerExe
FriendlyName=Snapback Installer
AppLaunched=powershell.exe -ExecutionPolicy Bypass -NoProfile -File $installerScriptName -PackageZip $zipName
FILE0=$zipName
FILE1=$installerScriptName
[SourceFiles]
SourceFiles0=$buildPathEscaped
SourceFiles1=$scriptDirEscaped
[SourceFiles0]
%FILE0%=
[SourceFiles1]
%FILE1%=
"@ | Set-Content -LiteralPath $sedPath -Encoding ASCII
            Invoke-Native { & $iexpress.Source /N /Q $sedPath }
            if (-not (Test-Path $installerExe)) {
                throw "IExpress did not produce $installerExe"
            }
            Write-Host "Unsigned IExpress installer generated: $installerExe"
        } else {
            Write-Warning "IExpress was not found; skipped unsigned self-extracting installer."
        }
    }

    if ($TryNsis) {
        if (Get-Command makensis -ErrorAction SilentlyContinue) {
            Invoke-Native { cpack -G NSIS -C $Config }
        } else {
            Write-Warning "NSIS/makensis was not found; skipped unsigned NSIS installer."
        }
    }

    if ($SignCertificate) {
        # snapback.exe was signed before CPack ran, above. The installer is signed here
        # because it does not exist until IExpress has produced it — and by now it contains
        # the ZIP built from the already-signed binary.
        #
        # $installerExe is $null when IExpress was skipped/absent; Test-Path $null throws
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
                -Label "IExpress installer"
        }
    }
} finally {
    Pop-Location
}

Write-Host "Package output is under $BuildPath"
if (-not $SignCertificate) {
    Write-Host "Unsigned build. Pass -SignCertificate THUMBPRINT to Authenticode-sign release binaries."
}
