param(
    [Parameter(Mandatory)][string]$ReferenceLog,
    [Parameter(Mandatory)][string]$CandidateLog,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$GameplaySpeedReport
)
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Read-Capture([string]$path) {
    $points = @{}; $frames = @(); $levelTime = 0.0; $stream = $null; $culling = $null
    $passed = $false; $failures = @(); $build = $null; $activation = $false; $inGame = $false; $fixedInput = $false
    foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $path).Path)) {
        if ($line -match 'VULKANIZER_BUILD (.+)$') { $build = $Matches[1] }
        if ($line -match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST starting') { $inGame = $true }
        if ($line -match 'VULKANIZER_FIXED_INPUT') { $fixedInput = $true }
        if ($line -match 'VULKANIZER_TRANSFORM_CHECKPOINT schema=2 step=(\d+) level_time=([\d.]+) samples=(\d+) hash=(\w+) player=\(([^)]+)\)') {
            $step = $Matches[1]; $levelTime = [double]::Parse($Matches[2], $culture)
            $points[$step] = @{ time=$Matches[2]; samples=$Matches[3]; hash=$Matches[4]; player=$Matches[5] }
        }
        if ($line -match 'VULKANIZER_FRAME_LIGHT scene=gameplay frames=(\d+) mean_ms=([\d.]+) max_ms=([\d.]+)') {
            # The most recent checkpoint labels the interval approximately;
            # this is an interval average, not a per-frame percentile.
            $frames += [pscustomobject]@{level_time=$levelTime; frames=[int]$Matches[1]; mean_ms=[double]::Parse($Matches[2], $culture); max_ms=[double]::Parse($Matches[3], $culture)}
        }
        if ($inGame -and $line -match 'VULKANIZER_VISUAL_STREAM (.+)$') { $stream = $Matches[1] }
        if ($inGame -and $line -match 'VULKANIZER_TRANSFORM_CULLING (.+)$') { $culling = $Matches[1] }
        if ($inGame -and $line -match 'VULKANIZER_VISUAL_GPU_PARITY PASS') { $activation = $true }
        if ($line -match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST result=PASS') { $passed = $true }
        if ($line -match 'Visual GPU stream disabled|culling bounds failed|SCHEDULER_PROOF.*FAIL|CULLING.*FAIL|VK_ERROR_DEVICE_LOST|fence.*timeout') { $failures += $line }
    }
    return @{points=$points; frames=$frames; stream=$stream; culling=$culling; passed=$passed; failures=$failures; build=$build; activation=$activation; fixed_input=$fixedInput}
}
function Frame-Summary($capture) {
    $heavy = @($capture.frames | Where-Object { $_.level_time -ge 77 -and $_.level_time -le 90 })
    if (!$heavy.Count) { return $null }
    $count = ($heavy | Measure-Object frames -Sum).Sum
    $elapsed = ($heavy | ForEach-Object { $_.frames * $_.mean_ms } | Measure-Object -Sum).Sum
    return @{level_time_range='77..90 (approximate checkpoint labels)'; frames=$count; mean_ms=$elapsed/$count; fps=$count*1000/$elapsed; worst_interval_mean_ms=($heavy | Measure-Object mean_ms -Maximum).Maximum; max_frame_ms=($heavy | Measure-Object max_ms -Maximum).Maximum}
}
$reference = Read-Capture $ReferenceLog; $candidate = Read-Capture $CandidateLog
$matched = 0; $mismatches = @()
foreach ($step in $candidate.points.Keys | Sort-Object {[int]$_}) {
    if (!$reference.points.ContainsKey($step)) { continue }
    $a = $reference.points[$step]; $b = $candidate.points[$step]
    if ($a.time -eq $b.time -and $a.samples -eq $b.samples -and $a.hash -eq $b.hash -and $a.player -eq $b.player) { $matched++ }
    else { $mismatches += @{step=[int]$step; reference=$a; candidate=$b} }
}
$sameBuild = $reference.build -and $reference.build -eq $candidate.build
$valid = $reference.passed -and $candidate.passed -and $candidate.activation -and $matched -gt 16 -and !$mismatches.Count -and !$reference.failures.Count -and !$candidate.failures.Count -and $sameBuild
$speedValidated = $false
if ($GameplaySpeedReport) {
    $speed = Get-Content -LiteralPath $GameplaySpeedReport -Raw | ConvertFrom-Json
    $speedValidated = $speed.valid_gameplay_speed_parity -and
        (Resolve-Path -LiteralPath $speed.reference_log).Path -eq (Resolve-Path -LiteralPath $ReferenceLog).Path -and
        (Resolve-Path -LiteralPath $speed.candidate_log).Path -eq (Resolve-Path -LiteralPath $CandidateLog).Path
}
$report = [ordered]@{
    valid_comparison=[bool]$valid; same_build=[bool]$sameBuild
    reference_log=$ReferenceLog; candidate_log=$CandidateLog
    matched_checkpoints=$matched; mismatched_checkpoints=$mismatches.Count
    first_mismatches=@($mismatches | Select-Object -First 3)
    native_test_completed=$reference.passed; candidate_test_completed=$candidate.passed
    actual_gpu_application=$candidate.activation
    fixed_input_correctness_only=$candidate.fixed_input
    gameplay_speed_validated=[bool]$speedValidated
    valid_performance_capture=($valid -and $speedValidated -and !$candidate.fixed_input -and !$reference.fixed_input)
    failures=@($reference.failures + $candidate.failures)
    reference_heavy_interval=Frame-Summary $reference
    candidate_heavy_interval=Frame-Summary $candidate
    candidate_stream_cumulative=$candidate.stream; candidate_culling_cumulative=$candidate.culling
    limitation='Timings are interval averages. GPU wait includes queued GPU work; do not add GPU execution to it. State hashes sample objects, not every object. Performance claims require valid_comparison=true.'
}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
[pscustomobject]@{valid_comparison=[bool]$valid; matched=$matched; mismatched=$mismatches.Count; report=$OutputPath} | ConvertTo-Json -Compress
if (!$valid) { exit 1 }
