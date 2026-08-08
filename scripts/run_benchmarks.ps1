param(
    [string]$BuildDir = "build-benchmarks",
    [string]$Config = "Release",
    [int]$Minutes = 180
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$BuildPath = Join-Path $RepoRoot $BuildDir

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required but was not found on PATH."
    }
}

# $ErrorActionPreference = "Stop" only makes *cmdlets* terminate; native executables just set
# $LASTEXITCODE and the script sails past a failure — which here would publish benchmark
# numbers produced by a stale binary. Same helper as scripts/package_windows.ps1;
# scripts/check_ps_exit_codes.py enforces its use.
function Invoke-Native {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Command"
    }
}

Require-Command cmake

Write-Host "== Configure benchmarks =="
Invoke-Native {
    cmake -S $RepoRoot -B $BuildPath -DSNAPBACK_BUILD_APP=OFF -DSNAPBACK_ONNX=OFF `
        -DSNAPBACK_BUILD_BENCHMARKS=ON
}

Write-Host "== Build benchmarks =="
Invoke-Native { cmake --build $BuildPath --config $Config --target snapback_benchmarks }

Write-Host "== Run benchmarks =="
$Exe = Join-Path $BuildPath "$Config\snapback_benchmarks.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $BuildPath "snapback_benchmarks"
}
if (-not (Test-Path $Exe)) {
    throw "Benchmark executable not found under $BuildPath."
}

$env:SNAPBACK_BENCH_MINUTES = "$Minutes"
Invoke-Native { & $Exe }
