param(
    # Empty means "find the one Snapback ZIP sitting beside this script". This used to
    # default to a literal "Snapback-0.2.0-win64.zip", which is the project version written
    # down a third time -- CMakeLists.txt declares it (9.2 says that is the single source),
    # frontend/package.json repeats it, and this repeated it again where nothing would
    # notice, because the failure is a confusing "not found" at install time rather than a
    # build error. package_windows.ps1 already derives the name from the ZIP CPack actually
    # produced; this now does the same thing for a standalone run.
    [string]$PackageZip = "",
    [string]$InstallDir = "",
    [switch]$Launch,
    [switch]$NoShortcuts
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($PackageZip)) {
    $found = @(Get-ChildItem -LiteralPath $ScriptDir -Filter "Snapback-*-win64.zip" -File |
        Sort-Object Name)
    if ($found.Count -eq 0) {
        throw ("No Snapback-*-win64.zip found in $ScriptDir. Build one with " +
               "scripts\package_windows.ps1, or pass -PackageZip <path>.")
    }
    if ($found.Count -gt 1) {
        $names = ($found | ForEach-Object { $_.Name }) -join ", "
        throw ("Found more than one package ZIP in ${ScriptDir}: $names. " +
               "Pass -PackageZip <path> to say which one.")
    }
    $ZipPath = $found[0].FullName
} elseif ([System.IO.Path]::IsPathRooted($PackageZip)) {
    $ZipPath = $PackageZip
} else {
    $ZipPath = Join-Path $ScriptDir $PackageZip
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
    # Install the package's *contents*, not the versioned folder around them. This used to
    # hand "<root>\*" to -LiteralPath, which is the one parameter that does not expand a
    # wildcard: it looked for a file literally named "*", found none, and copied nothing --
    # so every install failed on the snapback.exe check below.
    #
    # Switching to -Path would expand the asterisk but would also treat any [ ] in the
    # extracted path as a character class, and that path comes from $env:TEMP. Enumerating
    # and copying each entry literally expands nothing at all.
    Get-ChildItem -LiteralPath $packageRoot.FullName -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Recurse -Force
    }

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
