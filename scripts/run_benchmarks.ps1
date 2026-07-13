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

Require-Command cmake

Write-Host "== Configure benchmarks =="
cmake -S $RepoRoot -B $BuildPath -DSNAPBACK_BUILD_APP=OFF -DSNAPBACK_ONNX=OFF -DSNAPBACK_BUILD_BENCHMARKS=ON

Write-Host "== Build benchmarks =="
cmake --build $BuildPath --config $Config --target snapback_benchmarks

Write-Host "== Run benchmarks =="
$Exe = Join-Path $BuildPath "$Config\snapback_benchmarks.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $BuildPath "snapback_benchmarks"
}
if (-not (Test-Path $Exe)) {
    throw "Benchmark executable not found under $BuildPath."
}

$env:SNAPBACK_BENCH_MINUTES = "$Minutes"
& $Exe
