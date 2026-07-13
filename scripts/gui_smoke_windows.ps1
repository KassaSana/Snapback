param(
    [string]$BuildDir = "build-windows-demo",
    [string]$Config = "Release",
    [int]$TimeoutSeconds = 20,
    [switch]$NoBuild,
    [switch]$OverlayTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$BuildPath = Join-Path $RepoRoot $BuildDir
$DemoDataDir = Join-Path $RepoRoot ".demo\gui-smoke-data"

function Find-SnapbackExe {
    $candidates = @(
        (Join-Path $BuildPath "Release\snapback.exe"),
        (Join-Path $BuildPath "snapback.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "snapback.exe was not found under $BuildPath."
}

if (-not $NoBuild) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $ScriptDir "windows_demo.ps1") -NoLaunch
}

New-Item -ItemType Directory -Force -Path $DemoDataDir | Out-Null

$exe = Find-SnapbackExe
$env:SNAPBACK_DATA_DIR = $DemoDataDir
Remove-Item Env:\SNAPBACK_FRONTEND_URL -ErrorAction SilentlyContinue
if ($OverlayTest) {
    $env:SNAPBACK_OVERLAY_TEST = "1"
    Remove-Item Env:\SNAPBACK_GUI_SESSION_SMOKE -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:\SNAPBACK_OVERLAY_TEST -ErrorAction SilentlyContinue
    $env:SNAPBACK_GUI_SESSION_SMOKE = "1"
}

$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $windowReady = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) {
            throw "snapback.exe exited early with code $($process.ExitCode)."
        }
        if ($process.MainWindowTitle -like "*Snapback*") {
            $windowReady = $true
            break
        }
    }

    if (-not $windowReady) {
        throw "snapback.exe did not expose a Snapback main window within $TimeoutSeconds seconds."
    }

    if ($env:SNAPBACK_GUI_SESSION_SMOKE) {
        $marker = Join-Path $DemoDataDir "gui_session_smoke.ok"
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            if (Test-Path $marker) { break }
            Start-Sleep -Milliseconds 200
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        if (-not (Test-Path $marker)) {
            throw "GUI session smoke marker was not written to $marker."
        }
        $sessionId = Get-Content -Raw $marker
        if ([string]::IsNullOrWhiteSpace($sessionId)) {
            throw "GUI session smoke marker was empty."
        }
        Write-Host "GUI session smoke passed: start/stop wrote marker for session $sessionId."
    }

    Write-Host "GUI smoke passed: snapback.exe launched and exposed window '$($process.MainWindowTitle)'."
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}
