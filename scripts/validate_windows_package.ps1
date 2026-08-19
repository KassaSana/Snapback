param(
    # Empty means "find the one ZIP under build-windows-package". See the note in
    # install_windows_package.ps1: hardcoding a version here writes the project version down
    # in a place nothing keeps in sync, and it fails as a puzzling "not found" rather than a
    # build error.
    [string]$PackageZip = "",
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($PackageZip)) {
    $PackageDir = Join-Path $RepoRoot "build-windows-package"
    $found = @()
    if (Test-Path -LiteralPath $PackageDir) {
        $found = @(Get-ChildItem -LiteralPath $PackageDir -Filter "Snapback-*-win64.zip" -File |
            Sort-Object Name)
    }
    if ($found.Count -eq 0) {
        throw ("No Snapback-*-win64.zip found in $PackageDir. Build one with " +
               "scripts\package_windows.ps1, or pass -PackageZip <path>.")
    }
    if ($found.Count -gt 1) {
        $names = ($found | ForEach-Object { $_.Name }) -join ", "
        throw ("Found more than one package ZIP in ${PackageDir}: $names. " +
               "Pass -PackageZip <path> to say which one.")
    }
    $ZipPath = $found[0].FullName
} elseif ([System.IO.Path]::IsPathRooted($PackageZip)) {
    $ZipPath = $PackageZip
} else {
    $ZipPath = Join-Path $RepoRoot $PackageZip
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
