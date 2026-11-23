param(
    [string]$LevelName = 'Peaceful 2',
    [ValidateRange(3,90)][int]$DurationSeconds = 30,
    [ValidateRange(1,5)][int]$Repeats = 2,
    [ValidateSet('OpenGL','Zink')][string]$Backend = 'OpenGL'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$folder = Join-Path $root ('build/diagnostic-reports/retention-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$keys = @('VULKANIZER_V2_RETENTION_AUDIT','VULKANIZER_V2_LAZY_ROUTES','VULKANIZER_V2_MOVE_SECTIONS')
$previous = @{}
foreach ($key in $keys) { $previous[$key] = [Environment]::GetEnvironmentVariable($key,'Process') }
$summary = @()
$packageHash = $null
try {
    $env:VULKANIZER_V2_RETENTION_AUDIT = '1'
    $env:VULKANIZER_V2_MOVE_SECTIONS = '1'
    for ($i = 1; $i -le $Repeats; $i++) {
        # Alternate ordering so a cold first run is not always one variant.
        $order = if ($i % 2) { @('eager','lazy') } else { @('lazy','eager') }
        foreach ($variant in $order) {
            $env:VULKANIZER_V2_LAZY_ROUTES = if ($variant -eq 'lazy') { '1' } else { '0' }
            $out = Join-Path $folder "$variant-$i"
            Write-Host "Running $variant ($i/$Repeats)..."
            & (Join-Path $PSScriptRoot 'test-v2-live.ps1') -Mode shared -LevelName $LevelName `
                -DurationSeconds $DurationSeconds -Backend $Backend -AggressiveOwnership -OutputDirectory $out | Out-Null
            $r = Get-Content (Join-Path $out 'shared.json') -Raw | ConvertFrom-Json
            if ($r.reason -ne 'test_complete' -or !$r.retention_audit) {
                throw "Missing instrumented test_complete report in $out. Build the patched package first."
            }
            $a = $r.retention_audit
            if ([bool]$a.lazy_routes -ne ($variant -eq 'lazy')) {
                throw "Lazy-route flag was not applied for $variant."
            }
            $manifest = Get-Content (Join-Path $out 'shared.execution.json') -Raw | ConvertFrom-Json
            if ($null -eq $packageHash) { $packageHash = $manifest.package_sha256 }
            if ($packageHash -ne $manifest.package_sha256) { throw 'Package changed during A/B.' }
            $w = $a.window_80_90
            $ticks = [double]$a.simulation_ticks
            $summary += [pscustomobject]@{
                variant = $variant; repeat = $i; ticks = $ticks; level_time = $r.level_time
                native_visits = $r.native_move_member_visits
                coverage = $r.resident_gpu.movement_coverage
                native_move_ms = $a.native_move_excluding_nested_handoff_and_sync_ms
                native_move_ms_per_1000_ticks = if ($ticks -gt 0) { 1000 * $a.native_move_excluding_nested_handoff_and_sync_ms / $ticks } else { $null }
                frame_ms_per_1000_ticks = if ($ticks -gt 0) { 1000 * $a.frame_elapsed_sum_ms / $ticks } else { $null }
                native_share = $a.native_move_exclusive_frame_share
                refresh_ms = $a.route_refresh_ms_inclusive; refreshes = $a.route_refreshes
                handoff_ms = $a.handoff_exclusive_ms; sync_ms = $a.sync_ms
                attributed_visits = $a.post_revocation_visits_stable_routes
                expected_visits = $a.expected_post_revocation_visits_stable_routes
                attribution_mismatched_routes = $a.attribution_mismatched_routes
                window_native_share = $w.native_move_exclusive_frame_share
                window_level_start = $w.actual_level_start; window_level_end = $w.actual_level_end
                window_native_visits = $w.native_move_visits
            }
            $summary | Export-Csv (Join-Path $folder 'summary.csv') -NoTypeInformation -Encoding UTF8
        }
    }
} finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}
$summary | Format-Table variant,repeat,ticks,native_visits,coverage,native_move_ms_per_1000_ticks,frame_ms_per_1000_ticks,refresh_ms -AutoSize
Write-Host "Reports: $folder"
