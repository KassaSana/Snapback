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

Require-Command cmake
Require-Command ctest

Write-Host "== C++ mock/headless tests =="
cmake -S $RepoRoot -B $BuildPath -DSNAPBACK_BUILD_APP=OFF -DSNAPBACK_ONNX=OFF
cmake --build $BuildPath --config $Config --target snapback_tests
ctest --test-dir $BuildPath -C $Config --output-on-failure

if (-not $SkipFrontend) {
    Require-Command npm
    Write-Host "== Frontend mock tests =="
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
}

if ($IncludeWindowsDemo) {
    Write-Host "== Windows desktop integration smoke =="
    powershell -ExecutionPolicy Bypass -File (Join-Path $ScriptDir "windows_demo.ps1") -NoLaunch
}

Write-Host "Local test suite completed."
