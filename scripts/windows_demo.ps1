param(
    [string]$BuildDir = "build-windows-demo",
    [string]$FrontendUrl = "http://127.0.0.1:5173",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [switch]$SkipFrontend,
    [switch]$SkipNpmInstall,
    [switch]$UseVite,
    [switch]$OverlayTest,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$FrontendDir = Join-Path $RepoRoot "frontend"
$BuildPath = Join-Path $RepoRoot $BuildDir
$LogDir = Join-Path $RepoRoot ".demo"
$ViteLog = Join-Path $LogDir "vite.log"
$ViteErrorLog = Join-Path $LogDir "vite.err.log"
$DemoDataDir = Join-Path $LogDir "data"

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required for the Windows demo but was not found on PATH."
    }
}

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
    throw "snapback.exe was not found under $BuildPath after the build."
}

function Resolve-CommandSource {
    param([string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    return $null
}

Require-Command cmake
Require-Command ctest

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $DemoDataDir | Out-Null

$ViteProcess = $null
$NodeProcessIdsBefore = @()
if (-not $SkipFrontend) {
    Require-Command npm
    $NpmCommand = Resolve-CommandSource "npm.cmd"
    if (-not $NpmCommand) {
        $NpmCommand = Resolve-CommandSource "npm"
    }
    if (-not $NpmCommand) {
        throw "npm is required for the Windows demo but was not found on PATH."
    }
    $NodeProcessIdsBefore = @(
        Get-Process node, npm -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id
    )
    if (-not (Test-Path (Join-Path $FrontendDir "node_modules")) -and -not $SkipNpmInstall) {
        Push-Location $FrontendDir
        try {
            npm ci
        } finally {
            Pop-Location
        }
    }

    Push-Location $FrontendDir
    try {
        npm run typecheck
        npm run build
    } finally {
        Pop-Location
    }

    if ($UseVite) {
        $port = ([Uri]$FrontendUrl).Port
        $viteArgs = @("run", "dev", "--", "--host", "127.0.0.1", "--port", "$port", "--strictPort")
        $ViteProcess = Start-Process -FilePath $NpmCommand `
            -ArgumentList $viteArgs `
            -WorkingDirectory $FrontendDir `
            -RedirectStandardOutput $ViteLog `
            -RedirectStandardError $ViteErrorLog `
            -WindowStyle Hidden `
            -PassThru
        $frontendReady = $false
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            try {
                $response = Invoke-WebRequest -Uri $FrontendUrl -UseBasicParsing -TimeoutSec 2
                if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 500) {
                    $frontendReady = $true
                    break
                }
            } catch {
                Start-Sleep -Milliseconds 500
            }
        }
        if (-not $frontendReady) {
            throw "Frontend did not respond at $FrontendUrl. See $ViteLog and $ViteErrorLog."
        }
    }
}

$configureArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildPath,
    "-G", $Generator,
    "-A", $Arch,
    "-DSNAPBACK_BUILD_APP=ON",
    "-DSNAPBACK_ONNX=OFF"
)
cmake @configureArgs
cmake --build $BuildPath --config Release --target snapback_tests
ctest --test-dir $BuildPath -C Release --output-on-failure
cmake --build $BuildPath --config Release --target snapback

if ($NoLaunch) {
    if ($null -ne $ViteProcess -and -not $ViteProcess.HasExited) {
        Stop-Process -Id $ViteProcess.Id -Force
    }
    $newNodeProcesses = Get-Process node, npm -ErrorAction SilentlyContinue |
        Where-Object { $NodeProcessIdsBefore -notcontains $_.Id }
    foreach ($process in $newNodeProcesses) {
        Stop-Process -Id $process.Id -Force
    }
    Write-Host "Windows demo build is ready at $BuildPath"
    exit 0
}

$exe = Find-SnapbackExe
$env:SNAPBACK_DATA_DIR = $DemoDataDir
if ($UseVite) {
    $env:SNAPBACK_FRONTEND_URL = $FrontendUrl
} else {
    Remove-Item Env:\SNAPBACK_FRONTEND_URL -ErrorAction SilentlyContinue
}
if ($OverlayTest) {
    $env:SNAPBACK_OVERLAY_TEST = "1"
} else {
    Remove-Item Env:\SNAPBACK_OVERLAY_TEST -ErrorAction SilentlyContinue
}

Write-Host "Launching Snapback demo:"
Write-Host "  App:      $exe"
Write-Host "  Frontend: $(if ($UseVite) { $FrontendUrl } else { 'bundled frontend/dist' })"
Write-Host "  Data:     $DemoDataDir"
if ($UseVite) {
    Write-Host "  Vite log: $ViteLog"
}
Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
