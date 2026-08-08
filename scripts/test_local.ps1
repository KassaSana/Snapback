param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$SkipFrontend,
    [switch]$SkipNpmInstall,
    [switch]$IncludeWindowsDemo
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

# $ErrorActionPreference = "Stop" only makes *cmdlets* terminate; native executables just set
# $LASTEXITCODE and the script sails past a failure — so "Local test suite completed." could
# print over a build that never compiled. Same helper as scripts/package_windows.ps1;
# scripts/check_ps_exit_codes.py enforces its use.
function Invoke-Native {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Command"
    }
}

Require-Command cmake
Require-Command ctest

Write-Host "== C++ mock/headless tests =="
Invoke-Native { cmake -S $RepoRoot -B $BuildPath -DSNAPBACK_BUILD_APP=OFF -DSNAPBACK_ONNX=OFF }
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback_tests }
Invoke-Native { ctest --test-dir $BuildPath -C $Config --output-on-failure }

if (-not $SkipFrontend) {
    Require-Command npm
    Write-Host "== Frontend mock tests =="
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
}

if ($IncludeWindowsDemo) {
    Write-Host "== Windows desktop integration smoke =="
    $DemoScript = Join-Path $ScriptDir "windows_demo.ps1"
    Invoke-Native { powershell -ExecutionPolicy Bypass -File $DemoScript -NoLaunch }
}

Write-Host "Local test suite completed."
