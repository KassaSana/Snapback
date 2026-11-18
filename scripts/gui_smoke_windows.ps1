param(
    [string]$BuildDir = "build-windows-demo",
    [string]$Config = "Release",
    [int]$TimeoutSeconds = 20,
    [switch]$NoBuild,
    [switch]$Acceptance,
    [switch]$Driven,
    [switch]$OverlayTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$BuildPath = Join-Path $RepoRoot $BuildDir
$DemoDataDir = Join-Path $RepoRoot ".demo\gui-smoke-data"

# $ErrorActionPreference = "Stop" only makes *cmdlets* terminate; native executables just set
# $LASTEXITCODE and the script sails past a failure. Unchecked, a failed demo build would fall
# through to Find-SnapbackExe and smoke-test whatever stale snapback.exe was already there.
# Same helper as scripts/package_windows.ps1; scripts/check_ps_exit_codes.py enforces its use.
function Invoke-Native {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Command"
    }
}

# When the CDP driver can't attach, say why: which runtime ran, whether the browser process got
# the debugging flags, and whether it ever opened a port.
function Write-WebView2Diagnostics {
    param([string]$UserDataFolder, [int]$Port)
    $clientKey = "SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    foreach ($hive in @("HKLM:", "HKCU:")) {
        $item = Get-ItemProperty -Path "$hive\$clientKey" -ErrorAction SilentlyContinue
        if ($item -and $item.PSObject.Properties["pv"]) { Write-Host "WebView2 runtime ($hive): $($item.pv)" }
    }
    Write-Host "Expected CDP port: $Port; user data folder: $UserDataFolder"
    $portFile = Join-Path $UserDataFolder "EBWebView\DevToolsActivePort"
    if (Test-Path -LiteralPath $portFile) {
        Write-Host "DevToolsActivePort: $((Get-Content -Raw -LiteralPath $portFile).Trim() -replace '\s+', ' ')"
    } else {
        Write-Host "DevToolsActivePort: missing (folder exists: $(Test-Path -LiteralPath $UserDataFolder))"
    }
    Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -notmatch '--type=' -and $_.CommandLine -match 'webview-exe-name=snapback' } |
        ForEach-Object { Write-Host "browser process $($_.ProcessId): $($_.CommandLine)" }
}

