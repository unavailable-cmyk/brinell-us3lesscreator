param(
    [ValidateSet('OpenGL', 'Zink')][string]$Backend = 'OpenGL',
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\GeometryDash.exe',
    [string]$MesaDirectory = (Join-Path $PSScriptRoot '..\dist\zink'),
    [string]$SettingsPath = (Join-Path $env:LOCALAPPDATA 'GeometryDash\geode\mods\us3lesscreator.vulkanizer\settings.json'),
    [string]$PresentMonPath = (Join-Path $PSScriptRoot '..\dist\tools\PresentMon-2.6.0-x64.exe'),
    [ValidateSet('auto', 'lazy', 'db')][string]$ZinkDescriptors = 'auto',
    [switch]$CaptureCpuProfile,
    [switch]$CapturePresentMon,
    [switch]$RestoreOnly
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ZinkSession.psm1') -Force
$gamePath = (Resolve-Path -LiteralPath $GameExe).Path
$gameRoot = Split-Path $gamePath -Parent
if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw 'Close Geometry Dash before starting or recovering a session.' }
if ($RestoreOnly) {
    Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $SettingsPath
    Write-Output 'Managed Zink files removed and original experimental settings restored.'
    return
}
if ($CapturePresentMon -and !(Test-Path -LiteralPath $PresentMonPath -PathType Leaf)) {
    throw "PresentMon CLI not found: $PresentMonPath"
}
if ($CaptureCpuProfile) {
    if (!(Get-Command wpr.exe -ErrorAction SilentlyContinue)) { throw 'WPR is unavailable; install the Windows Performance Toolkit.' }
    if (!(Get-Command xperf.exe -ErrorAction SilentlyContinue)) { throw 'Xperf is unavailable; install the Windows Performance Toolkit.' }
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'capture-cpu-profile.ps1') -PathType Leaf)) {
        throw 'The CPU profile helper script is missing.'
    }
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'VulkanizerCpu.wprp') -PathType Leaf)) {
        throw 'The focused CPU profile definition is missing.'
    }
}
if (Test-Path -LiteralPath (Join-Path $gameRoot '.vulkanizer-zink-session.json')) { throw 'Recover the previous session with -RestoreOnly first.' }
if (Test-Path -LiteralPath (Join-Path $gameRoot 'opengl32.dll')) { throw 'An existing local OpenGL provider was left untouched. Review it before running either mode.' }
$sessionId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$sessionRoot = Join-Path $PSScriptRoot "..\dist\sessions\$sessionId"
[IO.Directory]::CreateDirectory($sessionRoot) | Out-Null
$sessionRoot = (Resolve-Path -LiteralPath $sessionRoot).Path
$environmentKeys = @('GALLIUM_DRIVER', 'VULKANIZER_EVIDENCE_FILE', 'VULKANIZER_WGL_EVIDENCE_FILE', 'MESA_LOG_FILE', 'MESA_LOG_LEVEL', 'ZINK_DESCRIPTORS')
$oldEnvironment = @{}
foreach ($key in $environmentKeys) { $oldEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$installed = $false
$bundle = $null
$cpuTraceProcess = $null
$cpuProfilePidFile = Join-Path $sessionRoot 'cpu-profile.pid.json'
$cpuProfileCancelFile = Join-Path $sessionRoot 'cpu-profile.cancel'
$cpuProfileStatusFile = Join-Path $sessionRoot 'cpu-profile.status.json'
$cpuProfileReadyFile = Join-Path $sessionRoot 'cpu-profile.ready'
$cpuTracePath = Join-Path $sessionRoot 'cpu-profile.etl'
$cpuTraceHelperPath = Join-Path $PSScriptRoot 'capture-cpu-profile.ps1'
$cpuTraceInstance = 'VulkanizerCpu-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$presentMonProcess = $null
$presentMonStdout = Join-Path $sessionRoot 'presentmon.stdout.log'
$presentMonStderr = Join-Path $sessionRoot 'presentmon.stderr.log'
$presentMonSessionName = 'Vulkanizer-' + $sessionId
$presentMonExitCode = $null
$presentMonState = if ($CapturePresentMon) { 'starting' } else { 'not-requested' }
$gameStartUtc = $null
$gameEndUtc = $null
$gameExitCode = $null
try {
    if ($Backend -eq 'Zink') {
        $bundle = Get-Content -LiteralPath (Join-Path $MesaDirectory 'bundle.json') -Raw | ConvertFrom-Json
        Install-ZinkSession -GameDirectory $gameRoot -BundleDirectory $MesaDirectory -SettingsPath $SettingsPath | Out-Null
        $installed = $true
        Write-Output 'Zink session enabled experimental-renderer; preserved the Frame diagnostics preference.'
        $env:GALLIUM_DRIVER = 'zink'
        if ($bundle.instrumented) {
            $env:VULKANIZER_EVIDENCE_FILE = Join-Path $sessionRoot 'mesa-vulkan.jsonl'
            $env:VULKANIZER_WGL_EVIDENCE_FILE = Join-Path $sessionRoot 'mesa-wgl.jsonl'
        } else {
            $env:VULKANIZER_EVIDENCE_FILE = $null
            $env:VULKANIZER_WGL_EVIDENCE_FILE = $null
        }
        $env:MESA_LOG_FILE = Join-Path $sessionRoot 'mesa.log'
        $env:MESA_LOG_LEVEL = 'warn'
        $env:ZINK_DESCRIPTORS = $ZinkDescriptors
    } else {
        foreach ($key in $environmentKeys) { [Environment]::SetEnvironmentVariable($key, $null, 'Process') }
    }
    $instrumented = $Backend -eq 'Zink' -and [bool]$bundle.instrumented
    $descriptorMode = if ($Backend -eq 'Zink') { $ZinkDescriptors } else { 'not-applicable' }
    if ($CaptureCpuProfile) {
        $powershellPath = Join-Path $PSHOME 'powershell.exe'
        $helperArguments = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $cpuTraceHelperPath + '"'),
            '-SessionRoot', ('"' + $sessionRoot + '"'), '-InstanceName', $cpuTraceInstance
        )
        $cpuTraceProcess = Start-Process -FilePath $powershellPath -ArgumentList $helperArguments `
            -Verb RunAs -WindowStyle Hidden -PassThru
        $readyDeadline = [DateTime]::UtcNow.AddSeconds(90)
        while (!(Test-Path -LiteralPath $cpuProfileReadyFile) -and !$cpuTraceProcess.HasExited -and
               [DateTime]::UtcNow -lt $readyDeadline) {
            Start-Sleep -Milliseconds 250
        }
        if (!(Test-Path -LiteralPath $cpuProfileReadyFile)) {
            if (Test-Path -LiteralPath (Join-Path $sessionRoot 'cpu-profile-wpr.log')) {
                Get-Content -LiteralPath (Join-Path $sessionRoot 'cpu-profile-wpr.log') -Tail 8 | Write-Output
            }
            throw 'WPR CPU sampling did not become ready; Geometry Dash was not launched.'
        }
        Write-Output "WPR CPU stack sampling active; ETL target: $cpuTracePath"
    }
    Write-Output "Launching GD with $Backend (Zink descriptors=$descriptorMode; Mesa instrumentation=$instrumented). Session evidence: $sessionRoot"
    # The user runs this interactive launcher; GD is intentionally visible.
    $gameProcess = Start-Process -FilePath $gamePath -WorkingDirectory $gameRoot -PassThru
    $gameStartUtc = [DateTime]::UtcNow
    @{ state = 'running'; backend_requested = $Backend; pid = $gameProcess.Id; game = $gamePath;
       zink_descriptors = $descriptorMode; mesa_instrumented = $instrumented;
       presentmon_capture = [bool]$CapturePresentMon; cpu_profile_capture = [bool]$CaptureCpuProfile;
       started_utc = $gameStartUtc.ToString('o'); ended_utc = $null; elapsed_seconds = $null;
       game_exit_code = $null; presentmon_state = $presentMonState; presentmon_exit_code = $null;
       presentmon_session_name = $presentMonSessionName } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $sessionRoot 'session.json') -Encoding UTF8
    if ($CaptureCpuProfile) {
        @{ pid = $gameProcess.Id; start_ticks = $gameProcess.StartTime.ToUniversalTime().Ticks } |
            ConvertTo-Json | Set-Content -LiteralPath $cpuProfilePidFile -Encoding UTF8
    }
    if ($CapturePresentMon) {
        $presentMonCsv = Join-Path $sessionRoot 'presentmon.csv'
        $presentMonArguments = @(
            '--process_id', [string]$gameProcess.Id,
            '--output_file', ('"' + $presentMonCsv + '"'),
            '--session_name', $presentMonSessionName,
            '--date_time', '--v2_metrics', '--terminate_on_proc_exit',
            '--no_console_stats'
        )
        # Zink launches already run this child elevated. Capture both streams so
        # ETW permission/CLI failures have a concrete explanation in the session.
        try {
            $presentMonProcess = Start-Process -FilePath $PresentMonPath -ArgumentList $presentMonArguments `
                -WindowStyle Hidden -PassThru -RedirectStandardOutput $presentMonStdout -RedirectStandardError $presentMonStderr
            $presentMonState = 'running'
            Write-Output "PresentMon capture started for GD PID $($gameProcess.Id): $presentMonCsv"
        } catch {
            $presentMonState = 'failed-to-start'
            Write-Warning "PresentMon could not start; Geometry Dash will continue. Capture error: $($_.Exception.Message)"
        }
    }
    $gameProcess.WaitForExit()
    $gameEndUtc = [DateTime]::UtcNow
    $gameExitCode = $gameProcess.ExitCode
    Write-Output "GD exited with code $gameExitCode."
    if ($CaptureCpuProfile) {
        if ($cpuTraceProcess -and !$cpuTraceProcess.WaitForExit(60000)) {
            Write-Warning "WPR helper PID $($cpuTraceProcess.Id) has not finished; the CPU trace may need recovery."
        }
        if (Test-Path -LiteralPath $cpuProfileStatusFile) {
            $cpuStatus = Get-Content -LiteralPath $cpuProfileStatusFile -Raw | ConvertFrom-Json
            if ($cpuStatus.state -eq 'complete' -and (Test-Path -LiteralPath $cpuTracePath)) {
                $xperf = (Get-Command xperf.exe -ErrorAction Stop).Source
                $cpuReport = Join-Path $sessionRoot 'cpu-profile-summary.txt'
                $xperfLog = Join-Path $sessionRoot 'cpu-profile-xperf.log'
                $xperfStdout = Join-Path $sessionRoot 'cpu-profile-xperf.stdout.log'
                $xperfStderr = Join-Path $sessionRoot 'cpu-profile-xperf.stderr.log'
                Remove-Item -LiteralPath $cpuReport, $xperfStdout, $xperfStderr -Force -ErrorAction SilentlyContinue
                $xperfProcess = Start-Process -FilePath $xperf `
                    -ArgumentList @('-i', ('"' + $cpuTracePath + '"'), '-o', ('"' + $cpuReport + '"'), '-quiet', '-a', 'profile', '-detail') `
                    -WindowStyle Hidden -PassThru -Wait `
                    -RedirectStandardOutput $xperfStdout -RedirectStandardError $xperfStderr
                $xperfCode = $xperfProcess.ExitCode
                $reportBytes = if (Test-Path -LiteralPath $cpuReport) { (Get-Item -LiteralPath $cpuReport).Length } else { 0 }
                $xperfWarnings = @()
                foreach ($streamPath in @($xperfStdout, $xperfStderr)) {
                    if (Test-Path -LiteralPath $streamPath) {
                        $xperfWarnings += @(Get-Content -LiteralPath $streamPath | Where-Object { $_.Trim() })
                    }
                }
                @{ state = if ($xperfCode -eq 0 -and $reportBytes -gt 0) { 'complete' } else { 'failed' };
                   exit_code = $xperfCode; report = $cpuReport; report_bytes = $reportBytes;
                   warnings = $xperfWarnings } |
                    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionRoot 'cpu-profile-analysis.json') -Encoding UTF8
                if ($xperfCode -eq 0 -and $reportBytes -gt 0) {
                    if ($xperfWarnings -match 'lost|unreliable|dropped') {
                        Write-Warning "CPU trace summary was generated, but xperf reported lost events; review $xperfStderr."
                    } else {
                        Write-Output "CPU profile and module summary saved: $cpuReport"
                    }
                } else {
                    Write-Warning "Xperf could not summarize the CPU trace (exit $xperfCode); details are in $xperfLog and the ETL was retained."
                }
                $xperfDiagnostics = @()
                foreach ($streamPath in @($xperfStdout, $xperfStderr)) {
                    if (Test-Path -LiteralPath $streamPath) {
                        $xperfDiagnostics += @(Get-Content -LiteralPath $streamPath)
                    }
                }
                $xperfDiagnostics | Set-Content -LiteralPath $xperfLog -Encoding UTF8
            } else {
                Write-Warning "CPU profile did not complete: $($cpuStatus.detail)"
            }
        } else {
            Write-Warning 'CPU profile helper did not write a completion status.'
        }
    }
    if ($CapturePresentMon -and $presentMonProcess) {
        if ($presentMonProcess -and !$presentMonProcess.WaitForExit(15000)) {
            Write-Warning "PresentMon PID $($presentMonProcess.Id) is still active after GD exited; stopping the session capture so its CSV is released."
            try {
                Stop-Process -Id $presentMonProcess.Id -Force -ErrorAction Stop
                if ($presentMonProcess.WaitForExit(5000)) {
                    Write-Output 'Stopped leftover PresentMon capture after GD exited.'
                } else {
                    Write-Warning "PresentMon PID $($presentMonProcess.Id) did not stop; its CSV may remain locked."
                }
            } catch {
                Write-Warning "Could not stop PresentMon PID $($presentMonProcess.Id): $($_.Exception.Message)"
            }
        }
        if ($presentMonProcess -and $presentMonProcess.HasExited) {
            $presentMonExitCode = $presentMonProcess.ExitCode
            $presentMonState = if ($presentMonExitCode -eq 0) { 'exited' } else { 'exited-nonzero' }
        } elseif ($presentMonProcess) { $presentMonState = 'did-not-exit' }
        $captureDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 250
            $csvReady = (Test-Path -LiteralPath $presentMonCsv) -and (Get-Item -LiteralPath $presentMonCsv).Length -gt 0
        } while (!$csvReady -and [DateTime]::UtcNow -lt $captureDeadline)
        if ($csvReady) {
            $presentMonState = 'csv-saved'
            Write-Output 'PresentMon frame CSV saved.'
        } else {
            $presentMonState = 'no-csv'
            Write-Warning "PresentMon produced no CSV (exit=$presentMonExitCode); see presentmon.stdout.log and presentmon.stderr.log in $sessionRoot."
            foreach ($captureLog in @($presentMonStdout, $presentMonStderr)) {
                if (Test-Path -LiteralPath $captureLog) {
                    Get-Content -LiteralPath $captureLog -Tail 8 | Where-Object { $_.Trim() } | Write-Output
                }
            }
        }
    } elseif ($CapturePresentMon) {
        Write-Warning 'PresentMon capture was requested but did not start; see the runner stderr log.'
    }
    $logs = Join-Path $gameRoot 'geode\logs'
    $routePattern = '"pid"\s*:\s*' + $gameProcess.Id + '\b'
    $fallbackLines = @()
    foreach ($log in (Get-ChildItem -LiteralPath $logs -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 5)) {
        $lines = @(Get-Content -LiteralPath $log.FullName | Where-Object { $_ -match '\[Vulkanizer\]|Failed to load binary|4551' })
        if (!$fallbackLines.Count) { $fallbackLines = $lines }
        if ($lines -match $routePattern) {
            [IO.File]::WriteAllLines((Join-Path $sessionRoot 'geode-vulkanizer.log'), [string[]]$lines, [Text.UTF8Encoding]::new($false))
            break
        }
    }
    if (!(Test-Path -LiteralPath (Join-Path $sessionRoot 'geode-vulkanizer.log')) -and $fallbackLines.Count) {
        [IO.File]::WriteAllLines((Join-Path $sessionRoot 'geode-vulkanizer.log'), [string[]]$fallbackLines, [Text.UTF8Encoding]::new($false))
    }
} finally {
    if ($gameStartUtc) {
        if (!$gameEndUtc) { $gameEndUtc = [DateTime]::UtcNow }
        $sessionSummary = Get-Content -LiteralPath (Join-Path $sessionRoot 'session.json') -Raw | ConvertFrom-Json
        $sessionSummary.state = if ($gameExitCode -eq 0) { 'completed' } elseif ($null -ne $gameExitCode) { 'exited-nonzero' } else { 'interrupted' }
        $sessionSummary.ended_utc = $gameEndUtc.ToString('o')
        $sessionSummary.elapsed_seconds = [Math]::Round(($gameEndUtc - $gameStartUtc).TotalSeconds, 3)
        $sessionSummary.game_exit_code = $gameExitCode
        $sessionSummary.presentmon_state = $presentMonState
        $sessionSummary.presentmon_exit_code = $presentMonExitCode
        $sessionSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sessionRoot 'session.json') -Encoding UTF8
    }
    foreach ($key in $environmentKeys) { [Environment]::SetEnvironmentVariable($key, $oldEnvironment[$key], 'Process') }
    if ($CaptureCpuProfile -and $cpuTraceProcess -and !(Test-Path -LiteralPath $cpuProfilePidFile) -and
        !$cpuTraceProcess.HasExited) {
        'cancel' | Set-Content -LiteralPath $cpuProfileCancelFile -Encoding ascii
        [void]$cpuTraceProcess.WaitForExit(10000)
    }
    if ($installed) {
        if (Get-Process GeometryDash -ErrorAction SilentlyContinue) {
            Write-Warning 'GD is still running. Files were retained for safety. After closing GD, run with -RestoreOnly.'
        } else { Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $SettingsPath }
    }
}
Write-Output 'No Vulkan milestone is inferred from launch success. Run verify-gd-vulkan.py against this session and report visual results separately.'
