param(
    [string]$PackageZip = "build-windows-package\Snapback-0.2.0-win64.zip",
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ZipPath = if ([System.IO.Path]::IsPathRooted($PackageZip)) {
    $PackageZip
} else {
    Join-Path $RepoRoot $PackageZip
}

if (-not (Test-Path $ZipPath)) {
    throw "Package zip not found: $ZipPath"
}

$ExtractDir = Join-Path $RepoRoot ".demo\package-validate"
Remove-Item -LiteralPath $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null

Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExtractDir -Force
$PackageRoot = Get-ChildItem -LiteralPath $ExtractDir -Directory | Select-Object -First 1
if (-not $PackageRoot) {
    throw "Package zip did not contain a root directory."
}

$Exe = Join-Path $PackageRoot.FullName "snapback.exe"
$FrontendIndex = Join-Path $PackageRoot.FullName "frontend\index.html"
if (-not (Test-Path $Exe)) {
    throw "Package missing snapback.exe."
}
if (-not (Test-Path $FrontendIndex)) {
    throw "Package missing frontend\index.html."
}

$env:SNAPBACK_DATA_DIR = Join-Path $RepoRoot ".demo\package-validate-data"
Remove-Item Env:\SNAPBACK_FRONTEND_URL -ErrorAction SilentlyContinue

$Process = Start-Process -FilePath $Exe -WorkingDirectory $PackageRoot.FullName -PassThru
try {
    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Milliseconds 500
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Packaged snapback.exe exited early with code $($Process.ExitCode)."
        }
        if ($Process.MainWindowTitle -like "*Snapback*") {
            Write-Host "Package validation passed: packaged app launched window '$($Process.MainWindowTitle)'."
            return
        }
    }
    throw "Packaged snapback.exe did not expose a Snapback window within $TimeoutSeconds seconds."
} finally {
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
    }
}
