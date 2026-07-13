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
    Write-Host "Signed $Path"
}

Require-Command cmake
Require-Command ctest
Require-Command cpack
Require-Command npm

Push-Location $FrontendDir
try {
    if (-not (Test-Path "node_modules") -and -not $SkipNpmInstall) {
        npm ci
    }
    npm run typecheck
    npm run test
    npm run build
} finally {
    Pop-Location
}

cmake -S $RepoRoot -B $BuildPath -G "Visual Studio 17 2022" -A x64 -DSNAPBACK_BUILD_APP=ON -DSNAPBACK_ONNX=OFF
cmake --build $BuildPath --config $Config --target snapback_tests
ctest --test-dir $BuildPath -C $Config --output-on-failure
cmake --build $BuildPath --config $Config --target snapback

Push-Location $BuildPath
try {
    cpack -G ZIP -C $Config
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
            $installerExe = Join-Path $BuildPath "Snapback-0.2.0-win64-installer.exe"
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
            & $iexpress.Source /N /Q $sedPath
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
            cpack -G NSIS -C $Config
        } else {
            Write-Warning "NSIS/makensis was not found; skipped unsigned NSIS installer."
        }
    }

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
        if (Test-Path $installerExe) {
            Sign-ReleaseBinary -Path $installerExe -Certificate $SignCertificate
        }
    }
} finally {
    Pop-Location
}

Write-Host "Package output is under $BuildPath"
if (-not $SignCertificate) {
    Write-Host "Unsigned build. Pass -SignCertificate THUMBPRINT to Authenticode-sign release binaries."
}
