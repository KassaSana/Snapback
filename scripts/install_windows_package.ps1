param(
    [string]$PackageZip = "Snapback-0.2.0-win64.zip",
    [string]$InstallDir = "",
    [switch]$Launch,
    [switch]$NoShortcuts
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ZipPath = if ([System.IO.Path]::IsPathRooted($PackageZip)) {
    $PackageZip
} else {
    Join-Path $ScriptDir $PackageZip
}

if (-not (Test-Path $ZipPath)) {
    throw "Package zip not found: $ZipPath"
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Snapback"
}

$TempExtract = Join-Path $env:TEMP ("snapback-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $TempExtract | Out-Null

try {
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $TempExtract -Force
    $packageRoot = Get-ChildItem -LiteralPath $TempExtract -Directory | Select-Object -First 1
    if (-not $packageRoot) {
        throw "Package zip did not contain a root directory."
    }

    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageRoot.FullName "*") -Destination $InstallDir -Recurse -Force

    $exe = Join-Path $InstallDir "snapback.exe"
    if (-not (Test-Path $exe)) {
        throw "Installed package is missing snapback.exe."
    }

    if (-not $NoShortcuts) {
        $shell = New-Object -ComObject WScript.Shell
        $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Snapback"
        New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null

        foreach ($shortcutPath in @(
            (Join-Path $startMenuDir "Snapback.lnk"),
            (Join-Path ([Environment]::GetFolderPath("Desktop")) "Snapback.lnk")
        )) {
            $shortcut = $shell.CreateShortcut($shortcutPath)
            $shortcut.TargetPath = $exe
            $shortcut.WorkingDirectory = $InstallDir
            $shortcut.IconLocation = $exe
            $shortcut.Save()
        }
    }

    Write-Host "Snapback installed to $InstallDir"
    if ($Launch) {
        Start-Process -FilePath $exe -WorkingDirectory $InstallDir
    }
} finally {
    Remove-Item -LiteralPath $TempExtract -Recurse -Force -ErrorAction SilentlyContinue
}