function Find-SnapbackExe {
    $candidates = @(
        (Join-Path $BuildPath "$Config\snapback.exe"),
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

$modeCount = [int]$Acceptance.IsPresent + [int]$Driven.IsPresent + [int]$OverlayTest.IsPresent
if ($modeCount -gt 1) {
    throw "Choose only one of -Acceptance, -Driven, or -OverlayTest."
}

if (-not $NoBuild) {
    $DemoScript = Join-Path $ScriptDir "windows_demo.ps1"
    $demoArgs = @("-ExecutionPolicy", "Bypass", "-File", $DemoScript, "-NoLaunch")
    if ($Acceptance -or $Driven) { $demoArgs += "-EnableAcceptanceHarness" }
    Invoke-Native { powershell @demoArgs }
}

New-Item -ItemType Directory -Force -Path $DemoDataDir | Out-Null

$exe = Find-SnapbackExe
$env:SNAPBACK_DATA_DIR = $DemoDataDir
Remove-Item Env:\SNAPBACK_FRONTEND_URL -ErrorAction SilentlyContinue
$sessionMarker = Join-Path $DemoDataDir "gui_session_smoke.ok"
$acceptanceVerdict = Join-Path $DemoDataDir "acceptance-verdict.json"
$previousWebViewArguments = [Environment]::GetEnvironmentVariable("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")
$previousWebViewUserDataFolder = [Environment]::GetEnvironmentVariable("WEBVIEW2_USER_DATA_FOLDER")
Remove-Item -LiteralPath $sessionMarker, $acceptanceVerdict -Force -ErrorAction SilentlyContinue
Remove-Item Env:\SNAPBACK_WEBVIEW2_ARGS -ErrorAction SilentlyContinue
if ($OverlayTest) {
    $env:SNAPBACK_OVERLAY_TEST = "1"
    Remove-Item Env:\SNAPBACK_GUI_SESSION_SMOKE -ErrorAction SilentlyContinue
    Remove-Item Env:\SNAPBACK_ACCEPTANCE_SCRIPT -ErrorAction SilentlyContinue
    Remove-Item Env:\WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
} elseif ($Acceptance) {
    Remove-Item Env:\SNAPBACK_OVERLAY_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:\SNAPBACK_GUI_SESSION_SMOKE -ErrorAction SilentlyContinue
    $env:SNAPBACK_ACCEPTANCE_SCRIPT = Join-Path $ScriptDir "gui_acceptance.js"
    Remove-Item Env:\WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
} elseif ($Driven) {
    Remove-Item Env:\SNAPBACK_OVERLAY_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:\SNAPBACK_GUI_SESSION_SMOKE -ErrorAction SilentlyContinue
    $env:SNAPBACK_ACCEPTANCE_SCRIPT = Join-Path $ScriptDir "gui_acceptance_cdp_bootstrap.js"
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $cdpPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    # WebView2 may retain the browser process used by the preceding -Acceptance smoke. Browser
    # arguments are fixed when that shared process starts, so it would not begin listening on
    # this run's CDP port. A unique temporary profile gives the driven smoke its own process.
    $env:WEBVIEW2_USER_DATA_FOLDER = Join-Path $DemoDataDir "webview2-cdp-$cdpPort"
    $cdpArguments = "--remote-debugging-address=127.0.0.1 --remote-debugging-port=$cdpPort"
    $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = $cdpArguments
    # Newer runtimes ignore the variable above; smoke builds also pass this through WebView2's
    # environment options (see cmake/patch_webview.cmake).
    $env:SNAPBACK_WEBVIEW2_ARGS = $cdpArguments
} else {
    Remove-Item Env:\SNAPBACK_OVERLAY_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:\SNAPBACK_ACCEPTANCE_SCRIPT -ErrorAction SilentlyContinue
    $env:SNAPBACK_GUI_SESSION_SMOKE = "1"
    Remove-Item Env:\WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
}

$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $windowReady = $false
    $observedWindowTitle = ""
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) {
            throw "snapback.exe exited early with code $($process.ExitCode)."
        }
        if ($process.MainWindowTitle -like "*Snapback*") {
            $windowReady = $true
            $observedWindowTitle = $process.MainWindowTitle
            break
        }
    }

    if (-not $windowReady) {
        throw "snapback.exe did not expose a Snapback main window within $TimeoutSeconds seconds."
    }

    if ($Driven) {
        $driver = Join-Path $ScriptDir "gui_acceptance_cdp.mjs"
        try {
            Invoke-Native { node $driver --port $cdpPort --timeout-ms ($TimeoutSeconds * 1000) }
        } catch {
            Write-WebView2Diagnostics -UserDataFolder $env:WEBVIEW2_USER_DATA_FOLDER -Port $cdpPort
            throw
        }
    }

    if ($Acceptance -or $Driven) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            if (Test-Path -LiteralPath $acceptanceVerdict) { break }
            Start-Sleep -Milliseconds 200
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        if (-not (Test-Path -LiteralPath $acceptanceVerdict)) {
            throw "Desktop acceptance verdict was not written to $acceptanceVerdict."
        }
        $verdict = Get-Content -Raw -LiteralPath $acceptanceVerdict | ConvertFrom-Json
        if (-not $verdict.passed) {
            throw "Desktop acceptance reported a failed check: $($verdict | ConvertTo-Json -Depth 6 -Compress)"
        }
        if ($verdict.checks.Count -ne 5 -or @($verdict.checks | Where-Object { -not $_.passed }).Count -ne 0) {
            throw "Desktop acceptance verdict did not contain five passing checks."
        }

        $shutdownDeadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $shutdownDeadline -and -not $process.HasExited) {
            Start-Sleep -Milliseconds 200
            $process.Refresh()
        }
        if (-not $process.HasExited) {
            throw "snapback.exe wrote the acceptance verdict but did not exit within 15 seconds."
        }
        if ($process.ExitCode -ne 0) {
            throw "snapback.exe exited with code $($process.ExitCode) after desktop acceptance."
        }
        if ($Driven) {
            if ($verdict.driver -ne "webview2-cdp") {
                throw "Driven desktop acceptance verdict did not identify the WebView2 CDP driver."
            }
            Write-Host "Driven acceptance passed: WebView2 CDP -> real UI clicks -> native handlers."
        } else {
            Write-Host "Desktop acceptance passed: page -> shim -> webview.bind -> native handlers."
        }
    } elseif ($env:SNAPBACK_GUI_SESSION_SMOKE) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            if (Test-Path $sessionMarker) { break }
            Start-Sleep -Milliseconds 200
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        if (-not (Test-Path $sessionMarker)) {
            throw "GUI session smoke marker was not written to $sessionMarker."
        }
        $sessionId = Get-Content -Raw $sessionMarker
        if ([string]::IsNullOrWhiteSpace($sessionId)) {
            throw "GUI session smoke marker was empty."
        }
        Write-Host "GUI session smoke passed: start/stop wrote marker for session $sessionId."
    }

    Write-Host "GUI smoke passed: snapback.exe launched and exposed window '$observedWindowTitle'."
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    Remove-Item Env:\SNAPBACK_WEBVIEW2_ARGS -ErrorAction SilentlyContinue
    if ($null -eq $previousWebViewArguments) {
        Remove-Item Env:\WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
    } else {
        $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = $previousWebViewArguments
    }
    if ($null -eq $previousWebViewUserDataFolder) {
        Remove-Item Env:\WEBVIEW2_USER_DATA_FOLDER -ErrorAction SilentlyContinue
    } else {
        $env:WEBVIEW2_USER_DATA_FOLDER = $previousWebViewUserDataFolder
    }
}
