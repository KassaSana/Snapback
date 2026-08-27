param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$SkipFrontend,
    [switch]$SkipNpmInstall,
    [switch]$IncludeWindowsDemo,
    [switch]$IncludeBenchmarkSmoke
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
Invoke-Native { cmake -S $RepoRoot -B $BuildPath -DSNAPBACK_BUILD_APP=OFF -DSNAPBACK_ONNX=OFF -DSNAPBACK_BUILD_BENCHMARKS=ON }
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback_tests }
Invoke-Native { ctest --test-dir $BuildPath -C $Config --output-on-failure }

# Compiled every run, deliberately, even though the smoke run below is opt-in.
#
# benchmarks/ drives the same private seams the tests do, so a type change in src/ breaks it
# the same way -- but SNAPBACK_BUILD_BENCHMARKS defaults OFF, so nothing here used to compile
# these files and CI's benchmark job was the first thing that did. ADR-0007's timestamp_ms
# rename landed on a green local run and turned master red for exactly that reason. Building
# is seconds; it is the running that costs minutes.
Write-Host "== Benchmark build (compile-only unless -IncludeBenchmarkSmoke) =="
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback_benchmarks snapback_hotpath_benchmarks }

if ($IncludeBenchmarkSmoke) {
    Write-Host "== Benchmark smoke =="
    # Multi-config generators (Visual Studio) nest the binary under a per-config directory;
    # single-config ones (Ninja, Makefiles) put it straight in the build root.
    $BenchExe = Join-Path $BuildPath (Join-Path $Config "snapback_benchmarks.exe")
    if (-not (Test-Path $BenchExe)) {
        $BenchExe = Join-Path $BuildPath "snapback_benchmarks"
    }
    if (-not (Test-Path $BenchExe)) {
        throw "benchmark binary not found under $BuildPath"
    }
    # One simulated minute: enough to exercise every replay path, short enough to sit through.
    $env:SNAPBACK_BENCH_MINUTES = "1"
    try {
        Invoke-Native { & $BenchExe }
    } finally {
        Remove-Item Env:\SNAPBACK_BENCH_MINUTES -ErrorAction SilentlyContinue
    }
}

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
