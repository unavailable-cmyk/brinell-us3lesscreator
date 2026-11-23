param(
    [Parameter(Mandatory)][string]$SessionRoot,
    [Parameter(Mandatory)][string]$InstanceName
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $SessionRoot).Path
$wpr = (Get-Command wpr.exe -ErrorAction Stop).Source
$profilePath = Join-Path $PSScriptRoot 'VulkanizerCpu.wprp'
if (!(Test-Path -LiteralPath $profilePath -PathType Leaf)) {
    throw "Focused WPR profile is missing: $profilePath"
}
$tracePath = Join-Path $root 'cpu-profile.etl'
$readyPath = Join-Path $root 'cpu-profile.ready'
$pidPath = Join-Path $root 'cpu-profile.pid.json'
$cancelPath = Join-Path $root 'cpu-profile.cancel'
$statusPath = Join-Path $root 'cpu-profile.status.json'
$logPath = Join-Path $root 'cpu-profile-wpr.log'
$started = $false

function Write-Status([string]$State, [string]$Detail) {
    @{ state = $State; detail = $Detail; trace = $tracePath; helper_pid = $PID } |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
}

function Invoke-Wpr([string[]]$Arguments) {
    $lines = @(& $wpr @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($lines.Count) { $lines | Add-Content -LiteralPath $logPath -Encoding UTF8 }
    return $exitCode
}

try {
    $startCode = Invoke-Wpr @('-start', ($profilePath + '!VulkanizerCpu'), '-filemode', '-instancename', $InstanceName)
    if ($startCode -ne 0) { throw "WPR start failed with exit code $startCode" }
    $started = $true
    'ready' | Set-Content -LiteralPath $readyPath -Encoding ascii

    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    while (!(Test-Path -LiteralPath $pidPath)) {
        if (Test-Path -LiteralPath $cancelPath) { throw 'Capture cancelled before Geometry Dash started' }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Timed out waiting for Geometry Dash process ID' }
        Start-Sleep -Milliseconds 200
    }

    $target = Get-Content -LiteralPath $pidPath -Raw | ConvertFrom-Json
    while ($true) {
        if (Test-Path -LiteralPath $cancelPath) { break }
        $process = Get-Process -Id ([int]$target.pid) -ErrorAction SilentlyContinue
        if (!$process) { break }
        if ($process.StartTime.ToUniversalTime().Ticks -ne [long]$target.start_ticks) { break }
        Start-Sleep -Milliseconds 200
    }

    $stopCode = Invoke-Wpr @('-stop', $tracePath, 'Vulkanizer CPU stack sample', '-instancename', $InstanceName)
    if ($stopCode -ne 0) { throw "WPR stop failed with exit code $stopCode" }
    $started = $false
    Write-Status 'complete' 'CPU verbose profile captured and saved.'
} catch {
    $message = $_.Exception.Message
    "ERROR: $message" | Add-Content -LiteralPath $logPath -Encoding UTF8
    if ($started) {
        [void](Invoke-Wpr @('-cancel', '-instancename', $InstanceName))
        $started = $false
    }
    Write-Status 'failed' $message
    exit 1
}
